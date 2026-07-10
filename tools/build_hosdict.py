#!/usr/bin/env python3
"""
build_hosdict.py — Packs assets into a HSKD container (.hosdict).

Reads from yandex_extracted + frequency data + MS models.
Output: single .hosdict container for the HOSKEY engine.
"""

import argparse
import json
import os
import struct
import sys
from dataclasses import dataclass

HSKD_MAGIC = 0x444B5348  # "HSKD" LE
HSKD_VERSION = 1
ALIGNMENT = 16

PROJECT_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
YANDEX_DIR = os.path.join("C:/Users/Kharki/Desktop/yandex_extracted/extracted_sections")
MS_MODELS_DIR = os.path.join("C:/Users/Kharki/Desktop/yandex_extracted/ms_models")
TFLITE_MODELS_DIR = os.path.join("C:/Users/Kharki/clawd/yandex_extracted/yandex_extracted")
TOOLS_DIR = os.path.dirname(os.path.abspath(__file__))
DICT_OUTPUT = os.path.join(PROJECT_ROOT, "entry/src/main/resources/rawfile/dict")
PREDICTOR_BINARY = os.path.join(TOOLS_DIR, "lexicon.predictor.hpt")
WEIGHTED_WORDS = os.path.join(TOOLS_DIR, "words_with_weights.tsv")


@dataclass
class AssetSpec:
    key: str
    fmt: str
    source: object
    label: str


def align(offset, alignment=ALIGNMENT):
    r = offset % alignment
    return offset + (alignment - r) if r else offset


def build_manifest():
    return json.dumps(
        {
            "manifest_version": "hosdict.manifest/v2",
            "engine_abi": "hoskey.engine/v2",
            "package_name": "ru.hoskey.default",
            "locale": "ru-RU",
        },
        ensure_ascii=False,
    ).encode("utf-8")


def find_file(*candidates):
    """Return first existing path from candidates."""
    for c in candidates:
        if os.path.exists(c):
            return c
    return None


def raw_model_format(path, fallback="raw_model_blob"):
    """Classify an extracted model blob by magic without trying to parse it."""
    try:
        with open(path, "rb") as f:
            head = f.read(16)
    except OSError:
        return fallback
    if head.startswith(b"ynnlm_v1"):
        return "ynnlm_v1_bundle"
    if len(head) >= 8 and head[4:8] == b"TFL3":
        return "tflite_model"
    if len(head) >= 8 and head[4:8] == b"MSL2":
        return "mindspore_model"
    return fallback


def is_cyrillic_or_hyphen(ch):
    return ("а" <= ch <= "я") or ("А" <= ch <= "Я") or ch in ("ё", "Ё", "-")


def normalize_predictor_word(word):
    return "".join("ё" if ch == "Ё" else ch.lower() for ch in word.strip())


def is_accepted_predictor_word(word):
    if not word:
        return False
    byte_len = len(word.encode("utf-8"))
    if byte_len < 2 or byte_len > 64:
        return False
    if len(word) < 2 or len(word) > 32:
        return False
    return all(is_cyrillic_or_hyphen(ch) for ch in word)


def read_weighted_words(path):
    """Match the runtime weighted_wordlist_v1 acceptance/dedup semantics."""
    words = []
    seen = set()
    with open(path, "r", encoding="utf-8", errors="ignore") as f:
        for raw in f:
            line = raw.strip()
            if not line:
                continue
            if "\t" in line:
                word, weight_s = line.split("\t", 1)
            else:
                word, weight_s = line, "0"
            word = normalize_predictor_word(word)
            if not is_accepted_predictor_word(word):
                continue
            if word in seen:
                continue
            seen.add(word)
            weight = 0
            for ch in weight_s.strip():
                if "0" <= ch <= "9":
                    weight = weight * 10 + (ord(ch) - ord("0"))
                else:
                    break
            words.append((word, weight))
    return words


