"""Strict offline validators for the two distinct recovered emoji assets.

`search_emoji_model` is a FlatBuffer mapping/trie payload plus groups of emoji
strings.  `emoji_model` is a `ynnlm_v1` wrapper containing a ranked 1752-item
catalog and a state-to-softmax TFLite decoder.  Validation here proves those
byte contracts only; it deliberately does not fabricate the stripped native
query mapping or the missing raw-text-to-state chain.
"""

from __future__ import annotations

import argparse
import bisect
import dataclasses
import hashlib
import json
import struct
from pathlib import Path


@dataclasses.dataclass(frozen=True)
class ContextEmojiWrapper:
    order: int
    config: dict
    catalog_offset: int
    tflite_offset: int
    catalog: list[str]
    rank_to_lex: list[int]
    lex_to_rank: list[int]

    def ranked_emoji(self) -> list[str]:
        return [self.catalog[index] for index in self.rank_to_lex]


@dataclasses.dataclass(frozen=True)
class SearchEmojiMapping:
    opaque_payload_size: int
    groups: list[list[str]]
    entries: list[tuple[bytes, int]]

    def canonical_sha256(self) -> str:
        canonical = bytearray()
        for group in self.groups:
            canonical.extend(struct.pack("<I", len(group)))
            for item in group:
                encoded = item.encode("utf-8")
                canonical.extend(struct.pack("<I", len(encoded)))
                canonical.extend(encoded)
        return hashlib.sha256(canonical).hexdigest()

    def search(self, query: str, max_results: int = 64,
               max_entries: int = 200) -> list[str]:
        """Run the recovered native prefix route over the compact trie.

        Keys are serialized UTF-16BE because the native `TCompactTrie<wchar16>`
        consumes the most-significant byte of every code unit first.  Leaves
        are signed prefix-varints whose non-negative values index `groups`.
        """
        normalized = query.strip().lower()
        if not normalized or max_results <= 0 or max_entries <= 0:
            return []
        prefix = normalized.encode("utf-16-be")
        index = bisect.bisect_left(self.entries, (prefix, -(1 << 63)))
        seen_groups: set[int] = set()
        seen_emoji: set[str] = set()
        result: list[str] = []
        examined = 0
        while index < len(self.entries) and examined < max_entries:
            key, group_index = self.entries[index]
            if not key.startswith(prefix):
                break
            examined += 1
            index += 1
            if group_index in seen_groups:
                continue
            seen_groups.add(group_index)
            for emoji in self.groups[group_index]:
                if emoji in seen_emoji:
                    continue
                seen_emoji.add(emoji)
                result.append(emoji)
                if len(result) >= max_results:
                    return result
        return result


def _require(data: bytes, offset: int, size: int, label: str) -> None:
    if offset < 0 or size < 0 or offset > len(data) or size > len(data) - offset:
        raise ValueError(f"{label} out of bounds: offset={offset}, size={size}")


def _u16(data: bytes, offset: int) -> int:
    _require(data, offset, 2, "u16")
    return struct.unpack_from("<H", data, offset)[0]


def _u32(data: bytes, offset: int) -> int:
    _require(data, offset, 4, "u32")
    return struct.unpack_from("<I", data, offset)[0]


def _i32(data: bytes, offset: int) -> int:
    _require(data, offset, 4, "i32")
    return struct.unpack_from("<i", data, offset)[0]


def _table_field(data: bytes, table: int, index: int) -> int | None:
    vtable = table - _i32(data, table)
    _require(data, vtable, 4, "FlatBuffer vtable")
    vtable_size = _u16(data, vtable)
    object_size = _u16(data, vtable + 2)
    if vtable_size < 4 or vtable_size % 2 != 0:
        raise ValueError("invalid FlatBuffer vtable size")
    _require(data, vtable, vtable_size, "FlatBuffer vtable")
    _require(data, table, object_size, "FlatBuffer table")
    entry = vtable + 4 + index * 2
    if entry + 2 > vtable + vtable_size:
        return None
    relative = _u16(data, entry)
    if relative == 0:
        return None
    if relative >= object_size:
        raise ValueError("FlatBuffer field offset outside object")
    return table + relative


def _uoffset_target(data: bytes, position: int, label: str) -> int:
    target = position + _u32(data, position)
    _require(data, target, 4, label)
    return target


def _vector(data: bytes, field: int, label: str) -> tuple[int, int]:
    vector = _uoffset_target(data, field, label)
    count = _u32(data, vector)
    start = vector + 4
    return start, count


def _u32_vector(data: bytes, field: int, label: str) -> list[int]:
    start, count = _vector(data, field, label)
    _require(data, start, count * 4, label)
    if count == 0:
        return []
    return list(struct.unpack_from("<" + "I" * count, data, start))


