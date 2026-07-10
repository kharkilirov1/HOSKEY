#!/usr/bin/env python3
"""
analyze_models.py — Deep analysis of TFLite models for HOSKEY reverse engineering.

For each model outputs: name, size, input/output tensor details,
parameter count, operator types, quantization status.

Usage:
    python analyze_models.py --src <models_dir> [--output <report.md>]
    python analyze_models.py \
        --src /c/Users/Kharki/clawd/yandex_extracted/yandex_extracted \
        --output /c/Users/Kharki/DevEcoStudioProjects/HOSKEY/reverse/MODEL_ANALYSIS.md
"""

import argparse
import struct
import sys
from pathlib import Path

# ── FlatBuffer TFLite schema parsing (no dependency) ────────────────────────
#
# TFLite uses FlatBuffers. We parse the raw binary to extract model info
# without requiring the tensorflow package. Falls back to tf if available.

# TFLite FlatBuffer schema constants.
# Tensor types.
TFLITE_TENSOR_TYPES = {
    0: "FLOAT32", 1: "FLOAT16", 2: "INT32", 3: "UINT8",
    4: "INT64", 5: "STRING", 6: "BOOL", 7: "INT16",
    8: "COMPLEX64", 9: "INT8", 10: "FLOAT64", 11: "COMPLEX128",
    12: "UINT64", 13: "RESOURCE", 14: "VARIANT", 15: "UINT32",
    16: "UINT16", 17: "INT4",
}

