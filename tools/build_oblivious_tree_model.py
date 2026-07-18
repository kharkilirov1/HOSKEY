"""Convert recovered MatrixNet-like forests to HOSKEY's ``HOBL`` format.

The common evaluator contract is recovered: strict ``>``, LSB-first leaf bits,
float64 leaves/accumulation and sigmoid calibration.  Role-specific feature
semantics and policy gates remain separate activation requirements, so the CLI
still writes a provenance sidecar and marks converted artifacts ineligible for
production unless those contracts are independently proven.
"""

from __future__ import annotations

import argparse
import dataclasses
import hashlib
import json
import math
import struct
from pathlib import Path
from typing import Any


@dataclasses.dataclass(frozen=True)
class ConversionFacts:
    tree_offset: int
    tree_count: int
    depth: int
    feature_count: int
    flat_border_count: int
    condition_max_plus_one: int
    zero_border_features: list[int]
    comparator_assumption: str = "gt_proven"
    leaf_order_assumption: str = "lsb_proven"
    contract: str = "evaluator_exact_features_unverified"


@dataclasses.dataclass(frozen=True)
class _RawTree:
    offset: int
    condition_offset: int
    object_end: int


def _u32(data: bytes, offset: int) -> int:
    return struct.unpack_from("<I", data, offset)[0]


def _f32(data: bytes, offset: int) -> float:
    return struct.unpack_from("<f", data, offset)[0]


def _f64(data: bytes, offset: int) -> float:
    return struct.unpack_from("<d", data, offset)[0]


def feature_count_from_payload(payload: dict[str, Any]) -> int:
    if isinstance(payload.get("Features"), list):
        return (len(payload["Features"]) + len(payload.get("Stats", [])) +
                len(payload.get("Combos", [])))

    count = 0
    for section in payload.values():
        if not isinstance(section, dict):
            continue
        count += len(section.get("Features", []))
        count += len(section.get("Combos", []))
        count += len(section.get("Stats", []))
    return count


def feature_count_from_file(path: Path) -> int:
    payload = json.loads(path.read_text(encoding="utf-8"))
    count = feature_count_from_payload(payload)
    if not 1 <= count <= 255:
        raise ValueError(f"invalid feature count {count} from {path}")
    return count


def _tree_layout(data: bytes, offset: int,
                 expected_depth: int | None) -> tuple[int, int] | None:
    if offset < 0 or offset + 16 > len(data):
        return None
    leaf_count = _u32(data, offset + 12)
    if leaf_count == 0 or leaf_count & (leaf_count - 1):
        return None
    depth = leaf_count.bit_length() - 1
    if not 1 <= depth <= 12 or (expected_depth is not None and
                                depth != expected_depth):
        return None
    header_end = 16 + leaf_count * 8
    if (_u32(data, offset + 4) != header_end or
            _u32(data, offset + 8) != 4):
        return None
    record_size = header_end + 8 + depth * 4
    if offset + record_size > len(data):
        return None
    if (_u32(data, offset + header_end) != 0 or
            _u32(data, offset + header_end + 4) != depth):
        return None
    return depth, record_size


def _tree_object(data: bytes, offset: int,
                 expected_depth: int | None) -> tuple[_RawTree, int] | None:
    if offset < 0 or offset + 16 > len(data) or _u32(data, offset + 8) != 4:
        return None
    leaf_count = _u32(data, offset + 12)
    if leaf_count == 0 or leaf_count & (leaf_count - 1):
        return None
    depth = leaf_count.bit_length() - 1
    if not 1 <= depth <= 12 or (expected_depth is not None and
                                depth != expected_depth):
        return None
    header_end = 16 + leaf_count * 8
    if offset + header_end + 4 > len(data):
        return None
    pointer = _u32(data, offset + 4)
    footer = _u32(data, offset + header_end)
    if pointer == header_end and footer == 0:
        if (offset + header_end + 8 > len(data) or
                _u32(data, offset + header_end + 4) != depth):
            return None
        condition_offset = offset + header_end + 8
    elif pointer == header_end - 4 and footer == depth:
        # The first logical tree in recovered blobs omits the otherwise
        # constant zero footer word.  The outer relative-pointer vector is the
        # only reliable way to include this compact object.
        condition_offset = offset + header_end + 4
    else:
        return None
    object_end = condition_offset + depth * 4
    if object_end > len(data):
        return None
    return _RawTree(offset, condition_offset, object_end), depth