def _string_from_slot(data: bytes, slot: int, label: str) -> str:
    string = _uoffset_target(data, slot, label)
    length = _u32(data, string)
    _require(data, string + 4, length + 1, label)
    raw = data[string + 4:string + 4 + length]
    if data[string + 4 + length] != 0:
        raise ValueError(f"{label} is not NUL terminated")
    try:
        return raw.decode("utf-8")
    except UnicodeDecodeError as error:
        raise ValueError(f"{label} is not UTF-8") from error


def _string_vector(data: bytes, field: int, label: str) -> list[str]:
    start, count = _vector(data, field, label)
    _require(data, start, count * 4, label)
    return [_string_from_slot(data, start + index * 4,
                              f"{label}[{index}]")
            for index in range(count)]


def parse_context_wrapper(data: bytes) -> ContextEmojiWrapper:
    _require(data, 0, 16, "context wrapper header")
    if data[:8] != b"ynnlm_v1":
        raise ValueError("context wrapper magic mismatch")
    order = _u32(data, 8)
    if order != 10 or _u32(data, 12) != 0:
        raise ValueError("context wrapper order/reserved contract mismatch")
    config_end = data.find(b"\0", 16)
    if config_end < 0:
        raise ValueError("context wrapper JSON terminator missing")
    try:
        config = json.loads(data[16:config_end].decode("utf-8"))
    except (UnicodeDecodeError, json.JSONDecodeError) as error:
        raise ValueError("context wrapper JSON invalid") from error
    if config.get("Order") != order:
        raise ValueError("context wrapper JSON/header order mismatch")
    catalog_offset = (config_end + 15) & ~15
    _require(data, catalog_offset, 4, "context catalog root")

    tfl3 = []
    position = catalog_offset
    while True:
        magic = data.find(b"TFL3", position)
        if magic < 0:
            break
        if magic >= 4:
            tfl3.append(magic - 4)
        position = magic + 4
    if len(tfl3) != 1:
        raise ValueError(f"expected one embedded TFLite graph, got {tfl3}")
    tflite_offset = tfl3[0]
    if tflite_offset <= catalog_offset:
        raise ValueError("embedded TFLite overlaps context catalog")

    root = catalog_offset + _u32(data, catalog_offset)
    _require(data, root, 4, "context catalog table")
    rank_field = _table_field(data, root, 3)
    inverse_field = _table_field(data, root, 2)
    strings_field = _table_field(data, root, 1)
    if rank_field is None or inverse_field is None or strings_field is None:
        raise ValueError("context catalog required field missing")
    rank_to_lex = _u32_vector(data, rank_field, "rank_to_lex")
    lex_to_rank = _u32_vector(data, inverse_field, "lex_to_rank")
    catalog = _string_vector(data, strings_field, "emoji catalog")
    count = len(catalog)
    if count == 0 or len(rank_to_lex) != count or len(lex_to_rank) != count:
        raise ValueError("context catalog vector lengths disagree")
    expected = list(range(count))
    if sorted(rank_to_lex) != expected or sorted(lex_to_rank) != expected:
        raise ValueError("context catalog maps are not permutations")
    if any(lex_to_rank[rank_to_lex[index]] != index for index in expected):
        raise ValueError("context catalog maps are not inverses")
    if len(set(catalog)) != count:
        raise ValueError("context emoji catalog contains duplicates")
    return ContextEmojiWrapper(order, config, catalog_offset, tflite_offset,
                               catalog, rank_to_lex, lex_to_rank)


