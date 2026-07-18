#!/usr/bin/env python3
"""Build the exact standalone YNNLM char-decoder asset from TFLite m0.

The TFLite graph uses hybrid FullyConnected kernels: int8 weights plus
per-invocation asymmetric quantization of float activations. MindSpore Lite's
TFLite converter preserves the weights but changes those kernels to float
matmul, which causes score drift. ``YCD1`` stores the original constants and
quantization scales for the small native decoder in ``ynnlm_char_decoder.cpp``.
"""

from __future__ import annotations

import argparse
import hashlib
import struct
from dataclasses import dataclass
from pathlib import Path


MAGIC = b"YCD1"
VERSION = 1
HEADER_SIZE = 160

VOCAB_SIZE = 38
EMBEDDING_SIZE = 32
HIDDEN_SIZE = 750
GATE_SIZE = 3000
STATE_SIZE = 1500

TENSOR_INT8 = 9
TENSOR_FLOAT32 = 0


@dataclass(frozen=True)
class Tensor:
    name: str
    shape: tuple[int, ...]
    tensor_type: int
    data: bytes
    scales: tuple[float, ...]
    zero_points: tuple[int, ...]


@dataclass(frozen=True)
class BuildReport:
    source_sha256: str
    output_sha256: str
    output_bytes: int


class FlatBuffer:
    def __init__(self, data: bytes):
        self.data = data

    def _check(self, offset: int, size: int) -> None:
        if offset < 0 or size < 0 or offset + size > len(self.data):
            raise ValueError("TFLite FlatBuffer offset outside file")

    def u8(self, offset: int) -> int:
        self._check(offset, 1)
        return self.data[offset]

    def u16(self, offset: int) -> int:
        self._check(offset, 2)
        return struct.unpack_from("<H", self.data, offset)[0]

    def u32(self, offset: int) -> int:
        self._check(offset, 4)
        return struct.unpack_from("<I", self.data, offset)[0]

    def i32(self, offset: int) -> int:
        self._check(offset, 4)
        return struct.unpack_from("<i", self.data, offset)[0]

    def table_field(self, table: int, field_index: int) -> int | None:
        vtable = table - self.i32(table)
        self._check(vtable, 4)
        vtable_size = self.u16(vtable)
        entry = vtable + 4 + field_index * 2
        if entry + 2 > vtable + vtable_size:
            return None
        relative = self.u16(entry)
        return table + relative if relative else None

    def indirect(self, offset: int) -> int:
        return offset + self.u32(offset)

    def vector(self, field: int | None) -> tuple[int, int]:
        if field is None:
            return 0, 0
        vector = self.indirect(field)
        length = self.u32(vector)
        start = vector + 4
        self._check(start, 0)
        return start, length

    def vector_table(self, start: int, index: int) -> int:
        entry = start + index * 4
        return self.indirect(entry)

    def string(self, field: int | None) -> str:
        if field is None:
            return ""
        value = self.indirect(field)
        length = self.u32(value)
        self._check(value + 4, length)
        return self.data[value + 4:value + 4 + length].decode("utf-8")


def _int32_vector(fb: FlatBuffer, field: int | None) -> tuple[int, ...]:
    start, length = fb.vector(field)
    fb._check(start, length * 4)
    return tuple(struct.unpack_from(f"<{length}i", fb.data, start)) if length else ()


def _float_vector(fb: FlatBuffer, field: int | None) -> tuple[float, ...]:
    start, length = fb.vector(field)
    fb._check(start, length * 4)
    return tuple(struct.unpack_from(f"<{length}f", fb.data, start)) if length else ()


def _int64_vector(fb: FlatBuffer, field: int | None) -> tuple[int, ...]:
    start, length = fb.vector(field)
    fb._check(start, length * 8)
    return tuple(struct.unpack_from(f"<{length}q", fb.data, start)) if length else ()


def parse_tflite_tensors(data: bytes) -> dict[str, Tensor]:
    if len(data) < 8 or data[4:8] != b"TFL3":
        raise ValueError("input has no TFL3 identifier")
    fb = FlatBuffer(data)
    model = fb.u32(0)

    subgraphs_start, subgraph_count = fb.vector(fb.table_field(model, 2))
    if subgraph_count != 1:
        raise ValueError(f"expected one TFLite subgraph, got {subgraph_count}")
    subgraph = fb.vector_table(subgraphs_start, 0)

    buffers_start, buffer_count = fb.vector(fb.table_field(model, 4))
    buffers: list[bytes] = []
    for index in range(buffer_count):
        table = fb.vector_table(buffers_start, index)
        start, size = fb.vector(fb.table_field(table, 0))
        fb._check(start, size)
        buffers.append(data[start:start + size])

    tensors_start, tensor_count = fb.vector(fb.table_field(subgraph, 0))
    tensors: dict[str, Tensor] = {}
    for index in range(tensor_count):
        table = fb.vector_table(tensors_start, index)
        name = fb.string(fb.table_field(table, 3))
        shape = _int32_vector(fb, fb.table_field(table, 0))
        type_field = fb.table_field(table, 1)
        tensor_type = fb.u8(type_field) if type_field is not None else TENSOR_FLOAT32
        buffer_field = fb.table_field(table, 2)
        buffer_index = fb.u32(buffer_field) if buffer_field is not None else 0
        if buffer_index >= len(buffers):
            raise ValueError(f"tensor {name!r} has invalid buffer {buffer_index}")

        scales: tuple[float, ...] = ()
        zero_points: tuple[int, ...] = ()
        quant_field = fb.table_field(table, 4)
        if quant_field is not None:
            quant = fb.indirect(quant_field)
            scales = _float_vector(fb, fb.table_field(quant, 2))
            zero_points = _int64_vector(fb, fb.table_field(quant, 3))
        tensors[name] = Tensor(
            name=name,
            shape=shape,
            tensor_type=tensor_type,
            data=buffers[buffer_index],
            scales=scales,
            zero_points=zero_points,
        )
    return tensors