def _find_pointer_tree_set(
        data: bytes, expected_depth: int | None,
        expected_tree_count: int | None) -> tuple[list[_RawTree], int] | None:
    candidates: list[tuple[list[_RawTree], int]] = []
    for vector_offset in range(0, max(0, len(data) - 7), 4):
        if _u32(data, vector_offset) != 4:
            continue
        count = _u32(data, vector_offset + 4)
        if count == 0 or count > 100000:
            continue
        if expected_tree_count is not None and count != expected_tree_count:
            continue
        table_end = vector_offset + 8 + count * 4
        if table_end > len(data):
            continue
        trees: list[_RawTree] = []
        depth: int | None = None
        valid = True
        for index in range(count):
            pointer_offset = vector_offset + 8 + index * 4
            relative = _u32(data, pointer_offset)
            target = pointer_offset + relative
            if target < table_end:
                valid = False
                break
            parsed = _tree_object(data, target, expected_depth)
            if parsed is None:
                valid = False
                break
            tree, tree_depth = parsed
            if depth is None:
                depth = tree_depth
            elif depth != tree_depth:
                valid = False
                break
            trees.append(tree)
        if valid and depth is not None and len({tree.offset for tree in trees}) == count:
            candidates.append((trees, depth))
    if not candidates:
        return None
    max_count = max(len(candidate[0]) for candidate in candidates)
    best = [candidate for candidate in candidates
            if len(candidate[0]) == max_count]
    if len(best) != 1:
        raise ValueError("ambiguous relative-pointer tree vector")
    return best[0]


def _find_tree_run(data: bytes, expected_depth: int | None,
                   expected_tree_count: int | None) -> tuple[int, int, int, int]:
    candidates: list[tuple[int, int, int, int]] = []
    for offset in range(0, max(0, len(data) - 15), 4):
        layout = _tree_layout(data, offset, expected_depth)
        if layout is None:
            continue
        depth, record_size = layout
        count = 1
        while _tree_layout(data, offset + count * record_size, depth) == (
                depth, record_size):
            count += 1
        candidates.append((offset, count, depth, record_size))

    if not candidates:
        suffix = (f" for expected depth {expected_depth}"
                  if expected_depth is not None else "")
        raise ValueError(f"no valid oblivious tree run{suffix}")
    best_count = max(candidate[1] for candidate in candidates)
    best = [candidate for candidate in candidates if candidate[1] == best_count]
    # Interior records are also candidates but have shorter runs.  Two equal
    # longest runs indicate that the source cannot be selected unambiguously.
    if len(best) != 1:
        raise ValueError("ambiguous oblivious tree run")
    result = best[0]
    if expected_tree_count is not None and result[1] != expected_tree_count:
        raise ValueError(
            f"tree run count {result[1]} != expected {expected_tree_count}")
    return result


def _find_trees(data: bytes, expected_depth: int | None,
                expected_tree_count: int | None
                ) -> tuple[list[_RawTree], int, int]:
    pointer_set = _find_pointer_tree_set(
        data, expected_depth, expected_tree_count)
    if pointer_set is not None:
        trees, depth = pointer_set
        return trees, depth, max(tree.object_end for tree in trees)

    offset, count, depth, record_size = _find_tree_run(
        data, expected_depth, expected_tree_count)
    leaf_count = 1 << depth
    header_end = 16 + leaf_count * 8
    trees = [
        _RawTree(
            offset=offset + index * record_size,
            condition_offset=offset + index * record_size + header_end + 8,
            object_end=offset + (index + 1) * record_size,
        )
        for index in range(count)
    ]
    return trees, depth, trees[-1].object_end


