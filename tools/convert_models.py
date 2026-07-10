#!/usr/bin/env python3
"""
convert_models.py — TFLite model converter and packager for HOSKEY.

Lists available .tflite models, inspects their structure,
optionally converts to MindSpore .ms format, copies into rawfile/dict/models/,
and generates a JSON manifest.

Usage:
    python convert_models.py --src <models_dir> --dst <rawfile_models_dir>
    python convert_models.py --src /c/Users/Kharki/clawd/yandex_extracted/yandex_extracted \
                             --dst /c/Users/Kharki/DevEcoStudioProjects/HOSKEY/entry/src/main/resources/rawfile/dict/models
"""

import argparse
import json
import os
import shutil
import sys
from pathlib import Path

# ── TFLite inspection ────────────────────────────────────────────────────────

def inspect_tflite(path: Path) -> dict:
    """Read a TFLite model and return metadata about its tensors and ops."""
    try:
        import tensorflow as tf  # type: ignore
    except ImportError:
        try:
            import tflite_runtime.interpreter as tflite  # type: ignore
            return _inspect_with_tflite_runtime(path, tflite)
        except ImportError:
            return _inspect_raw(path)

    interpreter = tf.lite.Interpreter(model_path=str(path))
    interpreter.allocate_tensors()

    input_details = interpreter.get_input_details()
    output_details = interpreter.get_output_details()

    inputs = []
    for inp in input_details:
        inputs.append({
            "name": inp["name"],
            "shape": inp["shape"].tolist(),
            "dtype": str(inp["dtype"]),
            "quantization": inp.get("quantization_parameters", {}),
        })

    outputs = []
    for out in output_details:
        outputs.append({
            "name": out["name"],
            "shape": out["shape"].tolist(),
            "dtype": str(out["dtype"]),
            "quantization": out.get("quantization_parameters", {}),
        })

    # Operator info via experimental details.
    ops = set()
    try:
        tensor_details = interpreter.get_tensor_details()
        # TF 2.x exposes _get_ops_details in some builds.
        if hasattr(interpreter, "_get_ops_details"):
            for op in interpreter._get_ops_details():
                ops.add(op.get("op_name", "unknown"))
    except Exception:
        pass

    return {
        "inputs": inputs,
        "outputs": outputs,
        "ops": sorted(ops) if ops else ["(use analyze_models.py for op details)"],
    }


def _inspect_with_tflite_runtime(path: Path, tflite) -> dict:
    """Inspect using tflite_runtime (lighter package)."""
    interpreter = tflite.Interpreter(model_path=str(path))
    interpreter.allocate_tensors()

    inputs = []
    for inp in interpreter.get_input_details():
        inputs.append({
            "name": inp["name"],
            "shape": inp["shape"].tolist(),
            "dtype": str(inp["dtype"]),
        })

    outputs = []
    for out in interpreter.get_output_details():
        outputs.append({
            "name": out["name"],
            "shape": out["shape"].tolist(),
            "dtype": str(out["dtype"]),
        })

    return {"inputs": inputs, "outputs": outputs, "ops": []}


def _inspect_raw(path: Path) -> dict:
    """Minimal inspection without TensorFlow — just file metadata."""
    size = path.stat().st_size
    # Check TFLite magic.
    with open(path, "rb") as f:
        header = f.read(8)
    is_tflite = len(header) >= 8 and header[4:8] == b"TFL3"
    return {
        "inputs": [],
        "outputs": [],
        "ops": [],
        "note": "tensorflow/tflite_runtime not installed; raw inspection only",
        "is_tflite": is_tflite,
    }


# ── MindSpore conversion ────────────────────────────────────────────────────

def try_convert_to_ms(src_path: Path, dst_path: Path) -> bool:
    """Attempt conversion from TFLite to MindSpore .ms format."""
    try:
        import mindspore_lite as mslite  # type: ignore
    except ImportError:
        return False

    try:
        converter = mslite.Converter()
        converter.convert(
            fmk_type=mslite.FmkType.kFmkTypeTflite,
            model_file=str(src_path),
            output_file=str(dst_path),
            weight_fp16=True,
        )
        return True
    except Exception as e:
        print(f"  [WARN] MindSpore conversion failed for {src_path.name}: {e}",
              file=sys.stderr)
        return False


# ── Main ─────────────────────────────────────────────────────────────────────