REQUIRED = (
    ("embedding", "dec_char/dec_char/char_emb/embedding_lookup/8711",
     (VOCAB_SIZE, EMBEDDING_SIZE), TENSOR_INT8),
    ("input_weights", "dec_char/dec_char/lstm/lstm/lstm_cell/MatMul",
     (GATE_SIZE, EMBEDDING_SIZE), TENSOR_INT8),
    ("recurrent_weights", "dec_char/dec_char/lstm/lstm/lstm_cell/MatMul_1",
     (GATE_SIZE, HIDDEN_SIZE), TENSOR_INT8),
    ("lstm_bias",
     "dec_char/dec_char/lstm/lstm/lstm_cell/BiasAdd/ReadVariableOp/resource",
     (GATE_SIZE,), TENSOR_FLOAT32),
    ("output_weights", "dec_char/dec_char/dense_o/MatMul",
     (VOCAB_SIZE, HIDDEN_SIZE), TENSOR_INT8),
    ("output_bias",
     "dec_char/dec_char/dense_o/BiasAdd/ReadVariableOp/resource",
     (VOCAB_SIZE,), TENSOR_FLOAT32),
)


def build_decoder_blob(source: bytes) -> bytes:
    tensors = parse_tflite_tensors(source)
    selected: list[Tensor] = []
    for label, name, shape, tensor_type in REQUIRED:
        tensor = tensors.get(name)
        if tensor is None:
            raise ValueError(f"missing required {label} tensor: {name}")
        if tensor.shape != shape or tensor.tensor_type != tensor_type:
            raise ValueError(
                f"invalid {label} contract: shape={tensor.shape} type={tensor.tensor_type}"
            )
        expected_bytes = struct.calcsize("b" if tensor_type == TENSOR_INT8 else "f")
        expected_bytes *= _element_count(shape)
        if len(tensor.data) != expected_bytes:
            raise ValueError(
                f"invalid {label} data size: {len(tensor.data)} != {expected_bytes}"
            )
        if tensor_type == TENSOR_INT8:
            if len(tensor.scales) != 1 or tensor.zero_points not in ((0,), ()):
                raise ValueError(
                    f"invalid {label} quantization: scales={tensor.scales} "
                    f"zero_points={tensor.zero_points}"
                )
        selected.append(tensor)

    scales = [selected[index].scales[0] for index in (0, 1, 2, 4)]
    total_size = HEADER_SIZE + sum(len(tensor.data) for tensor in selected)
    header = bytearray(HEADER_SIZE)
    struct.pack_into(
        "<4sIIIIIIII",
        header,
        0,
        MAGIC,
        VERSION,
        HEADER_SIZE,
        total_size,
        VOCAB_SIZE,
        EMBEDDING_SIZE,
        HIDDEN_SIZE,
        GATE_SIZE,
        STATE_SIZE,
    )
    struct.pack_into("<4f", header, 36, *scales)
    offset = HEADER_SIZE
    for index, tensor in enumerate(selected):
        struct.pack_into("<II", header, 52 + index * 8, offset, len(tensor.data))
        offset += len(tensor.data)
    header[100:132] = hashlib.sha256(source).digest()
    return bytes(header) + b"".join(tensor.data for tensor in selected)


def _element_count(shape: tuple[int, ...]) -> int:
    count = 1
    for dimension in shape:
        count *= dimension
    return count


def build_decoder_file(source_path: Path, output_path: Path) -> BuildReport:
    source = Path(source_path).read_bytes()
    blob = build_decoder_blob(source)
    output = Path(output_path)
    output.parent.mkdir(parents=True, exist_ok=True)
    temporary = output.with_suffix(output.suffix + ".tmp")
    temporary.write_bytes(blob)
    temporary.replace(output)
    return BuildReport(
        source_sha256=hashlib.sha256(source).hexdigest(),
        output_sha256=hashlib.sha256(blob).hexdigest(),
        output_bytes=len(blob),
    )


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Build exact YCD1 char decoder from YNNLM m0 TFLite"
    )
    parser.add_argument("input", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    report = build_decoder_file(args.input, args.output)
    print(
        f"YCD1 OK source_sha256={report.source_sha256} "
        f"output_sha256={report.output_sha256} bytes={report.output_bytes}"
    )


if __name__ == "__main__":
    main()