def _monotonic_floats(data: bytes, offset: int,
                      count: int) -> list[float] | None:
    if count <= 0 or count > 65535 or offset + count * 4 > len(data):
        return None
    values = [_f32(data, offset + index * 4) for index in range(count)]
    if not all(math.isfinite(value) for value in values):
        return None
    if any(left > right for left, right in zip(values, values[1:])):
        return None
    return values


def _read_borders(data: bytes, catalog_start: int,
                  feature_count: int) -> dict[int, list[float]]:
    borders: dict[int, list[float]] = {}
    for offset in range(catalog_start, max(catalog_start, len(data) - 15), 4):
        marker, feature_id, item_size, count = struct.unpack_from(
            "<IIII", data, offset)
        if marker >> 16 != 0xFFFF or item_size != 4:
            continue
        if not 2 <= feature_id < feature_count:
            continue
        values = _monotonic_floats(data, offset + 16, count)
        if values is not None:
            borders[feature_id] = values

    feature_one_end = catalog_start
    for offset in range(catalog_start, max(catalog_start, len(data) - 15), 4):
        if struct.unpack_from("<III", data, offset) != (8, 1, 4):
            continue
        count = _u32(data, offset + 12)
        values = _monotonic_floats(data, offset + 16, count)
        if values is not None:
            borders[1] = values
            feature_one_end = offset + 16 + count * 4
            break

    for offset in range(feature_one_end, max(feature_one_end, len(data) - 15), 4):
        if (_u32(data, offset + 4), _u32(data, offset + 8)) != (8, 4):
            continue
        count = _u32(data, offset + 12)
        values = _monotonic_floats(data, offset + 16, count)
        if values is not None:
            borders[0] = values
            break
    return borders


def convert_model(data: bytes, *, feature_count: int,
                  expected_depth: int | None = None,
                  expected_tree_count: int | None = None,
                  output_format: str = "hobl"
                  ) -> tuple[bytes, ConversionFacts]:
    if not 1 <= feature_count <= 255:
        raise ValueError(f"invalid feature count: {feature_count}")
    trees, depth, catalog_start = _find_trees(
        data, expected_depth, expected_tree_count)
    tree_count = len(trees)
    leaf_count = 1 << depth
    borders = _read_borders(data, catalog_start, feature_count)

    flat = [(feature_id, threshold)
            for feature_id in range(feature_count)
            for threshold in borders.get(feature_id, [])]
    condition_indices: list[int] = []
    for tree in trees:
        condition_indices.extend(
            _u32(data, tree.condition_offset + condition * 4)
            for condition in range(depth))
    if not condition_indices:
        raise ValueError("empty condition catalog")
    condition_max_plus_one = max(condition_indices) + 1
    if len(flat) != condition_max_plus_one:
        raise ValueError(
            "condition/catalog invariant failed: "
            f"condition max + 1 = {condition_max_plus_one}, "
            f"flat borders = {len(flat)}")
    if any(index >= len(flat) for index in condition_indices):
        raise ValueError("condition index outside flat border catalog")

    if output_format == "hobl":
        output = bytearray(b"HOBL")
        output.extend(struct.pack("<HBBII", 2, depth, 0, tree_count,
                                  feature_count))
    elif output_format == "hrnk":
        if depth != 6:
            raise ValueError("HRNK runtime format requires depth 6")
        output = bytearray(b"HRNK")
        output.extend(struct.pack("<II", tree_count, feature_count))
    else:
        raise ValueError(f"unsupported output format {output_format!r}")
    for feature_id in range(feature_count):
        values = borders.get(feature_id, [])
        if len(values) > 65535:
            raise ValueError(f"too many borders for feature {feature_id}")
        output.extend(struct.pack("<H", len(values)))
        if values:
            output.extend(struct.pack("<" + "f" * len(values), *values))

    for tree_index, tree in enumerate(trees):
        tree_base = tree.offset
        for condition in range(depth):
            feature_id, threshold = flat[_u32(
                data, tree.condition_offset + condition * 4)]
            output.extend(struct.pack("<Bf", feature_id, threshold))
        for leaf in range(leaf_count):
            value = _f64(data, tree_base + 16 + leaf * 8)
            if not math.isfinite(value):
                raise ValueError(
                    f"non-finite leaf in tree {tree_index}, leaf {leaf}")
            if output_format == "hobl":
                output.extend(struct.pack("<d", value))
            else:
                try:
                    output.extend(struct.pack("<f", value))
                except OverflowError as error:
                    raise ValueError(
                        f"leaf outside float32 in tree {tree_index}, "
                        f"leaf {leaf}") from error

    facts = ConversionFacts(
        tree_offset=min(tree.offset for tree in trees),
        tree_count=tree_count,
        depth=depth,
        feature_count=feature_count,
        flat_border_count=len(flat),
        condition_max_plus_one=condition_max_plus_one,
        zero_border_features=[feature for feature in range(feature_count)
                              if feature not in borders],
    )
    return bytes(output), facts