def build_predictor_trie_blob(weighted_path):
    """
    Build runtime predictor as a compact binary trie instead of shipping a text
    word list.  Format predictor_trie_v1:

      magic "HPT1", version=1, node_count, word_count, edge_count, reserved
      repeated node_count: first_edge(u32), edge_count(u32), weight(i32),
                           max_subtree_weight(i32)
      repeated edge_count: label_unicode_codepoint(u32), child_node(u32)
    """
    weighted_words = read_weighted_words(weighted_path)

    nodes = [{"children": {}, "weight": -1, "max": -1}]
    word_count = 0
    for word, weight in weighted_words:
        node = 0
        for ch in word:
            children = nodes[node]["children"]
            child = children.get(ch)
            if child is None:
                child = len(nodes)
                children[ch] = child
                nodes.append({"children": {}, "weight": -1, "max": -1})
            node = child
        if nodes[node]["weight"] < 0:
            word_count += 1
            nodes[node]["weight"] = weight
        else:
            nodes[node]["weight"] = max(nodes[node]["weight"], weight)

    # Compute max_subtree_weight iteratively to avoid Python recursion limits.
    order = [0]
    for node in order:
        order.extend(nodes[node]["children"].values())
    for node in reversed(order):
        best = nodes[node]["weight"]
        for child in nodes[node]["children"].values():
            best = max(best, nodes[child]["max"])
        nodes[node]["max"] = best

    node_records = []
    edge_records = []
    for node in nodes:
        children = sorted(node["children"].items(), key=lambda item: ord(item[0]))
        first_edge = len(edge_records)
        for ch, child in children:
            edge_records.append((ord(ch), child))
        node_records.append((first_edge, len(children), node["weight"], node["max"]))

    blob = bytearray()
    blob += struct.pack(
        "<IIIIII", 0x31545048, 1, len(nodes), word_count, len(edge_records), 0
    )  # "HPT1" LE
    for first_edge, edge_count, weight, max_weight in node_records:
        blob += struct.pack("<IIii", first_edge, edge_count, weight, max_weight)
    for label, child in edge_records:
        blob += struct.pack("<II", label, child)
    return bytes(blob)


def env_flag(name):
    return os.environ.get(name, "").strip().lower() in ("1", "true", "yes", "on")


def append_asset_if_exists(assets, key, fmt, *candidates):
    path = find_file(*candidates)
    if path:
        assets.append(AssetSpec(key, fmt, path, os.path.basename(path)))
        return True
    return False


def append_raw_model_if_exists(assets, key, name, fmt=None):
    path = find_file(os.path.join(YANDEX_DIR, name))
    if path:
        assets.append(AssetSpec(key, fmt or raw_model_format(path), path, os.path.basename(path)))
        return True
    return False


def append_tflite_model_if_exists(assets, key, name):
    return append_asset_if_exists(
        assets,
        key,
        "tflite_model",
        os.path.join(TFLITE_MODELS_DIR, f"{name}.tflite"),
        os.path.join(TFLITE_MODELS_DIR, name),
    )


