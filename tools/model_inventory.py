#!/usr/bin/env python3
"""
model_inventory.py — inventory Yandex/HOSKEY model assets and infer runtime roles.

The goal is not to "load" the models.  It gives a deterministic, offline
answer to these engineering questions:

  * what model assets are referenced by Yandex config;
  * which of them are packed into ru.hoskey.hosdict;
  * which raw/TFLite/MSL2 files exist locally;
  * which tensor signatures look like subword NNLM, char LM, word LM, ranker,
    tap/autocc, or just state projector blocks;
  * whether the currently packed assets match HOSKEY runtime expectations.

This script intentionally reuses tools/analyze_models.py for TFLite FlatBuffer
inspection instead of duplicating the parser.
"""

from __future__ import annotations

import argparse
import datetime as _dt
import importlib.util
import json
import math
import struct
import sys
from dataclasses import asdict, dataclass
from pathlib import Path
from typing import Any, Iterable


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_HOSDICT = ROOT / "entry/src/main/resources/rawfile/dict/ru.hoskey.hosdict"
DEFAULT_RAW_DIR = Path(r"C:/Users/Kharki/Desktop/yandex_extracted/extracted_sections")
DEFAULT_MS_DIR = Path(r"C:/Users/Kharki/Desktop/yandex_extracted/ms_models")
DEFAULT_TFLITE_DIR = Path(r"C:/Users/Kharki/clawd/yandex_extracted/yandex_extracted")
DEFAULT_REPORT = ROOT / f"reverse/MODEL_INVENTORY_{_dt.date.today():%Y-%m-%d}.md"

RAW_TO_PACKED_KEYS = {
    "config": ["config"],
    "trie": ["lexicon.default"],
    "KEYBOARD-13421_trie": ["lexicon.experiments.KEYBOARD-13421_v2"],
    "rules.bin": ["rules.transfeme"],
    "KEYBOARD-13421_rules.bin": ["rules.transfeme_experiments.KEYBOARD-13421_v2"],
    "normdict": ["normdict.default"],
    "lemmer_mhash": ["lemma.hash"],
    "lemmer_rules": ["lemma.rules"],
    "blacklist": ["policy.blacklist"],
    "blacklist_suggest": ["policy.blacklist_suggest"],
    "autocorrect_blocker": ["policy.autocorrect_blocker"],
    "abbr": ["policy.abbr"],
    "features_autocc.json": ["features.autocc"],
    "features_ranker.json": ["features.ranker"],
    "features_tap_model.json": ["features.tap_model"],
    "ranker.ms": ["ranking.ranker"],
    "neural_model.ms": ["ranking.neural_model"],
    "ranker": ["ranking.ranker", "ranking.catboost"],
    "ranker_swipe": ["ranking.swipe"],
    "ranker_swipe_backoff": ["ranking.swipe_backoff"],
    "swipe_blocker": ["ranking.swipe_blocker"],
    "neural_model": ["ranking.neural_model", "neural.word_model"],
    "KEYBOARD-13321_nnlm_model": ["neural.subword_nnlm.KEYBOARD-13321"],
    "char_model": ["neural.char_model"],
    "rnnt_model": ["rnnt.model"],
    "tree_autocorrect_model": ["autocc.tree_model.raw"],
    "tap_model_ranker": ["tap.model_ranker.raw"],
    "tap_model_classifier": ["tap.model_classifier.raw"],
    "tap_models_default": ["tap.models_default"],
    "search_emoji_model": ["emoji.search_model.raw"],
    "emoji_model": ["emoji.suggest_model.raw"],
    "KEYBOARD-13321_emoji_suggest": ["emoji.suggest_model.KEYBOARD-13321"],
    "KEYBOARD-13321_autocc": ["autocc.tree_model.KEYBOARD-13321"],
    "KEYBOARD-13321_ranker": ["ranking.catboost.KEYBOARD-13321"],
    "KEYBOARD-13321_ranker_swipe": ["ranking.swipe.KEYBOARD-13321"],
    "KEYBOARD-13321_ranker_swipe_backoff": ["ranking.swipe_backoff.KEYBOARD-13321"],
    "KEYBOARD-13321_rnnt_model": ["rnnt.model.KEYBOARD-13321"],
    "KEYBOARD-13321_tap_model_ranker": ["tap.model_ranker.KEYBOARD-13321"],
    "KEYBOARD-13321_tap_model_classifier": ["tap.model_classifier.KEYBOARD-13321"],
    "KEYBOARD-13421_autocc": ["autocc.tree_model.KEYBOARD-13421"],
    "KEYBOARD-13421_ranker": ["ranking.catboost.KEYBOARD-13421"],
    "KEYBOARD-13421_ranker_swipe": ["ranking.swipe.KEYBOARD-13421"],
    "KEYBOARD-13421_ranker_swipe_backoff": ["ranking.swipe_backoff.KEYBOARD-13421"],
    "KEYBOARD-13421_tap_model_ranker": ["tap.model_ranker.KEYBOARD-13421"],
    "KEYBOARD-13421_tap_model_classifier": ["tap.model_classifier.KEYBOARD-13421"],
    "features_ranker_swipe.json": ["features.ranker_swipe"],
    "features_ranker_swipe_backoff.json": ["features.ranker_swipe_backoff"],
    "features_swipe_blocker.json": ["features.swipe_blocker"],
    "KEYBOARD-13421_features_tap_model": ["features.experiments.KEYBOARD-13421_tap_model"],
}