def _sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--features", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--metadata-out", type=Path)
    parser.add_argument("--variant", required=True)
    parser.add_argument("--role", required=True)
    parser.add_argument("--expected-depth", type=int, required=True)
    parser.add_argument("--expected-tree-count", type=int, required=True)
    parser.add_argument("--expected-source-sha256", required=True)
    parser.add_argument("--format", choices=("hobl", "hrnk"), default="hobl")
    parser.add_argument("--experimental-structural-only",
                        action="store_true")
    args = parser.parse_args()

    if not args.experimental_structural_only:
        parser.error("--experimental-structural-only is required: original "
                     "evaluator semantics are not proven")
    source = args.source.read_bytes()
    source_sha256 = _sha256(source)
    if source_sha256.lower() != args.expected_source_sha256.lower():
        raise SystemExit(
            f"source SHA-256 mismatch: {source_sha256} != "
            f"{args.expected_source_sha256.lower()}")

    feature_count = feature_count_from_file(args.features)
    output, facts = convert_model(
        source, feature_count=feature_count,
        expected_depth=args.expected_depth,
        expected_tree_count=args.expected_tree_count,
        output_format=args.format)
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_bytes(output)
    output_sha256 = _sha256(output)

    metadata = {
        **dataclasses.asdict(facts),
        "variant": args.variant,
        "role": args.role,
        "source": str(args.source.resolve()),
        "source_sha256": source_sha256,
        "features": str(args.features.resolve()),
        "features_sha256": _sha256(args.features.read_bytes()),
        "output": str(args.out.resolve()),
        "output_sha256": output_sha256,
        "output_format": args.format,
        "production_eligible": False,
        "blocker": ("role-specific feature producers and policy gates are "
                    "not all proven"),
    }
    metadata_out = args.metadata_out or args.out.with_suffix(
        args.out.suffix + ".json")
    metadata_out.parent.mkdir(parents=True, exist_ok=True)
    metadata_out.write_text(json.dumps(metadata, ensure_ascii=False, indent=2),
                            encoding="utf-8")
    print(
        f"asset={args.source.name},contract=evaluator_exact_features_unverified,"
        f"variant={args.variant},load=converted,invoke_count=0,"
        f"scored_count=0,fallback=not_packaged,latency_us=0,"
        f"trees={facts.tree_count},depth={facts.depth},"
        f"features={facts.feature_count},borders={facts.flat_border_count},"
        f"format={args.format},"
        f"sha256={output_sha256}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