# Builtin operator codes (subset of commonly seen ops).
TFLITE_BUILTINS = {
    0: "ADD", 1: "AVERAGE_POOL_2D", 2: "CONCATENATION", 3: "CONV_2D",
    4: "DEPTHWISE_CONV_2D", 5: "DEPTH_TO_SPACE", 6: "DEQUANTIZE",
    7: "EMBEDDING_LOOKUP", 8: "FLOOR", 9: "FULLY_CONNECTED",
    10: "HASHTABLE_LOOKUP", 11: "L2_NORMALIZATION", 12: "L2_POOL_2D",
    13: "LOCAL_RESPONSE_NORMALIZATION", 14: "LOGISTIC", 15: "LSH_PROJECTION",
    16: "LSTM", 17: "MAX_POOL_2D", 18: "MUL", 19: "RELU",
    20: "RELU_N1_TO_1", 21: "RELU6", 22: "RESHAPE", 23: "RESIZE_BILINEAR",
    25: "SOFTMAX", 26: "SPACE_TO_DEPTH", 27: "SVDF",
    28: "TANH", 29: "CONCAT_EMBEDDINGS", 30: "SKIP_GRAM",
    31: "CALL", 32: "CUSTOM", 34: "UNIDIRECTIONAL_SEQUENCE_LSTM",
    37: "GATHER", 39: "UNIDIRECTIONAL_SEQUENCE_RNN",
    40: "EXP", 41: "TOPK_V2", 42: "SPLIT", 43: "LOG_SOFTMAX",
    44: "DELEGATE", 46: "CAST", 47: "PRELU",
    48: "MAXIMUM", 49: "ARG_MAX", 50: "MINIMUM", 51: "LESS",
    52: "NEG", 53: "PADV2", 54: "GREATER", 55: "GREATER_EQUAL",
    56: "LESS_EQUAL", 57: "SELECT", 58: "SLICE", 59: "SIN",
    60: "TRANSPOSE_CONV", 61: "SPARSE_TO_DENSE",
    62: "TILE", 63: "EXPAND_DIMS", 64: "EQUAL", 65: "NOT_EQUAL",
    66: "LOG", 67: "SUM", 68: "SQRT", 69: "RSQRT",
    70: "SHAPE", 71: "POW", 72: "ARG_MIN", 73: "FAKE_QUANT",
    74: "REDUCE_PROD", 75: "REDUCE_MAX", 76: "PACK", 77: "LOGICAL_OR",
    78: "ONE_HOT", 79: "LOGICAL_AND", 80: "LOGICAL_NOT",
    81: "UNPACK", 82: "REDUCE_MIN", 83: "FLOOR_DIV", 84: "REDUCE_ANY",
    85: "SQUARE", 86: "ZEROS_LIKE", 87: "FILL", 88: "FLOOR_MOD",
    89: "RANGE", 90: "RESIZE_NEAREST_NEIGHBOR", 91: "LEAKY_RELU",
    92: "SQUARED_DIFFERENCE", 93: "MIRROR_PAD", 94: "ABS",
    95: "SPLIT_V", 96: "UNIQUE", 97: "CEIL",
    98: "REVERSE_V2", 99: "ADD_N", 100: "GATHER_ND", 101: "COS",
    102: "WHERE", 103: "RANK", 104: "ELU",
    105: "REVERSE_SEQUENCE", 106: "MATRIX_DIAG",
    107: "QUANTIZE", 108: "MATRIX_SET_DIAG", 109: "ROUND",
    110: "HARD_SWISH", 111: "IF", 112: "WHILE",
    113: "NON_MAX_SUPPRESSION_V4", 114: "NON_MAX_SUPPRESSION_V5",
    115: "SCATTER_ND", 116: "SELECT_V2", 117: "DENSIFY",
    118: "SEGMENT_SUM", 119: "BATCH_MATMUL", 120: "PLACEHOLDER_FOR_GREATER_OP_CODES",
    121: "CUMSUM", 122: "CALL_ONCE", 123: "BROADCAST_TO",
    124: "RFFT2D", 125: "CONV_3D", 126: "IMAG", 127: "REAL",
    128: "COMPLEX_ABS", 129: "HASHTABLE", 130: "HASHTABLE_FIND",
    131: "HASHTABLE_IMPORT", 132: "HASHTABLE_SIZE",
    133: "REDUCE_ALL", 134: "CONV_3D_TRANSPOSE",
    135: "VAR_HANDLE", 136: "READ_VARIABLE", 137: "ASSIGN_VARIABLE",
    138: "BROADCAST_ARGS", 139: "RANDOM_STANDARD_NORMAL",
    140: "BUCKETIZE", 141: "RANDOM_UNIFORM",
    142: "MULTINOMIAL", 143: "GELU", 144: "DYNAMIC_UPDATE_SLICE",
    145: "RELU_0_TO_1", 146: "UNSORTED_SEGMENT_PROD",
    147: "UNSORTED_SEGMENT_MAX", 148: "UNSORTED_SEGMENT_SUM",
    149: "ATAN2", 150: "UNSORTED_SEGMENT_MIN",
    151: "SIGN", 152: "BITCAST", 153: "BITWISE_XOR",
    154: "RIGHT_SHIFT",
}


def _read_u16(buf, off):
    return struct.unpack_from("<H", buf, off)[0]

def _read_u32(buf, off):
    return struct.unpack_from("<I", buf, off)[0]

def _read_i32(buf, off):
    return struct.unpack_from("<i", buf, off)[0]


def analyze_tflite_raw(path: Path) -> dict:
    """Parse TFLite FlatBuffer binary to extract model metadata."""
    data = path.read_bytes()
    file_size = len(data)

    result = {
        "name": path.name,
        "size_bytes": file_size,
        "size_mb": round(file_size / (1024 * 1024), 3),
        "is_tflite": False,
        "inputs": [],
        "outputs": [],
        "operators": [],
        "num_subgraphs": 0,
        "num_tensors": 0,
        "num_operators": 0,
        "num_buffers": 0,
        "estimated_params": 0,
        "quantized": False,
        "error": None,
    }

    if file_size < 8:
        result["error"] = "file too small"
        return result

    # Check magic.
    magic = data[4:8]
    if magic != b"TFL3":
        result["error"] = f"not a TFLite file (magic: {magic!r})"
        return result
    result["is_tflite"] = True

    try:
        return _parse_flatbuffer(data, result)
    except Exception as e:
        result["error"] = f"parse error: {e}"
        return result