@dataclass
class HosdictAsset:
    key: str
    fmt: str
    offset: int
    size: int
    magic: str


@dataclass
class ModelRecord:
    name: str
    source: str
    path: str
    size: int
    magic: str
    tfl3_offsets: list[int]
    alias_tflite: str | None
    inputs: list[dict[str, Any]]
    outputs: list[dict[str, Any]]
    operators: list[str]
    role: str
    current_ranker_compatible: bool
    current_nnlm_compatible: bool
    note: str


def die(msg: str) -> None:
    print(f"model_inventory: {msg}", file=sys.stderr)
    raise SystemExit(1)


def read_magic(data: bytes) -> str:
    if len(data) >= 8 and data[4:8] == b"TFL3":
        return "TFL3"
    if len(data) >= 8 and data[4:8] == b"MSL2":
        return "MSL2"
    if data.startswith(b"ynnlm_v1"):
        return "ynnlm_v1"
    if len(data) >= 4:
        return "raw:" + data[:4].hex()
    return "too_small"


def tfl3_offsets(data: bytes) -> list[int]:
    out: list[int] = []
    pos = 0
    while True:
        idx = data.find(b"TFL3", pos)
        if idx < 0:
            break
        # In a TFLite file the magic is at file_start + 4.
        out.append(max(0, idx - 4))
        pos = idx + 4
    return out


def parse_hosdict(path: Path) -> list[HosdictAsset]:
    if not path.exists():
        die(f".hosdict not found: {path}")
    data = path.read_bytes()
    if len(data) < 16:
        die(".hosdict too small")
    magic, version, count, manifest_size = struct.unpack_from("<IIII", data, 0)
    if magic != 0x444B5348:
        die(f".hosdict magic mismatch: 0x{magic:08x}")
    if version != 1:
        die(f".hosdict unsupported version: {version}")
    off = 16 + manifest_size
    assets: list[HosdictAsset] = []
    for _ in range(count):
        key_len = struct.unpack_from("<I", data, off)[0]
        off += 4
        key = data[off : off + key_len].decode("utf-8", errors="replace")
        off += key_len
        fmt_len = struct.unpack_from("<I", data, off)[0]
        off += 4
        fmt = data[off : off + fmt_len].decode("utf-8", errors="replace")
        off += fmt_len
        asset_off, size = struct.unpack_from("<QQ", data, off)
        off += 16
        blob = data[asset_off : asset_off + size]
        assets.append(HosdictAsset(key, fmt, asset_off, size, read_magic(blob)))
    return assets


