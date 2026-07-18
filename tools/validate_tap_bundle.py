"""Strict parser for the recovered default TapModel protobuf bundle.

The bundle is length-delimited protobuf without a generated schema.  This file
implements only the descriptor recovered from the native library and rejects
unknown wire contracts, duplicate map keys, truncation and non-finite Gaussian
parameters. Parsing this one asset alone is intentionally not a production
gate; the C++ TapModelRunner additionally validates the exact 33+5 schema,
38-feature ranker and 2N+2 (78-feature) classifier as one atomic chain.
"""

from __future__ import annotations

import argparse
import dataclasses
import hashlib
import json
import math
import struct
from pathlib import Path


@dataclasses.dataclass(frozen=True)
class TapKeyParams:
    x: int = 0
    y: int = 0
    width: int = 0
    height: int = 0
    log_prior: float = 0.0
    mean_x: float = 0.0
    mean_y: float = 0.0
    variance_x: float = 0.0
    variance_y: float = 0.0
    correlation: float = 0.0


@dataclasses.dataclass(frozen=True)
class StaticTapModel:
    key_params: dict[str, TapKeyParams]
    width: int = 0
    height: int = 0


@dataclasses.dataclass(frozen=True)
class TapModelBundle:
    frame_size: int
    layouts: dict[str, StaticTapModel]


def _varint(data: bytes, position: int, end: int) -> tuple[int, int]:
    value = 0
    for shift in range(0, 70, 7):
        if position >= end:
            raise ValueError("truncated protobuf varint")
        byte = data[position]
        position += 1
        value |= (byte & 0x7F) << shift
        if byte < 0x80:
            return value, position
    raise ValueError("protobuf varint exceeds 10 bytes")


def _fields(data: bytes) -> list[tuple[int, int, int | bytes]]:
    result: list[tuple[int, int, int | bytes]] = []
    position = 0
    end = len(data)
    while position < end:
        tag, position = _varint(data, position, end)
        number = tag >> 3
        wire = tag & 7
        if number == 0:
            raise ValueError("protobuf field number zero")
        if wire == 0:
            value, position = _varint(data, position, end)
            result.append((number, wire, value))
        elif wire == 2:
            length, position = _varint(data, position, end)
            if length > end - position:
                raise ValueError("truncated protobuf length-delimited field")
            result.append((number, wire, data[position:position + length]))
            position += length
        elif wire == 5:
            if end - position < 4:
                raise ValueError("truncated protobuf fixed32 field")
            result.append((number, wire, data[position:position + 4]))
            position += 4
        else:
            raise ValueError(f"unsupported protobuf wire type {wire}")
    return result


def _single(fields: list[tuple[int, int, int | bytes]], number: int,
            wire: int, default=None):
    values = [value for field, field_wire, value in fields
              if field == number and field_wire == wire]
    wrong = [field_wire for field, field_wire, _ in fields
             if field == number and field_wire != wire]
    if wrong:
        raise ValueError(f"field {number} has wrong wire type")
    if len(values) > 1:
        raise ValueError(f"field {number} repeated unexpectedly")
    return values[0] if values else default


def _utf8(value: bytes | None, label: str) -> str:
    if value is None:
        raise ValueError(f"{label} missing")
    try:
        text = value.decode("utf-8")
    except UnicodeDecodeError as error:
        raise ValueError(f"{label} is not UTF-8") from error
    if not text:
        raise ValueError(f"{label} is empty")
    return text


def _float32(value: bytes | None) -> float:
    if value is None:
        return 0.0
    result = struct.unpack("<f", value)[0]
    if not math.isfinite(result):
        raise ValueError("TapModel parameter is non-finite")
    return result


def _reject_unknown(fields: list[tuple[int, int, int | bytes]],
                    allowed: dict[int, int], label: str) -> None:
    for number, wire, _ in fields:
        if number not in allowed or allowed[number] != wire:
            raise ValueError(f"{label}: unexpected field {number}/wire {wire}")