def append_full_model_assets(assets):
    """
    Add reverse-engineered Yandex neural/ranker/tap/autocc model assets.

    This is intentionally opt-in: the exact neural wrappers are large and the
    lean debug package must stay small and deterministic.  Use either
    --full-models or HOSKEY_PACK_FULL_MODELS=1.
    """
    # Extra feature configs used by ranker/swipe policy modules.
    extra_features = [
        ("features.ranker_swipe", "features_ranker_swipe.json"),
        ("features.ranker_swipe_backoff", "features_ranker_swipe_backoff.json"),
        ("features.swipe_blocker", "features_swipe_blocker.json"),
        ("features.experiments.KEYBOARD-13421_tap_model", "KEYBOARD-13421_features_tap_model"),
    ]
    for key, name in extra_features:
        append_asset_if_exists(
            assets,
            key,
            "feature_json",
            os.path.join(YANDEX_DIR, name),
            os.path.join(DICT_OUTPUT, name),
        )

    # Exact raw config-referenced Yandex wrappers/blobs.
    raw_models = [
        ("neural.word_model", "neural_model", None),
        ("neural.subword_nnlm.KEYBOARD-13321", "KEYBOARD-13321_nnlm_model", None),
        ("neural.char_model", "char_model", None),
        ("rnnt.model", "rnnt_model", None),
        ("ranking.catboost", "ranker", "catboost_model"),
        ("ranking.swipe", "ranker_swipe", "catboost_model"),
        ("ranking.swipe_backoff", "ranker_swipe_backoff", "catboost_model"),
        ("ranking.swipe_blocker", "swipe_blocker", "catboost_model"),
        ("autocc.tree_model.raw", "tree_autocorrect_model", None),
        ("tap.model_ranker.raw", "tap_model_ranker", None),
        ("tap.model_classifier.raw", "tap_model_classifier", None),
        ("tap.models_default", "tap_models_default", "tap_model_bundle"),
        ("emoji.search_model.raw", "search_emoji_model", None),
        ("emoji.suggest_model.raw", "emoji_model", None),
        ("emoji.suggest_model.KEYBOARD-13321", "KEYBOARD-13321_emoji_suggest", None),
        ("autocc.tree_model.KEYBOARD-13321", "KEYBOARD-13321_autocc", None),
        ("ranking.catboost.KEYBOARD-13321", "KEYBOARD-13321_ranker", "catboost_model"),
        ("ranking.swipe.KEYBOARD-13321", "KEYBOARD-13321_ranker_swipe", "catboost_model"),
        ("ranking.swipe_backoff.KEYBOARD-13321", "KEYBOARD-13321_ranker_swipe_backoff", "catboost_model"),
        ("rnnt.model.KEYBOARD-13321", "KEYBOARD-13321_rnnt_model", None),
        ("tap.model_ranker.KEYBOARD-13321", "KEYBOARD-13321_tap_model_ranker", None),
        ("tap.model_classifier.KEYBOARD-13321", "KEYBOARD-13321_tap_model_classifier", None),
        ("autocc.tree_model.KEYBOARD-13421", "KEYBOARD-13421_autocc", None),
        ("ranking.catboost.KEYBOARD-13421", "KEYBOARD-13421_ranker", "catboost_model"),
        ("ranking.swipe.KEYBOARD-13421", "KEYBOARD-13421_ranker_swipe", "catboost_model"),
        ("ranking.swipe_backoff.KEYBOARD-13421", "KEYBOARD-13421_ranker_swipe_backoff", "catboost_model"),
        ("tap.model_ranker.KEYBOARD-13421", "KEYBOARD-13421_tap_model_ranker", None),
        ("tap.model_classifier.KEYBOARD-13421", "KEYBOARD-13421_tap_model_classifier", None),
    ]
    for key, name, fmt in raw_models:
        append_raw_model_if_exists(assets, key, name, fmt)

    # Direct TFLite aliases extracted from the same Yandex assets.  These are
    # the practical inputs for future TFLite/MindSpore runner parity work.
    tflite_models = [
        ("neural.word_model.tflite", "neural_model"),
        ("neural.subword_nnlm.KEYBOARD-13321.tflite", "KEYBOARD-13321_nnlm_model"),
        ("neural.char_model.tflite", "char_model"),
        ("rnnt.model.tflite", "rnnt_model"),
        ("ranking.catboost.tflite", "ranker"),
        ("ranking.swipe.tflite", "ranker_swipe"),
        ("ranking.swipe_blocker.tflite", "swipe_blocker"),
        ("autocc.tree_model.tflite", "tree_autocorrect_model"),
        ("tap.model_ranker.tflite", "tap_model_ranker"),
        ("tap.model_classifier.tflite", "tap_model_classifier"),
        ("emoji.search_model.tflite", "search_emoji_model"),
        ("emoji.suggest_model.tflite", "emoji_model"),
        ("emoji.suggest_model.KEYBOARD-13321.tflite", "KEYBOARD-13321_emoji_suggest"),
        ("autocc.tree_model.KEYBOARD-13321.tflite", "KEYBOARD-13321_autocc"),
        ("ranking.catboost.KEYBOARD-13321.tflite", "KEYBOARD-13321_ranker"),
        ("ranking.swipe.KEYBOARD-13321.tflite", "KEYBOARD-13321_ranker_swipe"),
        ("rnnt.model.KEYBOARD-13321.tflite", "KEYBOARD-13321_rnnt_model"),
        ("tap.model_ranker.KEYBOARD-13321.tflite", "KEYBOARD-13321_tap_model_ranker"),
        ("tap.model_classifier.KEYBOARD-13321.tflite", "KEYBOARD-13321_tap_model_classifier"),
        ("autocc.tree_model.KEYBOARD-13421.tflite", "KEYBOARD-13421_autocc"),
        ("ranking.catboost.KEYBOARD-13421.tflite", "KEYBOARD-13421_ranker"),
        ("ranking.swipe.KEYBOARD-13421.tflite", "KEYBOARD-13421_ranker_swipe"),
        ("tap.model_ranker.KEYBOARD-13421.tflite", "KEYBOARD-13421_tap_model_ranker"),
        ("tap.model_classifier.KEYBOARD-13421.tflite", "KEYBOARD-13421_tap_model_classifier"),
    ]
    for key, name in tflite_models:
        append_tflite_model_if_exists(assets, key, name)