def _parse_flatbuffer(data: bytes, result: dict) -> dict:
    """Parse the TFLite FlatBuffer structure."""
    # Root table offset.
    root_off = _read_u32(data, 0)

    # Model table.
    model_vtable_off = root_off - _read_i32(data, root_off)

    vtable_size = _read_u16(data, model_vtable_off)
    # Fields in Model vtable: version(0), operator_codes(1), subgraphs(2),
    # description(3), buffers(4), ...

    def _field_off(field_idx):
        off_in_vtable = 4 + field_idx * 2
        if off_in_vtable >= vtable_size:
            return 0
        rel = _read_u16(data, model_vtable_off + off_in_vtable)
        if rel == 0:
            return 0
        return root_off + rel

    # Operator codes.
    opcodes_field = _field_off(1)
    op_code_names = []
    if opcodes_field:
        opcodes_vec_off = opcodes_field + _read_u32(data, opcodes_field)
        num_opcodes = _read_u32(data, opcodes_vec_off)
        for i in range(num_opcodes):
            entry_off = opcodes_vec_off + 4 + i * 4
            opcode_off = entry_off + _read_u32(data, entry_off)
            # OperatorCode vtable.
            oc_vtable_off = opcode_off - _read_i32(data, opcode_off)
            oc_vtable_size = _read_u16(data, oc_vtable_off)
            # deprecated_builtin_code at field 0, custom_code at field 1,
            # version at field 2, builtin_code at field 3.

            def _oc_field(fi):
                o = 4 + fi * 2
                if o >= oc_vtable_size:
                    return 0
                r = _read_u16(data, oc_vtable_off + o)
                if r == 0:
                    return 0
                return opcode_off + r

            # Try builtin_code (field 3, int32) first, then deprecated (field 0, uint8).
            bc_off = _oc_field(3)
            if bc_off:
                builtin_code = _read_i32(data, bc_off)
            else:
                dep_off = _oc_field(0)
                if dep_off:
                    builtin_code = data[dep_off]
                else:
                    builtin_code = -1

            if builtin_code == 32:  # CUSTOM
                custom_off = _oc_field(1)
                if custom_off:
                    str_off = custom_off + _read_u32(data, custom_off)
                    str_len = _read_u32(data, str_off)
                    custom_name = data[str_off + 4: str_off + 4 + str_len].decode(
                        "utf-8", errors="replace")
                    op_code_names.append(f"CUSTOM({custom_name})")
                else:
                    op_code_names.append("CUSTOM(unknown)")
            else:
                op_code_names.append(TFLITE_BUILTINS.get(builtin_code,
                                                         f"OP_{builtin_code}"))

    result["operators"] = sorted(set(op_code_names))

    # Buffers — count and estimate parameter bytes.
    buffers_field = _field_off(4)
    total_buffer_bytes = 0
    num_buffers = 0
    if buffers_field:
        buffers_vec_off = buffers_field + _read_u32(data, buffers_field)
        num_buffers = _read_u32(data, buffers_vec_off)
        for i in range(num_buffers):
            entry_off = buffers_vec_off + 4 + i * 4
            buf_off = entry_off + _read_u32(data, entry_off)
            buf_vtable_off = buf_off - _read_i32(data, buf_off)
            buf_vtable_size = _read_u16(data, buf_vtable_off)
            # data field at index 0.
            if buf_vtable_size > 4:
                data_rel = _read_u16(data, buf_vtable_off + 4)
                if data_rel:
                    data_vec_off = (buf_off + data_rel)
                    data_vec_off = data_vec_off + _read_u32(data, data_vec_off)
                    vec_len = _read_u32(data, data_vec_off)
                    total_buffer_bytes += vec_len
    result["num_buffers"] = num_buffers

    # Subgraphs.
    subgraphs_field = _field_off(2)
    if subgraphs_field:
        sg_vec_off = subgraphs_field + _read_u32(data, subgraphs_field)
        num_subgraphs = _read_u32(data, sg_vec_off)
        result["num_subgraphs"] = num_subgraphs

        # Parse first subgraph for tensor info.
        if num_subgraphs > 0:
            sg0_off = sg_vec_off + 4 + _read_u32(data, sg_vec_off + 4)
            _parse_subgraph(data, sg0_off, result)

    # Estimate parameters from buffer bytes (assume float32 = 4 bytes/param,
    # int8 = 1 byte/param).
    if result["quantized"]:
        result["estimated_params"] = total_buffer_bytes
    else:
        result["estimated_params"] = total_buffer_bytes // 4

    return result