def load_tflite_analyzer():
    path = ROOT / "tools/analyze_models.py"
    spec = importlib.util.spec_from_file_location("hoskey_analyze_models", path)
    if spec is None or spec.loader is None:
        die(f"cannot import {path}")
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod.analyze_tflite_raw


def shape_product(shape: Iterable[int]) -> int:
    result = 1
    for dim in shape:
        result *= int(dim)
    return result


def tensor_shapes(tensors: list[dict[str, Any]]) -> list[list[int]]:
    return [list(t.get("shape", [])) for t in tensors]


def has_shape(tensors: list[dict[str, Any]], expected: list[int]) -> bool:
    return any(list(t.get("shape", [])) == expected for t in tensors)


def classify_role(name: str, inputs: list[dict[str, Any]],
                  outputs: list[dict[str, Any]], magic: str) -> str:
    lname = name.lower()
    in_shapes = tensor_shapes(inputs)
    out_shapes = tensor_shapes(outputs)

    if magic == "ynnlm_v1":
        return "ynnlm_v1 wrapper with embedded TFLite components"
    if has_shape(outputs, [1, 1, 7000]) and has_shape(inputs, [1, 2500]):
        return "SubwordNNLMDecoder"
    if has_shape(inputs, [1, 2500]) and has_shape(outputs, [1, 1752]):
        return "WordLMDecoder"
    if has_shape(inputs, [1, 1, 100]) and has_shape(inputs, [1, 1, 750]):
        return "CharLMJoint"
    if has_shape(inputs, [1, 500]) and has_shape(outputs, [1, 1500]):
        return "EncDecStateProjector"
    if has_shape(inputs, [1, 500]) and has_shape(outputs, [1, 2500]):
        return "SubwordStateProjector"
    if has_shape(inputs, [1, 1, 24]) and has_shape(outputs, [1, 1, 525]):
        return "CnnEmbedding"
    if "autocc" in lname or "autocorrect" in lname:
        return "AutocorrectModel"
    if "tap_model" in lname:
        return "TapModelComponent"
    if "ranker" in lname or "swipe" in lname:
        return "RankerOrSwipeComponent"
    if "emoji" in lname:
        return "EmojiModel"
    if magic == "MSL2":
        return "MindSporeLiteModel"
    if magic == "TFL3":
        return "TFLiteModel"
    return "UnknownBinary"


def current_ranker_compatible(inputs: list[dict[str, Any]],
                              outputs: list[dict[str, Any]]) -> bool:
    # HOSKEY MLRanker currently expects one float tensor with 39 features and a
    # scalar-ish output.  Yandex CatBoost/TFLite components seen so far do not
    # match this shape.
    return (
        any(shape_product(t.get("shape", [])) == 39 for t in inputs)
        and any(shape_product(t.get("shape", [])) == 1 for t in outputs)
    )


def current_nnlm_compatible(inputs: list[dict[str, Any]],
                            outputs: list[dict[str, Any]]) -> bool:
    # HOSKEY NeuralLM currently expects token id + [1x2500] state, and the
    # subword decoder outputs [1x1x7000] plus next state.
    return (
        has_shape(inputs, [1, 1])
        and has_shape(inputs, [1, 2500])
        and has_shape(outputs, [1, 1, 7000])
        and has_shape(outputs, [1, 2500])
    )


def find_alias_tflite(name: str, tflite_dir: Path) -> Path | None:
    candidates = [
        tflite_dir / f"{name}.tflite",
        tflite_dir / name,
    ]
    if name.endswith(".ms"):
        candidates.append(tflite_dir / f"{name[:-3]}.tflite")
    for path in candidates:
        if path.exists():
            return path
    return None


