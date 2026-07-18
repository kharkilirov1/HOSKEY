#!/usr/bin/env python3
"""Make float REDUCE_MAX portable to MindSpore Lite's NNACL runtime.

MindSpore Lite 2.6's converted float REDUCE_MAX path initializes the maximum
with the smallest positive float.  Consequently, a reduction whose values are
all negative is returned as approximately zero.  For tensors with a known
lower bound, adding a positive offset before the reduction and subtracting it
afterward preserves the TFLite result while keeping every reduced input above
zero.

This rewrite is intentionally strict: it only touches FLOAT32 REDUCE_MAX ops
whose producer is TANH, for which +1 is a mathematically safe offset.
"""

from __future__ import annotations

import argparse
from pathlib import Path

import flatbuffers
import numpy as np
from tensorflow.lite.python import schema_py_generated as schema


def _find_opcode(model: schema.ModelT, builtin_code: int) -> int:
    for index, opcode in enumerate(model.operatorCodes):
        if opcode.builtinCode == builtin_code:
            return index
    raise ValueError(f"model has no reusable builtin opcode {builtin_code}")


def _add_float_constant(
    model: schema.ModelT,
    subgraph: schema.SubGraphT,
    name: bytes,
    value: float,
) -> int:
    buffer = schema.BufferT()
    buffer.data = np.frombuffer(
        np.asarray([value], dtype=np.float32).tobytes(), dtype=np.uint8
    ).copy()
    model.buffers.append(buffer)

    tensor = schema.TensorT()
    tensor.shape = np.asarray([1], dtype=np.int32)
    tensor.type = schema.TensorType.FLOAT32
    tensor.buffer = len(model.buffers) - 1
    tensor.name = name
    tensor.quantization = schema.QuantizationParametersT()
    subgraph.tensors.append(tensor)
    return len(subgraph.tensors) - 1


def _add_runtime_tensor(
    subgraph: schema.SubGraphT,
    template: schema.TensorT,
    name: bytes,
) -> int:
    tensor = schema.TensorT()
    tensor.shape = np.asarray(template.shape, dtype=np.int32)
    if template.shapeSignature is not None:
        tensor.shapeSignature = np.asarray(template.shapeSignature, dtype=np.int32)
    tensor.type = template.type
    tensor.buffer = 0
    tensor.name = name
    tensor.quantization = schema.QuantizationParametersT()
    subgraph.tensors.append(tensor)
    return len(subgraph.tensors) - 1


def _binary_operator(
    opcode_index: int,
    input_a: int,
    input_b: int,
    output: int,
    *,
    add: bool,
) -> schema.OperatorT:
    operator = schema.OperatorT()
    operator.opcodeIndex = opcode_index
    operator.inputs = np.asarray([input_a, input_b], dtype=np.int32)
    operator.outputs = np.asarray([output], dtype=np.int32)
    if add:
        options = schema.AddOptionsT()
        options.fusedActivationFunction = schema.ActivationFunctionType.NONE
        operator.builtinOptionsType = schema.BuiltinOptions.AddOptions
    else:
        options = schema.SubOptionsT()
        options.fusedActivationFunction = schema.ActivationFunctionType.NONE
        operator.builtinOptionsType = schema.BuiltinOptions.SubOptions
    operator.builtinOptions = options
    return operator


def _is_tanh_bounded(
    model: schema.ModelT,
    producer_by_tensor: dict[int, schema.OperatorT],
    tensor_index: int,
) -> bool:
    """Return true when a tensor is TANH output modulo shape-only operators."""
    shape_only = {
        schema.BuiltinOperator.RESHAPE,
        schema.BuiltinOperator.SQUEEZE,
        schema.BuiltinOperator.TRANSPOSE,
    }
    seen: set[int] = set()
    while tensor_index not in seen:
        seen.add(tensor_index)
        producer = producer_by_tensor.get(tensor_index)
        if producer is None:
            return False
        code = model.operatorCodes[producer.opcodeIndex].builtinCode
        if code == schema.BuiltinOperator.TANH:
            return True
        if code not in shape_only or not len(producer.inputs):
            return False
        tensor_index = int(producer.inputs[0])
    return False


def rewrite_reduce_max(source: Path, output: Path, expected_count: int | None) -> int:
    data = source.read_bytes()
    root = schema.Model.GetRootAsModel(data, 0)
    model = schema.ModelT.InitFromObj(root)
    if len(model.subgraphs) != 1:
        raise ValueError(f"expected one subgraph, got {len(model.subgraphs)}")
    subgraph = model.subgraphs[0]
    add_opcode = _find_opcode(model, schema.BuiltinOperator.ADD)
    sub_opcode = _find_opcode(model, schema.BuiltinOperator.SUB)
    offset_tensor = _add_float_constant(
        model, subgraph, b"hoskey/reduce_max_positive_offset", 1.0
    )

    producer_by_tensor: dict[int, schema.OperatorT] = {}
    for operator in subgraph.operators:
        for tensor_index in operator.outputs:
            producer_by_tensor[int(tensor_index)] = operator

    rewritten = 0
    operators: list[schema.OperatorT] = []
    for operator in subgraph.operators:
        opcode = model.operatorCodes[operator.opcodeIndex]
        if opcode.builtinCode != schema.BuiltinOperator.REDUCE_MAX:
            operators.append(operator)
            continue
        if len(operator.inputs) != 2 or len(operator.outputs) != 1:
            raise ValueError("expected REDUCE_MAX with two inputs and one output")

        original_input = int(operator.inputs[0])
        original_output = int(operator.outputs[0])
        input_tensor = subgraph.tensors[original_input]
        output_tensor = subgraph.tensors[original_output]
        if input_tensor.type != schema.TensorType.FLOAT32:
            raise ValueError("refusing to rewrite non-FLOAT32 REDUCE_MAX")
        if not _is_tanh_bounded(model, producer_by_tensor, original_input):
            raise ValueError("refusing REDUCE_MAX not bounded by TANH")

        shifted_input = _add_runtime_tensor(
            subgraph,
            input_tensor,
            f"hoskey/reduce_max_{rewritten}/shifted_input".encode(),
        )
        shifted_output = _add_runtime_tensor(
            subgraph,
            output_tensor,
            f"hoskey/reduce_max_{rewritten}/shifted_output".encode(),
        )
        operators.append(
            _binary_operator(
                add_opcode,
                original_input,
                offset_tensor,
                shifted_input,
                add=True,
            )
        )
        operator.inputs = np.asarray(
            [shifted_input, int(operator.inputs[1])], dtype=np.int32
        )
        operator.outputs = np.asarray([shifted_output], dtype=np.int32)
        operators.append(operator)
        operators.append(
            _binary_operator(
                sub_opcode,
                shifted_output,
                offset_tensor,
                original_output,
                add=False,
            )
        )
        rewritten += 1

    if expected_count is not None and rewritten != expected_count:
        raise ValueError(f"expected {expected_count} REDUCE_MAX ops, got {rewritten}")
    if rewritten == 0:
        raise ValueError("no REDUCE_MAX ops were rewritten")
    subgraph.operators = operators

    builder = flatbuffers.Builder(len(data) + 16384)
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
    count = rewrite_reduce_max(args.source, args.output, args.expected_count)
    print(f"rewritten FLOAT32 TANH REDUCE_MAX ops: {count}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