def _parse_subgraph(data: bytes, sg_off: int, result: dict):
    """Parse a SubGraph table to extract tensor and operator info."""
    sg_vtable_off = sg_off - _read_i32(data, sg_off)
    sg_vtable_size = _read_u16(data, sg_vtable_off)

    def _sg_field(fi):
        o = 4 + fi * 2
        if o >= sg_vtable_size:
            return 0
        r = _read_u16(data, sg_vtable_off + o)
        if r == 0:
            return 0
        return sg_off + r

    # Tensors (field 0).
    tensors_field = _sg_field(0)
    tensors = []
    if tensors_field:
        t_vec_off = tensors_field + _read_u32(data, tensors_field)
        num_tensors = _read_u32(data, t_vec_off)
        result["num_tensors"] = num_tensors
        for i in range(min(num_tensors, 500)):  # cap to avoid huge loops
            t_entry = t_vec_off + 4 + i * 4
            t_off = t_entry + _read_u32(data, t_entry)
            tinfo = _parse_tensor(data, t_off)
            tensors.append(tinfo)
            if tinfo["type"] in ("INT8", "UINT8", "INT4"):
                result["quantized"] = True

    # Inputs (field 1).
    inputs_field = _sg_field(1)
    if inputs_field:
        inp_vec_off = inputs_field + _read_u32(data, inputs_field)
        num_inputs = _read_u32(data, inp_vec_off)
        for i in range(num_inputs):
            idx = _read_i32(data, inp_vec_off + 4 + i * 4)
            if 0 <= idx < len(tensors):
                result["inputs"].append(tensors[idx])

    # Outputs (field 2).
    outputs_field = _sg_field(2)
    if outputs_field:
        out_vec_off = outputs_field + _read_u32(data, outputs_field)
        num_outputs = _read_u32(data, out_vec_off)
        for i in range(num_outputs):
            idx = _read_i32(data, out_vec_off + 4 + i * 4)
            if 0 <= idx < len(tensors):
                result["outputs"].append(tensors[idx])

    # Operators (field 3) — count only.
    ops_field = _sg_field(3)
    if ops_field:
        ops_vec_off = ops_field + _read_u32(data, ops_field)
        result["num_operators"] = _read_u32(data, ops_vec_off)


def _parse_tensor(data: bytes, t_off: int) -> dict:
    """Parse a Tensor table."""
    vt_off = t_off - _read_i32(data, t_off)
    vt_size = _read_u16(data, vt_off)

    def _t_field(fi):
        o = 4 + fi * 2
        if o >= vt_size:
            return 0
        r = _read_u16(data, vt_off + o)
        if r == 0:
            return 0
        return t_off + r

    # Shape (field 0) — vector of int32.
    shape = []
    shape_field = _t_field(0)
    if shape_field:
        s_vec_off = shape_field + _read_u32(data, shape_field)
        s_len = _read_u32(data, s_vec_off)
        for j in range(s_len):
            shape.append(_read_i32(data, s_vec_off + 4 + j * 4))

    # Type (field 1) — uint8 enum.
    dtype_field = _t_field(1)
    dtype_val = data[dtype_field] if dtype_field else 0
    dtype_name = TFLITE_TENSOR_TYPES.get(dtype_val, f"TYPE_{dtype_val}")

    # Name (field 3) — string.
    name = ""
    name_field = _t_field(3)
    if name_field:
        name_str_off = name_field + _read_u32(data, name_field)
        name_len = _read_u32(data, name_str_off)
        name = data[name_str_off + 4: name_str_off + 4 + name_len].decode(
            "utf-8", errors="replace")

    return {"name": name, "shape": shape, "type": dtype_name}