def build_asset_list(include_full_models=False, include_predictor_fallback=False):
    """Build list of (key, format, filepath) for all available assets."""
    assets = []

    # Core assets from yandex_extracted.
    core = [
        ("config", "feature_json", "config"),
        ("lexicon.default", "succinct_lexicon_v1", "trie"),
        (
            "lexicon.experiments.KEYBOARD-13421_v2",
            "succinct_lexicon_k13421",
            "KEYBOARD-13421_trie",
        ),
        ("rules.transfeme", "tfmtrie_v1", "rules.bin"),
        (
            "rules.transfeme_experiments.KEYBOARD-13421_v2",
            "tfmtrie_k13421",
            "KEYBOARD-13421_rules.bin",
        ),
        ("normdict.default", "normdict_v2", "normdict"),
        # Raw Yandex ynnlm_v1 bundle.  This is the original neural model
        # wrapper, not the debug predictor fallback; the engine parses its
        # embedded UTF-8 vocabulary as an exact/vocab bridge when MindSpore
        # Lite inference is unavailable locally.
        ("neural.word_model.raw", "ynnlm_v1_bundle", "neural_model"),
        ("lemma.hash", "chd_hash", "lemmer_mhash"),
        ("lemma.rules", "lemma_rules_program", "lemmer_rules"),
        ("policy.blacklist", "policy_blob", "blacklist"),
        ("policy.blacklist_suggest", "policy_blob", "blacklist_suggest"),
        ("policy.autocorrect_blocker", "policy_blob", "autocorrect_blocker"),
        ("policy.abbr", "policy_blob", "abbr"),
        ("features.autocc", "feature_json", "features_autocc.json"),
        ("features.ranker", "feature_json", "features_ranker.json"),
        ("features.tap_model", "feature_json", "features_tap_model.json"),
    ]
    for key, fmt, name in core:
        path = find_file(
            os.path.join(YANDEX_DIR, name),
            os.path.join(DICT_OUTPUT, name),
        )
        if path:
            assets.append(AssetSpec(key, fmt, path, os.path.basename(path)))

    # Optional local/debug fallback. Disabled by default because the target
    # parity path is Yandex-like: 1nc7 states + neural/ranker models, not a
    # separate word-level predictor trie.
    if include_predictor_fallback:
        if os.path.exists(WEIGHTED_WORDS):
            assets.append(
                AssetSpec(
                    "lexicon.predictor",
                    "predictor_trie_v1",
                    build_predictor_trie_blob(WEIGHTED_WORDS),
                    os.path.basename(WEIGHTED_WORDS),
                )
            )
        else:
            append_asset_if_exists(
                assets,
                "lexicon.predictor",
                "predictor_trie_v1",
                PREDICTOR_BINARY,
            )

    # MindSpore models.
    ms_models = [
        ("ranking.ranker", "mindspore_model", "ranker.ms"),
        ("ranking.neural_model", "mindspore_model", "neural_model.ms"),
        ("ranking.tap_model_ranker", "mindspore_model", "tap_model_ranker.ms"),
        ("ranking.tap_model_classifier", "mindspore_model", "tap_model_classifier.ms"),
        ("autocc.tree_model", "mindspore_model", "tree_autocorrect_model.ms"),
    ]
    for key, fmt, name in ms_models:
        path = find_file(os.path.join(MS_MODELS_DIR, name))
        if path:
            assets.append(AssetSpec(key, fmt, path, os.path.basename(path)))

    # Neural word-suggestion chain: the reverse-engineered Yandex word LM as a
    # runnable composition. The RWKV encoder was int8-quantized in the original
    # bundle (its MEAN/layer-norm op is unsupported by NNACL), so it ships here
    # as float weights driven by the pure-C++ RwkvEncoder; the emb, enc_dec
    # bridge and word decoder are dequantized/converted .ms that build on the
    # device OH_AI runtime. Chain: chars -> emb -> encoder -> enc_dec -> dec.
    neural_word = [
        ("neural.word.emb", "mindspore_model", "word_emb.ms"),
        ("neural.word.enc_dec", "mindspore_model", "word_enc_dec.ms"),
        ("neural.word.dec", "mindspore_model", "word_dec.ms"),
        ("neural.word.encoder", "raw_model_blob", "word_encoder.bin"),
        # Reverse-engineered Yandex MatrixNet oblivious-tree ranker (HRNK), built
        # by tools/build_ranker_model.py from KEYBOARD-13421_ranker. Pure-C++
        # evaluator (MLRanker::LoadMatrixNet) — takes ranking priority on-device.
        ("ranking.matrixnet", "raw_model_blob", "ranker_model.bin"),
    ]
    for key, fmt, name in neural_word:
        path = find_file(os.path.join(MS_MODELS_DIR, name))
        if path:
            assets.append(AssetSpec(key, fmt, path, os.path.basename(path)))

    if include_full_models:
        append_full_model_assets(assets)

    return assets