def _parse_key_params(data: bytes) -> TapKeyParams:
    fields = _fields(data)
    allowed = {1: 0, 2: 0, 3: 0, 4: 0, 5: 5, 6: 5, 7: 5,
               8: 5, 9: 5, 10: 5}
    _reject_unknown(fields, allowed, "TKeyParams")
    params = TapKeyParams(
        x=int(_single(fields, 1, 0, 0)),
        y=int(_single(fields, 2, 0, 0)),
        width=int(_single(fields, 3, 0, 0)),
        height=int(_single(fields, 4, 0, 0)),
        log_prior=_float32(_single(fields, 5, 5)),
        mean_x=_float32(_single(fields, 6, 5)),
        mean_y=_float32(_single(fields, 7, 5)),
        variance_x=_float32(_single(fields, 8, 5)),
        variance_y=_float32(_single(fields, 9, 5)),
        correlation=_float32(_single(fields, 10, 5)),
    )
    if params.width < 0 or params.height < 0:
        raise ValueError("TKeyParams has negative dimensions")
    if params.variance_x < 0 or params.variance_y < 0:
        raise ValueError("TKeyParams has negative variance")
    if not -1.0 <= params.correlation <= 1.0:
        raise ValueError("TKeyParams correlation outside [-1,1]")
    return params


def _parse_key_entry(data: bytes) -> tuple[str, TapKeyParams]:
    fields = _fields(data)
    _reject_unknown(fields, {1: 2, 2: 2}, "KeyParamsEntry")
    key = _utf8(_single(fields, 1, 2), "KeyParamsEntry.key")
    value = _single(fields, 2, 2)
    if value is None:
        raise ValueError("KeyParamsEntry.value missing")
    return key, _parse_key_params(value)


def _parse_static_model(data: bytes) -> StaticTapModel:
    fields = _fields(data)
    _reject_unknown(fields, {1: 2, 2: 0, 3: 0}, "TStaticTapModelParams")
    params: dict[str, TapKeyParams] = {}
    for number, wire, value in fields:
        if number != 1:
            continue
        key, key_params = _parse_key_entry(value)  # type: ignore[arg-type]
        if key in params:
            raise ValueError(f"duplicate TapModel key {key!r}")
        params[key] = key_params
    width = int(_single(fields, 2, 0, 0))
    height = int(_single(fields, 3, 0, 0))
    if width <= 0 or height <= 0 or not params:
        raise ValueError("static TapModel has invalid dimensions/empty map")
    return StaticTapModel(params, width, height)


def _parse_layout_entry(data: bytes) -> tuple[str, StaticTapModel]:
    fields = _fields(data)
    _reject_unknown(fields, {1: 2, 2: 2}, "ParamsEntry")
    key = _utf8(_single(fields, 1, 2), "ParamsEntry.key")
    value = _single(fields, 2, 2)
    if value is None:
        raise ValueError("ParamsEntry.value missing")
    return key, _parse_static_model(value)


def parse_tap_bundle(data: bytes) -> TapModelBundle:
    frame_size, payload_start = _varint(data, 0, len(data))
    if frame_size != len(data) - payload_start:
        raise ValueError(
            f"TapModel frame size {frame_size} != payload "
            f"{len(data) - payload_start}")
    payload = data[payload_start:]
    fields = _fields(payload)
    _reject_unknown(fields, {1: 2}, "TTapModelBundleParams")
    layouts: dict[str, StaticTapModel] = {}
    for _, _, value in fields:
        key, model = _parse_layout_entry(value)  # type: ignore[arg-type]
        if key in layouts:
            raise ValueError(f"duplicate TapModel layout {key!r}")
        layouts[key] = model
    if not layouts:
        raise ValueError("TapModel bundle has no layouts")
    return TapModelBundle(frame_size, layouts)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--bundle", type=Path, required=True)
    parser.add_argument("--json-out", type=Path)
    args = parser.parse_args()
    data = args.bundle.read_bytes()
    bundle = parse_tap_bundle(data)
    report = {
        "source": str(args.bundle.resolve()),
        "source_sha256": hashlib.sha256(data).hexdigest(),
        "frame_size": bundle.frame_size,
        "layouts": {
            name: {
                "width": layout.width,
                "height": layout.height,
                "key_params": len(layout.key_params),
            }
            for name, layout in bundle.layouts.items()
        },
        "production_eligible": False,
        "blocker": ("bundle-only validation; run the atomic TapModel runner "
                    "and packaged-chain gates"),
    }
    if args.json_out:
        args.json_out.parent.mkdir(parents=True, exist_ok=True)
        args.json_out.write_text(json.dumps(report, ensure_ascii=False,
                                             indent=2), encoding="utf-8")
    print(
        "asset=tap_models_default,contract=valid,variant=default,"
        "load=validated,invoke_count=0,scored_count=0,"
        "fallback=blocked:bundle_only_gate,latency_us=0,"
        f"layouts={len(bundle.layouts)},keys="
        f"{sum(len(layout.key_params) for layout in bundle.layouts.values())}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
