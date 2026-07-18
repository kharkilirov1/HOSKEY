#!/usr/bin/env python3
"""Rewrite RNNT TFLite graph patterns unsupported by MindSpore Lite 2.6.

MindSpore Lite 2.6 does not register a TFLite REDUCE_ANY parser, and its NNACL
REDUCE_MAX kernel does not execute boolean tensors.  Every RNNT mask has the
exact pattern BOOL REDUCE_ANY -> CAST(FLOAT32).  Rewrite that pair to
CAST(FLOAT32) -> REDUCE_MAX; this preserves the truth table and leaves the
downstream tensor unchanged.

The post-conformer graphs also contain SPLIT_V operations with sizes [0, N].
TFLite accepts the empty first output, while the Lite runtime rejects a split
whose axis is smaller than the output count.  The empty output is dead and the
N-sized output is an identity, so the rewrite removes only that exact pattern.
"""

from __future__ import annotations

import argparse
from pathlib import Path

import flatbuffers
import numpy as np
from tensorflow.lite.python import schema_py_generated as schema


def _constant_int32(model: schema.ModelT, tensor: schema.TensorT) -> list[int]:
    if tensor.type != schema.TensorType.INT32:
        raise ValueError(f"reduction axis must be INT32, got {tensor.type}")
    data = model.buffers[int(tensor.buffer)].data
    if data is None:
        raise ValueError("reduction axis has no constant buffer")
    return np.frombuffer(bytes(data), dtype="<i4").astype(int).tolist()


def _opcode_index(model: schema.ModelT, builtin_code: int) -> int:
    for i, opcode in enumerate(model.operatorCodes):
        if opcode.builtinCode == builtin_code:
            return i
    opcode = schema.OperatorCodeT()
    opcode.builtinCode = builtin_code
    opcode.deprecatedBuiltinCode = builtin_code
    opcode.version = 1
    model.operatorCodes.append(opcode)
    return len(model.operatorCodes) - 1


def _add_float_intermediate(
    model: schema.ModelT,
    subgraph: schema.SubGraphT,
    source: schema.TensorT,
    name_suffix: bytes,
) -> int:
    tensor = schema.TensorT()
    tensor.shape = np.asarray(source.shape, dtype=np.int32)
    if source.shapeSignature is not None:
        tensor.shapeSignature = np.asarray(source.shapeSignature, dtype=np.int32)
    tensor.type = schema.TensorType.FLOAT32
    tensor.buffer = 0
    tensor.name = (source.name or b"rnnt_mask") + name_suffix
    tensor.quantization = schema.QuantizationParametersT()
    subgraph.tensors.append(tensor)
    return len(subgraph.tensors) - 1


def _prune_degenerate_split_v(
    model: schema.ModelT, subgraph: schema.SubGraphT
) -> int:
    pruned = 0
    kept_operators = []
    for operator in subgraph.operators:
        opcode = model.operatorCodes[int(operator.opcodeIndex)]
        if opcode.builtinCode != schema.BuiltinOperator.SPLIT_V:
            kept_operators.append(operator)
            continue

        value_id = int(operator.inputs[0])
        sizes = _constant_int32(model, subgraph.tensors[int(operator.inputs[1])])
        axes = _constant_int32(model, subgraph.tensors[int(operator.inputs[2])])
        value_shape = [int(v) for v in subgraph.tensors[value_id].shape]
        if len(axes) != 1:
            kept_operators.append(operator)
            continue
        axis = axes[0] + len(value_shape) if axes[0] < 0 else axes[0]
        nonempty = [i for i, size in enumerate(sizes) if size != 0]
        live_shape = (
            [int(v) for v in subgraph.tensors[int(operator.outputs[nonempty[0]])].shape]
            if len(nonempty) == 1
            else []
        )
        if (
            axis < 0
            or axis >= len(value_shape)
            or len(nonempty) != 1
            or sizes[nonempty[0]] not in (-1, value_shape[axis])
            or live_shape != value_shape
        ):
            kept_operators.append(operator)
            continue

        live_output = int(operator.outputs[nonempty[0]])
        dead_outputs = {
            int(operator.outputs[i]) for i, size in enumerate(sizes) if size == 0
        }
        graph_outputs = {int(v) for v in subgraph.outputs}
        if dead_outputs & graph_outputs:
            raise ValueError("degenerate SPLIT_V empty output is a graph output")
        for consumer in subgraph.operators:
            if consumer is operator:
                continue
            if any(int(v) in dead_outputs for v in consumer.inputs):
                raise ValueError("degenerate SPLIT_V empty output is consumed")
            consumer.inputs = np.asarray(
                [value_id if int(v) == live_output else int(v) for v in consumer.inputs],
                dtype=np.int32,
            )
        subgraph.outputs = np.asarray(
            [value_id if int(v) == live_output else int(v) for v in subgraph.outputs],
            dtype=np.int32,
        )
        pruned += 1

    subgraph.operators = kept_operators
    return pruned