# Canonical model names we care about (maps filename substring -> model key).
KNOWN_MODELS = {
    "ranker_swipe": "ranker_swipe",
    "ranker": "ranker",
    "tree_autocorrect_model": "autocorrect",
    "autocc": "autocorrect",
    "tap_model_ranker": "tap_model_ranker",
    "tap_model_classifier": "tap_model_classifier",
    "char_model": "char_lm",
    "neural_model": "neural_lm",
    "nnlm_model": "neural_lm",
    "emoji_model": "emoji",
    "emoji_suggest": "emoji",
    "search_emoji_model": "emoji_search",
    "swipe_blocker": "swipe_blocker",
}


def classify_model(filename: str) -> str:
    """Return a canonical key for a model filename, or the filename stem."""
    name = filename.lower()
    # Check known substrings (longer first to avoid prefix collisions).
    for substr in sorted(KNOWN_MODELS.keys(), key=len, reverse=True):
        if substr in name:
            return KNOWN_MODELS[substr]
    return Path(filename).stem


def main():
    parser = argparse.ArgumentParser(description="HOSKEY TFLite model converter")
    parser.add_argument("--src", type=Path, required=True,
                        help="Source directory with .tflite models")
    parser.add_argument("--dst", type=Path, default=None,
                        help="Destination rawfile/dict/models/ directory")
    parser.add_argument("--convert-ms", action="store_true",
                        help="Attempt MindSpore .ms conversion")
    parser.add_argument("--manifest", type=Path, default=None,
                        help="Output manifest JSON path (default: <dst>/manifest.json)")
    args = parser.parse_args()

    src_dir: Path = args.src
    if not src_dir.is_dir():
        print(f"ERROR: source directory does not exist: {src_dir}", file=sys.stderr)
        sys.exit(1)

    tflite_files = sorted(src_dir.glob("*.tflite"))
    if not tflite_files:
        print(f"No .tflite files found in {src_dir}", file=sys.stderr)
        sys.exit(1)

    print(f"Found {len(tflite_files)} TFLite model(s) in {src_dir}\n")

    manifest = {"models": [], "source": str(src_dir), "format": "tflite"}

    for model_path in tflite_files:
        size_mb = model_path.stat().st_size / (1024 * 1024)
        model_key = classify_model(model_path.name)
        print(f"  {model_path.name}  ({size_mb:.2f} MB)  -> key: {model_key}")

        info = inspect_tflite(model_path)
        if info.get("inputs"):
            for inp in info["inputs"]:
                print(f"    input:  {inp['name']}  shape={inp['shape']}  dtype={inp['dtype']}")
        if info.get("outputs"):
            for out in info["outputs"]:
                print(f"    output: {out['name']}  shape={out['shape']}  dtype={out['dtype']}")
        if info.get("ops"):
            print(f"    ops: {', '.join(info['ops'])}")

        entry = {
            "key": model_key,
            "filename": model_path.name,
            "size_bytes": model_path.stat().st_size,
            "inputs": info.get("inputs", []),
            "outputs": info.get("outputs", []),
            "ops": info.get("ops", []),
        }

        # Copy to destination.
        if args.dst:
            args.dst.mkdir(parents=True, exist_ok=True)
            dst_file = args.dst / model_path.name
            shutil.copy2(model_path, dst_file)
            entry["bundled_path"] = f"rawfile/dict/models/{model_path.name}"
            print(f"    -> copied to {dst_file}")

            # Optionally convert to .ms
            if args.convert_ms:
                ms_path = args.dst / (model_path.stem + ".ms")
                if try_convert_to_ms(model_path, ms_path):
                    entry["ms_path"] = f"rawfile/dict/models/{model_path.stem}.ms"
                    print(f"    -> converted to {ms_path}")

        manifest["models"].append(entry)
        print()

    # Write manifest.
    manifest_path = args.manifest
    if manifest_path is None and args.dst:
        manifest_path = args.dst / "manifest.json"
    if manifest_path is None:
        manifest_path = src_dir / "manifest.json"

    manifest_path.parent.mkdir(parents=True, exist_ok=True)
    with open(manifest_path, "w", encoding="utf-8") as f:
        json.dump(manifest, f, indent=2, ensure_ascii=False)
    print(f"Manifest written to {manifest_path}")
    print(f"Total models: {len(manifest['models'])}")


if __name__ == "__main__":
    main()