def parse_search_mapping(data: bytes) -> SearchEmojiMapping:
    _require(data, 0, 20, "search mapping root")
    root = _u32(data, 0)
    _require(data, root, 4, "search mapping table")
    groups_field = _table_field(data, root, 0)
    payload_field = _table_field(data, root, 1)
    if groups_field is None or payload_field is None:
        raise ValueError("search mapping required field missing")

    payload_start, payload_count = _vector(data, payload_field,
                                           "search compact trie")
    _require(data, payload_start, payload_count, "search opaque payload")
    groups_start, group_count = _vector(data, groups_field,
                                         "search groups")
    _require(data, groups_start, group_count * 4, "search groups")
    groups: list[list[str]] = []
    for group_index in range(group_count):
        slot = groups_start + group_index * 4
        table = _uoffset_target(data, slot, f"search group[{group_index}]")
        strings_field = _table_field(data, table, 0)
        if strings_field is None:
            raise ValueError(f"search group[{group_index}] has no strings")
        group = _string_vector(data, strings_field,
                               f"search group[{group_index}]")
        if not group:
            raise ValueError(f"search group[{group_index}] is empty")
        groups.append(group)
    trie = data[payload_start:payload_start + payload_count]

    def unpack_offset(position: int, length: int) -> int:
        _require(trie, position, length, "compact trie offset")
        value = 0
        for byte in trie[position:position + length]:
            value = (value << 8) | byte
        return value

    def leaf_size(first: int) -> int:
        # Same table used by the native NPackers signed integral packer:
        # 0xxxxxxx, 10xxxxxx x, 110xxxxx xx, ... 11111111 xxxxxxxx.
        if first < 0x80:
            return 1
        leading_ones = 0
        mask = 0x80
        while first & mask:
            leading_ones += 1
            mask >>= 1
        return min(leading_ones + 1, 9)

    def unpack_leaf(position: int) -> tuple[int, int]:
        _require(trie, position, 1, "compact trie leaf")
        length = leaf_size(trie[position])
        _require(trie, position, length, "compact trie leaf")
        value = trie[position] & (0x7F >> (length - 1))
        for byte in trie[position + 1:position + length]:
            value = (value << 8) | byte
        decoded = value >> 1
        if value & 1:
            decoded = -decoded
        return decoded, length

    entries: list[tuple[bytes, int]] = []
    node_count = 0

    def walk(position: int, prefix: bytes) -> None:
        nonlocal node_count
        _require(trie, position, 1, "compact trie node")
        node_count += 1
        if node_count > len(trie):
            raise ValueError("compact trie node count exceeds payload size")
        start = position
        flags = trie[position]
        position += 1
        if flags & 0xC0 == 0:  # minimized-DAG epsilon link
            length = flags & 0x07
            offset = unpack_offset(position, length)
            if offset == 0 or start + offset <= start:
                raise ValueError("invalid compact trie epsilon link")
            walk(start + offset, prefix)
            return

        _require(trie, position, 1, "compact trie label")
        label = trie[position]
        position += 1
        left_length = (flags >> 3) & 0x07
        left_offset = unpack_offset(position, left_length)
        position += left_length
        right_length = flags & 0x07
        right_offset = unpack_offset(position, right_length)
        position += right_length

        if left_offset:
            if start + left_offset <= start:
                raise ValueError("invalid compact trie left link")
            walk(start + left_offset, prefix)

        key = prefix + bytes((label,))
        if flags & 0x80:
            group_index, length = unpack_leaf(position)
            if group_index < 0 or group_index >= len(groups):
                raise ValueError(
                    f"compact trie group index out of range: {group_index}")
            entries.append((key, group_index))
            position += length
        if flags & 0x40:
            walk(position, key)
        if right_offset:
            if start + right_offset <= start:
                raise ValueError("invalid compact trie right link")
            walk(start + right_offset, prefix)

    walk(0, b"")
    if any(len(key) % 2 for key, _ in entries):
        raise ValueError("compact trie contains an odd UTF-16BE key")
    if any(entries[index - 1][0] >= entries[index][0]
           for index in range(1, len(entries))):
        raise ValueError("compact trie keys are not strictly ordered")
    return SearchEmojiMapping(payload_count, groups, entries)


def _sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--search", type=Path, required=True)
    parser.add_argument("--context", type=Path, required=True)
    parser.add_argument("--json-out", type=Path)
    args = parser.parse_args()

    search_data = args.search.read_bytes()
    context_data = args.context.read_bytes()
    search = parse_search_mapping(search_data)
    context = parse_context_wrapper(context_data)
    report = {
        "search": {
            "source": str(args.search.resolve()),
            "sha256": _sha256(search_data),
            "opaque_payload_size": search.opaque_payload_size,
            "groups": len(search.groups),
            "references": sum(len(group) for group in search.groups),
            "unique_emoji": len({item for group in search.groups
                                 for item in group}),
            "query_keys": len(search.entries),
            "canonical_catalog_sha256": search.canonical_sha256(),
            "production_eligible": True,
            "route": "utf16be_comptrie_prefix_to_signed_group_index",
        },
        "context": {
            "source": str(args.context.resolve()),
            "sha256": _sha256(context_data),
            "order": context.order,
            "catalog_offset": context.catalog_offset,
            "tflite_offset": context.tflite_offset,
            "catalog_size": len(context.catalog),
            "tflite_sha256": _sha256(context_data[context.tflite_offset:]),
            "top10": context.ranked_emoji()[:10],
            "production_eligible": False,
            "blocker": "matching encoder state[4000]+hidden[500] chain absent",
        },
    }
    if args.json_out:
        args.json_out.parent.mkdir(parents=True, exist_ok=True)
        args.json_out.write_text(json.dumps(report, ensure_ascii=False,
                                             indent=2), encoding="utf-8")
    print(
        "asset=search_emoji_model,contract=valid,variant=default,"
        "load=validated,invoke_count=0,scored_count=0,"
        "fallback=not_invoked:offline_validator,latency_us=0,"
        f"keys={len(search.entries)},groups={len(search.groups)},unique="
        f"{len({item for group in search.groups for item in group})}")
    print(
        "asset=emoji_model,contract=unsupported:missing_text_to_state_chain,"
        "variant=default,load=decoder_validated,invoke_count=0,"
        "scored_count=0,fallback=blocked:encoder_state,latency_us=0,"
        f"catalog={len(context.catalog)},tflite_offset="
        f"{context.tflite_offset}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