# ── Report generation ────────────────────────────────────────────────────────

def format_tensor(t: dict) -> str:
    shape_str = "x".join(str(s) for s in t["shape"]) if t["shape"] else "scalar"
    return f"`{t['name']}`  shape=[{shape_str}]  type={t['type']}"


def generate_report(analyses: list[dict]) -> str:
    lines = [
        "# HOSKEY Model Analysis Report",
        "",
        "Auto-generated analysis of TFLite models extracted from Yandex Keyboard.",
        "",
        f"Total models analyzed: {len(analyses)}",
        "",
    ]

    # Summary table.
    lines.append("## Summary")
    lines.append("")
    lines.append("| Model | Size | Params (est.) | Tensors | Operators | Quantized |")
    lines.append("|-------|------|--------------|---------|-----------|-----------|")
    for a in analyses:
        q = "Yes" if a["quantized"] else "No"
        params = f"{a['estimated_params']:,}"
        lines.append(
            f"| {a['name']} | {a['size_mb']} MB | {params} | "
            f"{a['num_tensors']} | {a['num_operators']} | {q} |"
        )
    lines.append("")

    # Detailed per-model sections.
    for a in analyses:
        lines.append(f"## {a['name']}")
        lines.append("")
        lines.append(f"- **Size:** {a['size_bytes']:,} bytes ({a['size_mb']} MB)")
        lines.append(f"- **Subgraphs:** {a['num_subgraphs']}")
        lines.append(f"- **Tensors:** {a['num_tensors']}")
        lines.append(f"- **Operators (graph nodes):** {a['num_operators']}")
        lines.append(f"- **Buffers:** {a['num_buffers']}")
        lines.append(f"- **Estimated parameters:** {a['estimated_params']:,}")
        lines.append(f"- **Quantized:** {'Yes' if a['quantized'] else 'No'}")

        if a.get("error"):
            lines.append(f"- **Parse error:** {a['error']}")

        if a["inputs"]:
            lines.append("")
            lines.append("### Inputs")
            for t in a["inputs"]:
                lines.append(f"- {format_tensor(t)}")

        if a["outputs"]:
            lines.append("")
            lines.append("### Outputs")
            for t in a["outputs"]:
                lines.append(f"- {format_tensor(t)}")

        if a["operators"]:
            lines.append("")
            lines.append("### Operator types")
            for op in a["operators"]:
                lines.append(f"- `{op}`")

        lines.append("")
        lines.append("---")
        lines.append("")

    return "\n".join(lines)


# ── Entrypoint ───────────────────────────────────────────────────────────────

def main():
    parser = argparse.ArgumentParser(description="HOSKEY TFLite model analyzer")
    parser.add_argument("--src", type=Path, required=True,
                        help="Source directory with .tflite models")
    parser.add_argument("--output", "-o", type=Path, default=None,
                        help="Output markdown report path")
    args = parser.parse_args()

    src_dir: Path = args.src
    if not src_dir.is_dir():
        print(f"ERROR: source directory does not exist: {src_dir}", file=sys.stderr)
        sys.exit(1)

    tflite_files = sorted(src_dir.glob("*.tflite"))
    if not tflite_files:
        print(f"No .tflite files found in {src_dir}", file=sys.stderr)
        sys.exit(1)

    print(f"Analyzing {len(tflite_files)} TFLite models...\n")

    analyses = []
    for model_path in tflite_files:
        print(f"  {model_path.name} ... ", end="", flush=True)
        info = analyze_tflite_raw(model_path)
        if info.get("error"):
            print(f"WARNING: {info['error']}")
        else:
            print(f"OK ({info['num_tensors']} tensors, {info['num_operators']} ops, "
                  f"{'quantized' if info['quantized'] else 'float'})")
        analyses.append(info)

    report = generate_report(analyses)

    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(report, encoding="utf-8")
        print(f"\nReport written to {args.output}")
    else:
        print("\n" + report)


if __name__ == "__main__":
    main()
