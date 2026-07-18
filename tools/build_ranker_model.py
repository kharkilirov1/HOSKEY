"""Decode Yandex MatrixNet blobs into HOSKEY's compact ``HRNK`` format.

The same raw layout is used by the normal ranker and by the swipe,
swipe-backoff, and swipe-blocker models:

* fixed 560-byte depth-6 oblivious-tree records;
* a feature-border catalog following the tree records;
* six condition indices per tree into the flattened border catalog.

The older converter hard-coded offsets from the 44-feature normal ranker.  This
version discovers the catalog and therefore also handles the 47/42/31-feature
swipe models.  A successful conversion proves the strongest structural
invariant available in the blob: ``flat_border_count == max(condition)+1``.

Environment variables:
  RANKER_SRC       raw Yandex model blob
  RANKER_FEATURES  matching feature JSON
  RANKER_OUT       output HRNK path
"""

from __future__ import annotations

import json
import math
import os
import struct
from pathlib import Path


YANDEX = Path("C:/Users/Kharki/Desktop/yandex_extracted")
SRC = Path(os.environ.get(
    "RANKER_SRC", YANDEX / "extracted_sections/KEYBOARD-13421_ranker"))
FEATURES = Path(os.environ.get(
    "RANKER_FEATURES", YANDEX / "extracted_sections/features_ranker.json"))
OUT = Path(os.environ.get(
    "RANKER_OUT", YANDEX / "ms_models/ranker_model.bin"))

d = SRC.read_bytes()


def u32(offset: int) -> int:
    return struct.unpack_from("<I", d, offset)[0]


def f32(offset: int) -> float:
    return struct.unpack_from("<f", d, offset)[0]


def f64(offset: int) -> float:
    return struct.unpack_from("<d", d, offset)[0]


def feature_count(path: Path) -> int:
    payload = json.loads(path.read_text(encoding="utf-8"))
    if isinstance(payload.get("Features"), list):
        return len(payload["Features"]) + len(payload.get("Stats", [])) + \
            len(payload.get("Combos", []))

    # features_ranker.json groups the base and keyboard-specific catalogs.
    count = 0
    for section in payload.values():
        if not isinstance(section, dict):
            continue
        count += len(section.get("Features", []))
        count += len(section.get("Combos", []))
        count += len(section.get("Stats", []))
    return count


def monotonic_floats(offset: int, count: int) -> list[float] | None:
    end = offset + 4 * count
    if count <= 0 or count > 4096 or end > len(d):
        return None
    values = [f32(offset + 4 * i) for i in range(count)]
    if not all(math.isfinite(value) for value in values):
        return None
    if any(values[i] > values[i + 1] for i in range(len(values) - 1)):
        return None
    return values


n_features = feature_count(FEATURES)
assert 1 <= n_features <= 255, f"invalid feature count: {n_features}"

# Every tree begins with [*, 528, 4, 64], followed by 64 f64 leaves and six
# u32 condition indices.  Adjacent tree records are exactly 560 bytes apart.
tree_offsets = [
    offset for offset in range(0, len(d) - 16, 4)
    if u32(offset + 4) == 528 and u32(offset + 8) == 4 and
    u32(offset + 12) == 64
]
assert tree_offsets, "no MatrixNet tree records"
assert all(b - a == 560 for a, b in zip(tree_offsets, tree_offsets[1:])), \
    "non-contiguous MatrixNet tree records"
tree_end = tree_offsets[-1] + 560

# Features N-1..2 use a regular [0xFFFFxxxx, feature_id, 4, count] header.
borders: dict[int, list[float]] = {}
for offset in range(tree_end, len(d) - 16, 4):
    marker, feature_id, item_size, count = (
        u32(offset), u32(offset + 4), u32(offset + 8), u32(offset + 12))
    if marker >> 16 != 0xFFFF or item_size != 4:
        continue
    if not 2 <= feature_id < n_features:
        continue
    values = monotonic_floats(offset + 16, count)
    if values is not None:
        borders[feature_id] = values

# Feature 1 has the stable irregular [8, 1, 4, count] header.
feature_one_end = tree_end
for offset in range(tree_end, len(d) - 16, 4):
    if (u32(offset), u32(offset + 4), u32(offset + 8)) != (8, 1, 4):
        continue
    count = u32(offset + 12)
    values = monotonic_floats(offset + 16, count)
    if values is not None:
        borders[1] = values
        feature_one_end = offset + 16 + 4 * count
        break

# Feature 0 follows feature 1 and uses [opaque, 8, 4, count].
for offset in range(feature_one_end, len(d) - 16, 4):
    if u32(offset + 4) != 8 or u32(offset + 8) != 4:
        continue
    count = u32(offset + 12)
    values = monotonic_floats(offset + 16, count)
    if values is not None:
        borders[0] = values
        break

missing = [feature_id for feature_id in range(n_features)
           if feature_id not in borders]
assert not missing, f"missing border blocks: {missing}"

flat = [(feature_id, threshold)
        for feature_id in range(n_features)
        for threshold in borders[feature_id]]
condition_indices = [
    u32(tree_offset + 528 + 8 + 4 * condition)
    for tree_offset in tree_offsets
    for condition in range(6)
]
condition_max = max(condition_indices)
assert len(flat) == condition_max + 1, \
    f"INVARIANT FAIL {len(flat)} != {condition_max + 1}"

out = bytearray(b"HRNK")
out += struct.pack("<II", len(tree_offsets), n_features)
for feature_id in range(n_features):
    out += struct.pack("<H", len(borders[feature_id]))
    for threshold in borders[feature_id]:
        out += struct.pack("<f", threshold)
for tree_offset in tree_offsets:
    for condition in range(6):
        feature_id, threshold = flat[
            u32(tree_offset + 528 + 8 + 4 * condition)]
        out += struct.pack("<Bf", feature_id, threshold)
    for leaf in range(64):
        out += struct.pack("<f", f64(tree_offset + 16 + 8 * leaf))

OUT.parent.mkdir(parents=True, exist_ok=True)
OUT.write_bytes(out)
print(
    f"source={SRC.name} trees={len(tree_offsets)} features={n_features} "
    f"borders={len(flat)} condmax+1={condition_max + 1} INVARIANT OK")
print(f"wrote {OUT} ({len(out)} bytes)")
