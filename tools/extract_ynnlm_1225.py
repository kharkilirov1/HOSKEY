#!/usr/bin/env python3
"""Extract and pin the Default YNNLM from Yandex Keyboard 122.5 dict 67.0."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_SOURCE = ROOT / "build/reverse/main_ru_1225"
DEFAULT_OUTPUT = ROOT / "build/reverse/yandex1225_neural"

SOURCE_SIZE = 127_930_320
SOURCE_SHA256 = "70e92da9b8a7c6f7df0b69c4665c363a61f305c68d1164e962f77f5bfad8018f"
WRAPPER_OFFSET = 17_579_904
WRAPPER_SIZE = 44_770_560
WRAPPER_SHA256 = "10d8e63f951b61b6084e13374d8f8b4ab0ae030600dfe0a7a4b11f55b761e80e"

# The ten-model layout is the material difference from the stale eight-model
# wrapper: age/app/sex embeddings occupy m3/m4/m5 and shift the remaining
# components to m6..m9.
EXPECTED_MODELS = (
    (0, 3_958_240, 2_393_696, "c3b8d59cf1dee140336b03cdb0d053b2ab2dfa2d126ff741923590a4a1323b34"),
    (1, 6_351_936, 9_008_816, "8b9de6156187ac1404b1c6b0e9659d1796dbaed19b6642b5e74ef963f464d9fd"),
    (2, 15_360_752, 20_544_352, "5d7b1bf65549eb05d4943c25edf9a232d0de98b120dbd5d27c849489cb117d5d"),
    (3, 35_905_104, 39_424, "702b16cecf020163fdec42314d4bb6e1b3d9b3a352a3925c890c3ec2859069dd"),
    (4, 35_944_528, 101_968, "203219817016e66eb6af62303345d68366526cc785fd763d715ab2400e3a7fb9"),
    (5, 36_046_496, 38_400, "6e58713931dabe2e847f953a57355335064a5c9caf7accc2135780b42d1690c9"),
    (6, 36_084_896, 1_177_856, "86a4b118ea9db8c67c73570848fe9af12a95c86e93a30bc0405edaf35d8b5bfa"),
    (7, 37_262_752, 261_456, "fa455892616413a2a09b7d1af55e61d25a55870c4d4a49569e39c38e58cd9cfe"),
    (8, 37_524_208, 382_368, "5873a17746727b80812036c76aaf5bd841cfe61a243429b7a8a6a9ebae9a8fce"),
    (9, 37_906_576, 6_863_984, "04fbb0ddbec844447b9a6cab9b046eae9012e75cefb5f4e0d33c7d3761bde369"),
)


def digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def checked_source(path: Path) -> bytes:
    data = path.read_bytes()
    if len(data) != SOURCE_SIZE or digest(data) != SOURCE_SHA256:
        raise ValueError(f"unexpected Yandex dictionary identity: {path}")
    return data


def extract(source: Path, output: Path) -> dict[str, object]:
    data = checked_source(source)
    wrapper = data[WRAPPER_OFFSET : WRAPPER_OFFSET + WRAPPER_SIZE]
    if len(wrapper) != WRAPPER_SIZE or digest(wrapper) != WRAPPER_SHA256:
        raise ValueError("YNNLM 122.5 wrapper identity mismatch")
    if not wrapper.startswith(b"ynnlm_v1"):
        raise ValueError("YNNLM wrapper magic mismatch")

    starts = [match.start() - 4 for match in re.finditer(b"TFL3", wrapper)]
    expected_starts = [entry[1] for entry in EXPECTED_MODELS]
    if starts != expected_starts:
        raise ValueError(f"YNNLM component offsets mismatch: {starts}")

    config_end = wrapper.find(b"\0", 16)
    config = json.loads(wrapper[16:config_end].decode("utf-8"))
    if config.get("Order") != 10 or config.get("CnnEmbedding", {}).get(
        "MaxWordLen"
    ) != 24:
        raise ValueError("YNNLM config contract mismatch")

    output.mkdir(parents=True, exist_ok=True)
    split = output / "split"
    split.mkdir(parents=True, exist_ok=True)
    (output / "neural_model_1225.bin").write_bytes(wrapper)
    (split / "layout.txt").write_text("live_1225_10\n", encoding="utf-8")

    models: list[dict[str, object]] = []
    for index, offset, size, expected_sha256 in EXPECTED_MODELS:
        blob = wrapper[offset : offset + size]
        actual_sha256 = digest(blob)
        if len(blob) != size or actual_sha256 != expected_sha256:
            raise ValueError(f"YNNLM m{index} identity mismatch")
        if blob[4:8] != b"TFL3":
            raise ValueError(f"YNNLM m{index} TFL3 identifier missing")
        (split / f"ynnlm_m{index}.tflite").write_bytes(blob)
        models.append(
            {
                "index": index,
                "offset": offset,
                "size": size,
                "sha256": actual_sha256,
            }
        )

    provenance: dict[str, object] = {
        "format": "hoskey.ynnlm1225/v1",
        "package_version": "122.5",
        "package_version_code": 31_220_500,
        "dictionary_version": "67.0",
        "source": str(source),
        "source_size": len(data),
        "source_sha256": SOURCE_SHA256,
        "wrapper_offset": WRAPPER_OFFSET,
        "wrapper_size": len(wrapper),
        "wrapper_sha256": WRAPPER_SHA256,
        "layout": "live_1225_10",
        "config": config,
        "models": models,
    }
    (output / "provenance.json").write_text(
        json.dumps(provenance, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8",
    )
    return provenance


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", type=Path, default=DEFAULT_SOURCE)
    parser.add_argument("--output", type=Path, default=DEFAULT_OUTPUT)
    args = parser.parse_args()
    result = extract(args.source, args.output)
    print(
        "YNNLM 122.5 EXTRACT OK: "
        f"{result['wrapper_size']} bytes, {len(result['models'])} models, "
        f"sha256={result['wrapper_sha256']}"
    )


if __name__ == "__main__":
    main()