def build_container(output_path, include_full_models=False,
                    include_predictor_fallback=False):
    assets = build_asset_list(
        include_full_models=include_full_models,
        include_predictor_fallback=include_predictor_fallback,
    )

    # Read all asset data.
    entries = []
    for asset in assets:
        key, fmt = asset.key, asset.fmt
        if isinstance(asset.source, (bytes, bytearray)):
            data = bytes(asset.source)
        else:
            with open(asset.source, "rb") as f:
                data = f.read()
        entries.append((key, fmt, data))
        size_mb = len(data) / 1024 / 1024
        label = f"{size_mb:.1f}MB" if size_mb > 1 else f"{len(data):,}B"
        print(f"  PACK {key}: {asset.label} ({label})")

    if not entries:
        print("ERROR: no assets found")
        sys.exit(1)

    manifest_bytes = build_manifest()
    asset_count = len(entries)

    # Calculate index size.
    index_size = 0
    for key, fmt, data in entries:
        index_size += 4 + len(key.encode()) + 4 + len(fmt.encode()) + 16

    header_size = 16
    data_start = align(header_size + len(manifest_bytes) + index_size)

    # Calculate offsets.
    cur = data_start
    offsets = []
    for _, _, data in entries:
        offsets.append(cur)
        cur = align(cur + len(data))

    # Build binary.
    buf = bytearray()
    buf += struct.pack("<I", HSKD_MAGIC)
    buf += struct.pack("<I", HSKD_VERSION)
    buf += struct.pack("<I", asset_count)
    buf += struct.pack("<I", len(manifest_bytes))
    buf += manifest_bytes

    for i, (key, fmt, data) in enumerate(entries):
        kb = key.encode()
        fb = fmt.encode()
        buf += struct.pack("<I", len(kb)) + kb
        buf += struct.pack("<I", len(fb)) + fb
        buf += struct.pack("<Q", offsets[i])
        buf += struct.pack("<Q", len(data))

    while len(buf) < data_start:
        buf += b"\x00"

    for i, (key, fmt, data) in enumerate(entries):
        assert len(buf) == offsets[i]
        buf += data
        padded = align(len(buf))
        while len(buf) < padded:
            buf += b"\x00"

    os.makedirs(os.path.dirname(output_path) or ".", exist_ok=True)
    with open(output_path, "wb") as f:
        f.write(buf)

    size_mb = len(buf) / 1024 / 1024
    print(f"\nContainer: {output_path}")
    print(f"  Assets: {asset_count}")
    print(f"  Size: {len(buf):,} bytes ({size_mb:.1f} MB)")


def main():
    default_output = os.path.join(DICT_OUTPUT, "ru.hoskey.hosdict")
    parser = argparse.ArgumentParser(description="Build HSKD container")
    parser.add_argument("--output", default=default_output)
    parser.add_argument(
        "--full-models",
        action="store_true",
        default=env_flag("HOSKEY_PACK_FULL_MODELS"),
        help="pack all reverse-engineered Yandex neural/ranker/tap/autocc model blobs",
    )
    parser.add_argument(
        "--predictor-fallback",
        dest="predictor_fallback",
        action="store_true",
        default=env_flag("HOSKEY_PACK_PREDICTOR_FALLBACK"),
        help="pack local binary word predictor fallback (debug only, not Yandex parity path)",
    )
    args = parser.parse_args()

    print("Building HOSKEY container v2...")
    if args.full_models:
        print("  full model pack: enabled")
    if args.predictor_fallback:
        print("  predictor fallback: enabled (debug)")
    build_container(
        args.output,
        include_full_models=args.full_models,
        include_predictor_fallback=args.predictor_fallback,
    )
    print("Done.")


if __name__ == "__main__":
    main()
