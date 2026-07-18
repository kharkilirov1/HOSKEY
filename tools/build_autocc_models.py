#!/usr/bin/env python3
"""Rebuild the recovered Yandex Autocc forest bundle deterministically.

The old package TOC points 48/104 bytes into the K13321/K13421 pointer tables.
The corrected offsets below were established from the actual model headers and
are guarded by both the whole-container and exact-slice SHA-256 values.

Yandex Keyboard 122.5 dictionary 67.0 promotes the recovered K13421 forest to
the default path.  The live package identity and non-zero inference oracle are
kept separately below so the default alias cannot silently drift back.
"""

from __future__ import annotations

import argparse
import dataclasses
import hashlib
import json
from pathlib import Path
from typing import Any

from build_oblivious_tree_model import convert_model


ROOT = Path(__file__).resolve().parents[1]
YANDEX_DIR = Path(r"C:\Users\Kharki\Desktop\yandex_extracted\extracted_sections")
OLD_MAIN_RU = Path(r"C:\Users\Kharki\clawd\main_ru")
FEATURE_SCHEMA = YANDEX_DIR / "features_autocc.json"

MAIN_RU_SIZE = 137_196_502
MAIN_RU_SHA256 = "59cb897b81a71fe9580e10ff65805c5f943e564225c66e022443ff20981335cb"
FEATURE_SCHEMA_SIZE = 4_718
FEATURE_SCHEMA_SHA256 = "d30141557cb12252718860c6e635033d4ff3233e535e37c38e65a4cfe12be832"

LIVE_PACKAGE = "ru.yandex.androidkeyboard"
LIVE_PACKAGE_VERSION = "122.5"
LIVE_PACKAGE_VERSION_CODE = 31_220_500
LIVE_DICTIONARY_VERSION = "67.0"
LIVE_MAIN_RU_SIZE = 127_930_320
LIVE_MAIN_RU_SHA256 = "70e92da9b8a7c6f7df0b69c4665c363a61f305c68d1164e962f77f5bfad8018f"
LIVE_DEFAULT_OFFSET = 13_922_928
LIVE_DEFAULT_SIZE = 1_154_672
LIVE_DEFAULT_RAW_SHA256 = "b0fedf03cc32a264e27d96f5867a82f75a7f4e60bdb71553618090073d593224"

FEATURE_COUNT = 79
TREE_COUNT = 2000
DEPTH = 6
THRESHOLD = 0.65

VARIANTS: dict[str, dict[str, Any]] = {
    "autocc_default.hobl": {
        "variant": "default",
        "source": "main_ru",
        "offset": 134_302_400,
        "size": LIVE_DEFAULT_SIZE,
        "raw_sha256": LIVE_DEFAULT_RAW_SHA256,
        "toc_correction": -104,
        "promoted_from": "KEYBOARD-13421",
    },
    "autocc_KEYBOARD-13321.hobl": {
        "variant": "KEYBOARD-13321",
        "source": "main_ru",
        "offset": 114_432_880,
        "size": 1_154_488,
        "raw_sha256": "85d7f801908d542a21fee51123c21553f304df68741123c33996f76e6c08e715",
        "toc_correction": -48,
    },
    "autocc_KEYBOARD-13421.hobl": {
        "variant": "KEYBOARD-13421",
        "source": "main_ru",
        "offset": 134_302_400,
        "size": 1_154_672,
        "raw_sha256": "b0fedf03cc32a264e27d96f5867a82f75a7f4e60bdb71553618090073d593224",
        "toc_correction": -104,
    },
}

EXPECTED_OUTPUTS: dict[str, dict[str, Any]] = {
    "autocc_default.hobl": {
        "size": 1_109_230,
        "sha256": "e8cabf55aa6871616355e774f79d34ea301dc0c49f1939711114cf1d795e48de",
    },
    "autocc_KEYBOARD-13321.hobl": {
        "size": 1_109_046,
        "sha256": "25cb613816fe209e054d234b39ac72a2e294d693928959c6172f09eded7794e9",
    },
    "autocc_KEYBOARD-13421.hobl": {
        "size": 1_109_230,
        "sha256": "e8cabf55aa6871616355e774f79d34ea301dc0c49f1939711114cf1d795e48de",
    },
}


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def checked_file(path: Path, expected_size: int, expected_sha256: str) -> None:
    if not path.is_file():
        raise FileNotFoundError(path)
    actual_size = path.stat().st_size
    if actual_size != expected_size:
        raise ValueError(
            f"source size mismatch for {path}: {actual_size} != {expected_size}"
        )
    actual_sha256 = sha256_file(path)
    if actual_sha256 != expected_sha256:
        raise ValueError(
            f"source SHA-256 mismatch for {path}: {actual_sha256}"
        )


