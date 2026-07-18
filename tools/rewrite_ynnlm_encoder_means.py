#!/usr/bin/env python3
"""Rewrite fixed-width LayerNorm MEAN ops for the HarmonyOS Lite runtime.

The ynnlm encoder reduces tensors with the fixed shape [1, 1, 500].  The
MindSpore Lite 2.6 converter accepts those TFLite MEAN ops, but the device NNACL
runtime fails while preparing them (MEAN ReSize error 103).  A one-output fully
connected layer with weights 1/500 is the same reduction and is supported by
both the converter and the runtime.
"""

from __future__ import annotations

import argparse
from pathlib import Path

import flatbuffers
import numpy as np
from tensorflow.lite.python import schema_py_generated as schema


EXPECTED_INPUT_SHAPE = [1, 1, 500]
EXPECTED_OUTPUT_SHAPE = [1, 1, 1]
EXPECTED_MEAN_COUNT = 10


def _add_constant(
    model: schema.ModelT,
    subgraph: schema.SubGraphT,
    name: bytes,
    shape: list[int],
    value: np.ndarray,
) -> int:
    buffer = schema.BufferT()
    buffer.data = np.frombuffer(value.tobytes(), dtype=np.uint8).copy()
    model.buffers.append(buffer)

    tensor = schema.TensorT()
    tensor.shape = np.asarray(shape, dtype=np.int32)
    tensor.type = schema.TensorType.FLOAT32
    tensor.buffer = len(model.buffers) - 1
    tensor.name = name
    tensor.quantization = schema.QuantizationParametersT()
    subgraph.tensors.append(tensor)
    return len(subgraph.tensors) - 1


def rewrite_encoder(source: Path, output: Path) -> int:
    data = source.read_bytes()
    root = schema.Model.GetRootAsModel(data, 0)
    model = schema.ModelT.InitFromObj(root)
    if len(model.subgraphs) != 1:
        raise ValueError(f"expected one encoder subgraph, got {len(model.subgraphs)}")
    subgraph = model.subgraphs[0]

    fc_opcode = next(
        (
            i
            for i, opcode in enumerate(model.operatorCodes)
            if opcode.builtinCode == schema.BuiltinOperator.FULLY_CONNECTED
        ),
        None,
    )
    if fc_opcode is None:
        raise ValueError("encoder has no FULLY_CONNECTED opcode to reuse")

    weights = _add_constant(
        model,
        subgraph,
        b"enc/layer_norm_mean_weights",
        [1, 500],
        np.full(500, 1.0 / 500.0, dtype=np.float32),
    )
    bias = _add_constant(
        model,
        subgraph,
        b"enc/layer_norm_mean_bias",
        [1],
        np.zeros(1, dtype=np.float32),
    )

    rewritten = 0
    for operator in subgraph.operators:
        opcode = model.operatorCodes[operator.opcodeIndex]
        if opcode.builtinCode != schema.BuiltinOperator.MEAN:
            continue

        input_tensor = subgraph.tensors[int(operator.inputs[0])]
        output_tensor = subgraph.tensors[int(operator.outputs[0])]
        if list(input_tensor.shape) != EXPECTED_INPUT_SHAPE:
            raise ValueError(f"unexpected MEAN input shape: {list(input_tensor.shape)}")
        if list(output_tensor.shape) != EXPECTED_OUTPUT_SHAPE:
            raise ValueError(f"unexpected MEAN output shape: {list(output_tensor.shape)}")

        options = schema.FullyConnectedOptionsT()
        options.fusedActivationFunction = schema.ActivationFunctionType.NONE
        options.weightsFormat = schema.FullyConnectedOptionsWeightsFormat.DEFAULT
        options.keepNumDims = True
        # Match the options on the encoder's existing FC operators.  Converter
        # 2.6 otherwise rejects the inserted fixed-width reduction at infer-shape.
        options.asymmetricQuantizeInputs = True
        options.quantizedBiasType = schema.TensorType.FLOAT32

        operator.opcodeIndex = fc_opcode
        operator.inputs = np.asarray(
            [int(operator.inputs[0]), weights, bias], dtype=np.int32
        )
        operator.builtinOptionsType = schema.BuiltinOptions.FullyConnectedOptions
        operator.builtinOptions = options
        rewritten += 1

    if rewritten != EXPECTED_MEAN_COUNT:
        raise ValueError(
            f"expected {EXPECTED_MEAN_COUNT} fixed LayerNorm MEAN ops, got {rewritten}"
        )

    builder = flatbuffers.Builder(len(data) + 4096)
    model_offset = model.Pack(builder)
    builder.Finish(model_offset, file_identifier=b"TFL3")
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_bytes(builder.Output())
    return rewritten


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("source", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    count = rewrite_encoder(args.source, args.output)
    print(f"rewritten encoder LayerNorm MEAN ops: {count}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