def analyze_record(name: str, source: str, path: Path, analyzer,
                   tflite_dir: Path) -> ModelRecord:
    data = path.read_bytes()
    magic = read_magic(data)
    offsets = tfl3_offsets(data)
    alias = find_alias_tflite(path.name, tflite_dir) or find_alias_tflite(name, tflite_dir)
    parse_path: Path | None = path if magic == "TFL3" else alias

    inputs: list[dict[str, Any]] = []
    outputs: list[dict[str, Any]] = []
    operators: list[str] = []
    note = ""
    if parse_path and parse_path.exists():
        try:
            result = analyzer(parse_path)
            inputs = result.get("inputs", []) or []
            outputs = result.get("outputs", []) or []
            operators = result.get("operators", []) or []
            if result.get("error"):
                note = str(result["error"])
        except Exception as exc:  # pragma: no cover - diagnostic tool
            note = f"TFLite parse failed: {exc}"
    elif magic == "ynnlm_v1":
        note = "wrapper: parse embedded TFL3 components separately"

    role = classify_role(name, inputs, outputs, magic)
    return ModelRecord(
        name=name,
        source=source,
        path=str(path),
        size=len(data),
        magic=magic,
        tfl3_offsets=offsets[:8],
        alias_tflite=str(alias) if alias else None,
        inputs=inputs,
        outputs=outputs,
        operators=operators,
        role=role,
        current_ranker_compatible=current_ranker_compatible(inputs, outputs),
        current_nnlm_compatible=current_nnlm_compatible(inputs, outputs),
        note=note,
    )


def collect_config_asset_refs(raw_dir: Path) -> list[tuple[str, str]]:
    config_path = raw_dir / "config"
    if not config_path.exists():
        return []
    data = json.loads(config_path.read_text(encoding="utf-8"))
    refs: list[tuple[str, str]] = []

    def walk(obj: Any, path: str) -> None:
        if isinstance(obj, dict):
            for k, v in obj.items():
                walk(v, f"{path}.{k}" if path else k)
        elif isinstance(obj, list):
            for i, v in enumerate(obj):
                walk(v, f"{path}[{i}]")
        elif isinstance(obj, str):
            if (raw_dir / obj).is_file() or (raw_dir / f"{obj}.json").is_file():
                refs.append((path, obj))

    walk(data, "")
    seen: set[tuple[str, str]] = set()
    deduped = []
    for item in refs:
        if item not in seen:
            seen.add(item)
            deduped.append(item)
    return deduped


def fmt_size(n: int) -> str:
    if n >= 1024 * 1024:
        return f"{n / 1024 / 1024:.2f} MB"
    if n >= 1024:
        return f"{n / 1024:.1f} KB"
    return f"{n} B"


def fmt_tensors(tensors: list[dict[str, Any]]) -> str:
    if not tensors:
        return "-"
    parts = []
    for t in tensors:
        name = str(t.get("name", "?")).split("/")[-1]
        shape = "x".join(str(x) for x in t.get("shape", []))
        parts.append(f"{name}[{shape}]")
    return "<br>".join(parts)


def md_escape(text: Any) -> str:
    return str(text).replace("|", "\\|").replace("\n", " ")