def rewrite_rnnt(
    source: Path,
    output: Path,
    expected_count: int | None,
    expected_split_v_prunes: int | None,
) -> tuple[int, int]:
    data = source.read_bytes()
    root = schema.Model.GetRootAsModel(data, 0)
    model = schema.ModelT.InitFromObj(root)
    if len(model.subgraphs) != 1:
        raise ValueError(f"expected one RNNT subgraph, got {len(model.subgraphs)}")
    subgraph = model.subgraphs[0]

    rewritten = 0
    cast_opcode = _opcode_index(model, schema.BuiltinOperator.CAST)
    reduce_max_opcode = _opcode_index(model, schema.BuiltinOperator.REDUCE_MAX)
    for operator in subgraph.operators:
        opcode_index = int(operator.opcodeIndex)
        opcode = model.operatorCodes[opcode_index]
        if opcode.builtinCode != schema.BuiltinOperator.REDUCE_ANY:
            continue
        if len(operator.inputs) != 2 or len(operator.outputs) != 1:
            raise ValueError("unexpected REDUCE_ANY arity")
        mask_id = int(operator.inputs[0])
        axis_id = int(operator.inputs[1])
        input_tensor = subgraph.tensors[mask_id]
        axes_tensor = subgraph.tensors[axis_id]
        output_tensor = subgraph.tensors[int(operator.outputs[0])]
        if input_tensor.type != schema.TensorType.BOOL:
            raise ValueError(f"REDUCE_ANY input must be BOOL, got {input_tensor.type}")
        if output_tensor.type != schema.TensorType.BOOL:
            raise ValueError(f"REDUCE_ANY output must be BOOL, got {output_tensor.type}")
        axes = _constant_int32(model, axes_tensor)
        if axes != [-1]:
            raise ValueError(f"only last-axis RNNT reductions are supported, got {axes}")
        output_id = int(operator.outputs[0])
        consumers = [
            candidate
            for candidate in subgraph.operators
            if candidate is not operator
            and any(int(v) == output_id for v in candidate.inputs)
        ]
        if len(consumers) != 1:
            raise ValueError(f"REDUCE_ANY output has {len(consumers)} consumers")
        cast = consumers[0]
        cast_code = model.operatorCodes[int(cast.opcodeIndex)].builtinCode
        if cast_code != schema.BuiltinOperator.CAST or len(cast.outputs) != 1:
            raise ValueError("REDUCE_ANY consumer is not a single-output CAST")
        cast_output = subgraph.tensors[int(cast.outputs[0])]
        if cast_output.type != schema.TensorType.FLOAT32:
            raise ValueError("REDUCE_ANY CAST output is not FLOAT32")

        reducer_options = operator.builtinOptions
        cast_options = cast.builtinOptions
        cast_output_id = int(cast.outputs[0])
        float_input = _add_float_intermediate(
            model, subgraph, input_tensor, b"/cast_before_reduce"
        )
        operator.opcodeIndex = cast_opcode
        operator.inputs = np.asarray([mask_id], dtype=np.int32)
        operator.outputs = np.asarray([float_input], dtype=np.int32)
        operator.builtinOptionsType = schema.BuiltinOptions.CastOptions
        operator.builtinOptions = cast_options

        cast.opcodeIndex = reduce_max_opcode
        cast.inputs = np.asarray([float_input, axis_id], dtype=np.int32)
        cast.outputs = np.asarray([cast_output_id], dtype=np.int32)
        cast.builtinOptionsType = schema.BuiltinOptions.ReducerOptions
        cast.builtinOptions = reducer_options
        rewritten += 1

    if expected_count is not None and rewritten != expected_count:
        raise ValueError(f"expected {expected_count} REDUCE_ANY ops, got {rewritten}")
    if rewritten == 0:
        raise ValueError("model has no REDUCE_ANY operators")

    split_v_prunes = _prune_degenerate_split_v(model, subgraph)
    if (
        expected_split_v_prunes is not None
        and split_v_prunes != expected_split_v_prunes
    ):
        raise ValueError(
            f"expected {expected_split_v_prunes} degenerate SPLIT_V ops, "
            f"got {split_v_prunes}"
        )

    builder = flatbuffers.Builder(len(data) + 1024)
    model_offset = model.Pack(builder)
    builder.Finish(model_offset, file_identifier=b"TFL3")
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_bytes(builder.Output())
    return rewritten, split_v_prunes


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("source", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--expected-count", type=int)
    parser.add_argument("--expected-split-v-prunes", type=int)
    args = parser.parse_args()
    count, split_v_prunes = rewrite_rnnt(
        args.source,
        args.output,
        args.expected_count,
        args.expected_split_v_prunes,
    )
    print(
        f"rewritten RNNT REDUCE_ANY ops: {count}; "
        f"pruned degenerate SPLIT_V ops: {split_v_prunes}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