def read_slice(path: Path, offset: int, size: int) -> bytes:
    with path.open("rb") as stream:
        stream.seek(offset)
        data = stream.read(size)
    if len(data) != size:
        raise ValueError(f"truncated model slice at {path}:{offset}+{size}")
    return data


def atomic_write(path: Path, data: bytes) -> None:
    temporary = path.with_suffix(path.suffix + ".tmp")
    temporary.write_bytes(data)
    temporary.replace(path)


def build_models(output_dir: Path) -> dict[str, Any]:
    checked_file(OLD_MAIN_RU, MAIN_RU_SIZE, MAIN_RU_SHA256)
    checked_file(FEATURE_SCHEMA, FEATURE_SCHEMA_SIZE, FEATURE_SCHEMA_SHA256)
    schema = json.loads(FEATURE_SCHEMA.read_text(encoding="utf-8"))
    actual_feature_count = (
        len(schema["Base"]["Features"])
        + len(schema["Base"]["Combos"])
        + len(schema["KeyboardSpecific"]["Features"])
    )
    if actual_feature_count != FEATURE_COUNT:
        raise ValueError(
            "Autocc schema feature count mismatch: "
            f"{actual_feature_count} != {FEATURE_COUNT}"
        )

    output_dir.mkdir(parents=True, exist_ok=True)
    metadata: dict[str, Any] = {
        "format": "HOBL/v2",
        "feature_schema": {
            "file": FEATURE_SCHEMA.name,
            "size": FEATURE_SCHEMA.stat().st_size,
            "sha256": sha256_file(FEATURE_SCHEMA),
        },
        "feature_count": FEATURE_COUNT,
        "tree_count": TREE_COUNT,
        "depth": DEPTH,
        "threshold": THRESHOLD,
        "source_container": {
            "file": str(OLD_MAIN_RU),
            "size": MAIN_RU_SIZE,
            "sha256": MAIN_RU_SHA256,
        },
        "live_default_identity": {
            "package": LIVE_PACKAGE,
            "package_version": LIVE_PACKAGE_VERSION,
            "package_version_code": LIVE_PACKAGE_VERSION_CODE,
            "dictionary_version": LIVE_DICTIONARY_VERSION,
            "source_container_size": LIVE_MAIN_RU_SIZE,
            "source_container_sha256": LIVE_MAIN_RU_SHA256,
            "source_offset": LIVE_DEFAULT_OFFSET,
            "source_size": LIVE_DEFAULT_SIZE,
            "source_sha256": LIVE_DEFAULT_RAW_SHA256,
            "output_sha256": EXPECTED_OUTPUTS["autocc_default.hobl"]["sha256"],
        },
        "models": {},
    }

    for output_name, spec in VARIANTS.items():
        raw = read_slice(OLD_MAIN_RU, spec["offset"], spec["size"])
        raw_sha256 = hashlib.sha256(raw).hexdigest()
        if raw_sha256 != spec["raw_sha256"]:
            raise ValueError(
                f"raw model SHA-256 mismatch for {output_name}: {raw_sha256}"
            )

        converted, facts = convert_model(
            raw,
            feature_count=FEATURE_COUNT,
            expected_depth=DEPTH,
            expected_tree_count=TREE_COUNT,
            output_format="hobl",
        )
        actual = {
            "size": len(converted),
            "sha256": hashlib.sha256(converted).hexdigest(),
        }
        expected = EXPECTED_OUTPUTS[output_name]
        if actual != expected:
            raise ValueError(
                f"converted model mismatch for {output_name}: {actual} != {expected}"
            )

        atomic_write(output_dir / output_name, converted)
        model_metadata = {
            "variant": spec["variant"],
            "source": spec["source"],
            "source_offset": spec["offset"],
            "source_size": spec["size"],
            "source_sha256": spec["raw_sha256"],
            "output_size": actual["size"],
            "output_sha256": actual["sha256"],
            "conversion": dataclasses.asdict(facts),
        }
        if "toc_correction" in spec:
            model_metadata["toc_correction"] = spec["toc_correction"]
        if "promoted_from" in spec:
            model_metadata["promoted_from"] = spec["promoted_from"]
        metadata["models"][output_name] = model_metadata

    provenance = (json.dumps(metadata, ensure_ascii=False, indent=2,
                             sort_keys=True) + "\n").encode("utf-8")
    atomic_write(output_dir / "autocc_models.provenance.json", provenance)
    return metadata


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--output-dir", type=Path, default=YANDEX_DIR)
    args = parser.parse_args()
    metadata = build_models(args.output_dir)
    print("AUTOCC MODEL BUILD OK")
    for name, model in metadata["models"].items():
        print(f"  {name}: {model['output_size']} bytes, {model['output_sha256']}")


if __name__ == "__main__":
    main()