def write_report(path: Path, hosdict_assets: list[HosdictAsset],
                 config_refs: list[tuple[str, str]],
                 records: list[ModelRecord]) -> None:
    by_name_source = {(r.source, r.name): r for r in records}
    tflite_records = [r for r in records if r.inputs or r.outputs or r.magic == "TFL3"]
    ms_records = [r for r in records if r.magic == "MSL2"]
    raw_records = [r for r in records if r.source == "raw"]

    packed_model_assets = [
        a for a in hosdict_assets
        if "model" in a.fmt or a.key.startswith(("ranking.", "autocc.", "neural.", "tap.", "emoji.", "rnnt."))
    ]

    findings = [
        f"Current `.hosdict` packs {len(packed_model_assets)} model-like assets: "
        + ", ".join(a.key for a in packed_model_assets) + ".",
        "The real subword NNLM signature is the `[1x1] + [1x2500] -> [1x1x7000] + [1x2500]` decoder.",
        "The current generic `MLRanker` shape (`39 float features -> scalar`) is not matched by the observed Yandex TFLite aliases.",
        "`ynnlm_v1` raw blobs are wrappers with embedded TFLite components; extracting from first `TFL3` to EOF is not a valid model split.",
    ]

    lines: list[str] = []
    lines.append("# HOSKEY model inventory")
    lines.append("")
    lines.append(f"Generated: {_dt.datetime.now().isoformat(timespec='seconds')}")
    lines.append("")
    lines.append("## Executive findings")
    lines.append("")
    for f in findings:
        lines.append(f"- {f}")
    lines.append("")

    lines.append("## Current packaged `.hosdict` assets")
    lines.append("")
    lines.append("| key | format | size | magic |")
    lines.append("|---|---:|---:|---|")
    for a in hosdict_assets:
        lines.append(
            f"| `{md_escape(a.key)}` | `{md_escape(a.fmt)}` | {fmt_size(a.size)} | `{a.magic}` |"
        )
    lines.append("")

    lines.append("## Config-referenced local assets")
    lines.append("")
    lines.append("| config path | asset | raw exists | packed key(s) |")
    lines.append("|---|---|---:|---|")
    packed_keys = {a.key for a in hosdict_assets}
    for cfg_path, asset in config_refs:
        raw_exists = "yes" if any(r.name == asset for r in raw_records) else "no"
        packed = [key for key in RAW_TO_PACKED_KEYS.get(asset, []) if key in packed_keys]
        lines.append(
            f"| `{md_escape(cfg_path)}` | `{md_escape(asset)}` | {raw_exists} | "
            f"{', '.join(f'`{p}`' for p in packed) if packed else '-'} |"
        )
    lines.append("")

    lines.append("## TFLite signatures / role inference")
    lines.append("")
    lines.append("| source | name | size | role | inputs | outputs | current ranker? | current NNLM? |")
    lines.append("|---|---|---:|---|---|---|---:|---:|")
    for r in sorted(tflite_records, key=lambda x: (x.role, x.name)):
        lines.append(
            f"| {r.source} | `{md_escape(r.name)}` | {fmt_size(r.size)} | "
            f"`{md_escape(r.role)}` | {fmt_tensors(r.inputs)} | {fmt_tensors(r.outputs)} | "
            f"{'yes' if r.current_ranker_compatible else 'no'} | "
            f"{'yes' if r.current_nnlm_compatible else 'no'} |"
        )
    lines.append("")

    lines.append("## MSL2 / packaged model correlation")
    lines.append("")
    lines.append("| source | name | size | alias TFLite | inferred alias role | compatible with current runner | note |")
    lines.append("|---|---|---:|---|---|---|---|")
    for r in sorted(ms_records, key=lambda x: (x.source, x.name)):
        compat = []
        if r.current_ranker_compatible:
            compat.append("MLRanker")
        if r.current_nnlm_compatible:
            compat.append("NeuralLM")
        lines.append(
            f"| {r.source} | `{md_escape(r.name)}` | {fmt_size(r.size)} | "
            f"{'`' + md_escape(Path(r.alias_tflite).name) + '`' if r.alias_tflite else '-'} | "
            f"`{md_escape(r.role)}` | {', '.join(compat) if compat else 'no'} | "
            f"{md_escape(r.note or '')} |"
        )
    lines.append("")

    lines.append("## Raw wrappers / embedded TFLite offsets")
    lines.append("")
    lines.append("| name | size | magic | embedded TFL3 starts | alias TFLite | inferred role |")
    lines.append("|---|---:|---|---|---|---|")
    for r in sorted(raw_records, key=lambda x: x.name):
        if r.magic == "TFL3" or r.tfl3_offsets or "model" in r.name.lower() or "ranker" in r.name.lower() or "autocc" in r.name.lower():
            starts = ", ".join(str(x) for x in r.tfl3_offsets) if r.tfl3_offsets else "-"
            alias = f"`{Path(r.alias_tflite).name}`" if r.alias_tflite else "-"
            lines.append(
                f"| `{md_escape(r.name)}` | {fmt_size(r.size)} | `{r.magic}` | {starts} | {alias} | `{md_escape(r.role)}` |"
            )
    lines.append("")

    lines.append("## Engineering conclusion")
    lines.append("")
    lines.append("- Do not route every model blob through one fake `MLRanker` or one fake `NeuralLM`.")
    lines.append("- Add separate runners: `SubwordNNLMRunner`, `WordLMRunner`, `CharLMRunner`, `CatboostOrTreeRankerRunner`, `TapModelRunner`, `AutoccModelRunner`.")
    lines.append("- Package missing config-referenced raw assets with semantic keys before attempting parity scoring.")
    lines.append("- Keep `1nc7` candidate materialization separate from NNLM scoring: trie supplies states/tokens/static LM; neural supplies probabilities/context scores.")
    lines.append("")

    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines), encoding="utf-8")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--hosdict", type=Path, default=DEFAULT_HOSDICT)
    parser.add_argument("--raw-dir", type=Path, default=DEFAULT_RAW_DIR)
    parser.add_argument("--ms-dir", type=Path, default=DEFAULT_MS_DIR)
    parser.add_argument("--tflite-dir", type=Path, default=DEFAULT_TFLITE_DIR)
    parser.add_argument("--out", type=Path, default=DEFAULT_REPORT)
    parser.add_argument("--json-out", type=Path, default=None)
    args = parser.parse_args()

    analyzer = load_tflite_analyzer()
    hosdict_assets = parse_hosdict(args.hosdict)
    config_refs = collect_config_asset_refs(args.raw_dir)

    records: list[ModelRecord] = []

    if args.raw_dir.exists():
        for path in sorted(args.raw_dir.iterdir()):
            if path.is_file():
                # Include all config-referenced files and all likely model files.
                lname = path.name.lower()
                if (
                    any(path.name == ref for _, ref in config_refs)
                    or any(tok in lname for tok in ("model", "ranker", "autocc", "swipe", "emoji", "rnnt", "char"))
                ):
                    records.append(analyze_record(path.name, "raw", path, analyzer, args.tflite_dir))

    if args.tflite_dir.exists():
        for path in sorted(args.tflite_dir.glob("*.tflite")):
            records.append(analyze_record(path.name, "tflite", path, analyzer, args.tflite_dir))

    if args.ms_dir.exists():
        for path in sorted(args.ms_dir.glob("*.ms")):
            records.append(analyze_record(path.name, "ms", path, analyzer, args.tflite_dir))

    # Add packaged model assets as logical records by writing temporary metadata
    # from their key/name where possible.  We do not extract blobs to disk here;
    # the .hosdict table already records magic and size.
    args.out = args.out if args.out.is_absolute() else ROOT / args.out
    write_report(args.out, hosdict_assets, config_refs, records)

    if args.json_out:
        args.json_out.parent.mkdir(parents=True, exist_ok=True)
        args.json_out.write_text(
            json.dumps(
                {
                    "hosdict_assets": [asdict(a) for a in hosdict_assets],
                    "config_refs": config_refs,
                    "records": [asdict(r) for r in records],
                },
                ensure_ascii=False,
                indent=2,
            ),
            encoding="utf-8",
        )

    print(f"MODEL INVENTORY OK: {args.out}")
    print(f"  hosdict_assets={len(hosdict_assets)}")
    print(f"  config_refs={len(config_refs)}")
    print(f"  records={len(records)}")


if __name__ == "__main__":
    main()
