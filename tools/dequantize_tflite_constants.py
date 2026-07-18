#!/usr/bin/env python3
"""Rewrite quantized constant tensors as equivalent FLOAT32 constants.

Some MindSpore Lite converter paths accept hybrid TFLite graphs but do not
preserve their per-axis INT8 weight semantics.  Dequantizing constant weights
ahead of conversion keeps the graph mathematically equivalent while avoiding
that converter path.  Runtime/intermediate tensors are deliberately untouched.
"""

from __future__ import annotations

import argparse
from pathlib import Path

import flatbuffers
import numpy as np
from tensorflow.lite.python import schema_py_generated as schema


def _dequantize_tensor(
    model: schema.ModelT,
    tensor: schema.TensorT,
) -> bool:
    if tensor.type != schema.TensorType.INT8 or tensor.buffer <= 0:
        return False
    buffer = model.buffers[tensor.buffer]
    if buffer.data is None or len(buffer.data) == 0:
        return False
    quantization = tensor.quantization
    if quantization is None or quantization.scale is None:
        raise ValueError(f"INT8 constant {tensor.name!r} has no quantization scale")

    shape = tuple(int(value) for value in tensor.shape)
    values = np.frombuffer(bytes(buffer.data), dtype=np.int8)
    if values.size != int(np.prod(shape)):
        raise ValueError(
            f"INT8 constant {tensor.name!r} size mismatch: "
            f"shape={shape}, values={values.size}"
        )
    values = values.reshape(shape).astype(np.float32)
    scales = np.asarray(quantization.scale, dtype=np.float32)
    zero_points = np.asarray(quantization.zeroPoint, dtype=np.float32)
    if scales.size == 0:
        raise ValueError(f"INT8 constant {tensor.name!r} has an empty scale")
    if zero_points.size == 0:
        zero_points = np.zeros(scales.size, dtype=np.float32)
    if zero_points.size not in (1, scales.size):
        raise ValueError(f"INT8 constant {tensor.name!r} zero-point count mismatch")

    if scales.size == 1:
        dequantized = (values - zero_points[0]) * scales[0]
    else:
        axis = int(quantization.quantizedDimension)
        if axis < 0 or axis >= len(shape) or shape[axis] != scales.size:
            raise ValueError(
                f"INT8 constant {tensor.name!r} per-axis metadata mismatch: "
                f"shape={shape}, axis={axis}, scales={scales.size}"
            )
        broadcast_shape = [1] * len(shape)
        broadcast_shape[axis] = scales.size
        dequantized = (
            values - zero_points.reshape(broadcast_shape)
        ) * scales.reshape(broadcast_shape)

    raw = np.asarray(dequantized, dtype="<f4").tobytes()
    buffer.data = np.frombuffer(raw, dtype=np.uint8).copy()
    tensor.type = schema.TensorType.FLOAT32
    tensor.quantization = schema.QuantizationParametersT()
    return True


def rewrite(source: Path, output: Path, expected_count: int | None) -> int:
    data = source.read_bytes()
    root = schema.Model.GetRootAsModel(data, 0)
    model = schema.ModelT.InitFromObj(root)
    if len(model.subgraphs) != 1:
        raise ValueError(f"expected one subgraph, got {len(model.subgraphs)}")

    rewritten = sum(
        _dequantize_tensor(model, tensor)
        for tensor in model.subgraphs[0].tensors
    )
    if expected_count is not None and rewritten != expected_count:
        raise ValueError(
            f"expected {expected_count} quantized constants, got {rewritten}"
        )

    builder = flatbuffers.Builder(len(data) * 4)
    model_offset = model.Pack(builder)
    builder.Finish(model_offset, file_identifier=b"TFL3")
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_bytes(builder.Output())
    return rewritten


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("source", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--expected-count", type=int)
    args = parser.parse_args()
    count = rewrite(args.source, args.output, args.expected_count)
    print(f"dequantized TFLite constants: {count}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
