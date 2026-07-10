#!/usr/bin/env python3
from __future__ import annotations

import os
import subprocess
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SRC_DIR = ROOT / "entry" / "src" / "main" / "cpp"
ENGINE_DIR = SRC_DIR / "hosdict_engine"
DIAG_SRC = ROOT / "tools" / "keyboard_engine_diag.cpp"
DIAG_EXE = ROOT / "tools" / "keyboard_engine_diag.exe"
COMPARE_SCRIPT = ROOT / "tools" / "compare_keyboard_engine_diag.py"
BASELINE_REPORT = ROOT / "reverse" / "R3_keyboard_engine_diag_2026-04-10.txt"
PACKAGE_DIR = ROOT / "entry" / "src" / "main" / "resources" / "rawfile" / "dict"
SWEEP_REPORT = ROOT / "reverse" / "R3_split_overlay_weight_sweep_2026-04-10.txt"

SOURCES = [
    DIAG_SRC,
    ENGINE_DIR / "asset_manifest.cpp",
    ENGINE_DIR / "loaders.cpp",
    ENGINE_DIR / "hosdict_container.cpp",
    ENGINE_DIR / "hosdict_package.cpp",
    ENGINE_DIR / "keyboard_engine.cpp",
    ENGINE_DIR / "predictor_engine.cpp",
    ENGINE_DIR / "text_utils.cpp",
    ENGINE_DIR / "policy_store.cpp",
    ENGINE_DIR / "normdict_reader.cpp",
    ENGINE_DIR / "lemmer.cpp",
    ENGINE_DIR / "tfmtrie_decoder.cpp",
    ENGINE_DIR / "ml_ranker.cpp",
    ENGINE_DIR / "neural_lm.cpp",
    ENGINE_DIR / "succinct_trie.cpp",
]

COMBOS = [
    (0, 8, 4),
    (0, 8, 3),
    (0, 8, 2),
    (0, 6, 4),
    (0, 6, 3),
    (0, 6, 2),
    (0, 4, 2),
]


def run(cmd: list[str], *, env: dict[str, str] | None = None) -> None:
    subprocess.run(cmd, cwd=ROOT, env=env, check=True)


def build_diag() -> None:
    cmd = [
        "g++",
        "-std=c++20",
        "-I",
        str(SRC_DIR),
        "-I",
        str(ENGINE_DIR),
        *[str(src) for src in SOURCES],
        "-o",
        str(DIAG_EXE),
    ]
    run(cmd)


def run_diag(report: Path, *, env_extra: dict[str, str] | None = None) -> None:
    env = os.environ.copy()
    for key in [
        "HOSKEY_EXPERIMENTAL_PREDICTOR_OVERLAY",
        "HOSKEY_EXPERIMENTAL_PREDICTOR_OVERLAY_MODE",
        "HOSKEY_EXPERIMENTAL_PREDICTOR_SPLIT_PREFIX_WEIGHT",
        "HOSKEY_EXPERIMENTAL_PREDICTOR_SPLIT_DIRECT_WEIGHT",
        "HOSKEY_EXPERIMENTAL_PREDICTOR_SPLIT_HANDOFF_WEIGHT",
    ]:
        env.pop(key, None)
    if env_extra:
        env.update(env_extra)
    run([str(DIAG_EXE), str(PACKAGE_DIR), str(report)], env=env)


def parse_summary(path: Path) -> dict[str, str]:
    out: dict[str, str] = {}
    for line in path.read_text(encoding="utf-8").splitlines():
        if "=" in line and not line.startswith(("baseline=", "overlay=")):
            key, value = line.split("=", 1)
            out[key.strip()] = value.strip()
    return out


def main() -> None:
    build_diag()
    run_diag(BASELINE_REPORT)

    rows: list[dict[str, str]] = []
    try:
        for prefix_w, direct_w, handoff_w in COMBOS:
            tag = f"p{prefix_w}_d{direct_w}_h{handoff_w}"
            overlay_report = ROOT / "reverse" / f"tmp_diag_{tag}.txt"
            compare_report = ROOT / "reverse" / f"tmp_compare_{tag}.txt"
            env = {
                "HOSKEY_EXPERIMENTAL_PREDICTOR_OVERLAY": "1",
                "HOSKEY_EXPERIMENTAL_PREDICTOR_OVERLAY_MODE": "split",
                "HOSKEY_EXPERIMENTAL_PREDICTOR_SPLIT_PREFIX_WEIGHT": str(prefix_w),
                "HOSKEY_EXPERIMENTAL_PREDICTOR_SPLIT_DIRECT_WEIGHT": str(direct_w),
                "HOSKEY_EXPERIMENTAL_PREDICTOR_SPLIT_HANDOFF_WEIGHT": str(handoff_w),
            }
            run_diag(overlay_report, env_extra=env)
            run(
                [
                    "python",
                    "-X",
                    "utf8",
                    str(COMPARE_SCRIPT),
                    "--baseline",
                    str(BASELINE_REPORT),
                    "--overlay",
                    str(overlay_report),
                    "--output",
                    str(compare_report),
                    "--top",
                    "10",
                ]
            )
            summary = parse_summary(compare_report)
            summary["tag"] = tag
            summary["prefix_weight"] = str(prefix_w)
            summary["direct_weight"] = str(direct_w)
            summary["handoff_weight"] = str(handoff_w)
            rows.append(summary)

        def sort_key(item: dict[str, str]) -> tuple[int, int, int, int]:
            return (
                -int(item.get("overlay_split_cases", "0")),
                int(item.get("changed_non_split_cases", "0")),
                int(item.get("top1_changed_cases", "0")),
                int(item.get("changed_suggestion_cases", "0")),
            )

        rows.sort(key=sort_key)

        lines = [
            "Split-only overlay weight sweep",
            f"baseline={BASELINE_REPORT}",
            "",
        ]
        for item in rows:
            lines.extend(
                [
                    f"tag={item['tag']}",
                    f"weights=prefix:{item['prefix_weight']} direct:{item['direct_weight']} handoff:{item['handoff_weight']}",
                    f"overlay_split_cases={item.get('overlay_split_cases', '?')}",
                    f"changed_suggestion_cases={item.get('changed_suggestion_cases', '?')}",
                    f"changed_non_split_cases={item.get('changed_non_split_cases', '?')}",
                    f"top1_changed_cases={item.get('top1_changed_cases', '?')}",
                    f"overlay_promoted_split_prefixes={item.get('overlay_promoted_split_prefixes', '-')}",
                    "",
                ]
            )

        SWEEP_REPORT.write_text("\n".join(lines), encoding="utf-8", newline="\n")
        print(SWEEP_REPORT)
    finally:
        if DIAG_EXE.exists():
            DIAG_EXE.unlink()


if __name__ == "__main__":
    main()
