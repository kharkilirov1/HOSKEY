"""Decode the reverse-engineered Yandex MatrixNet ranker (KEYBOARD-13421_ranker)
into HOSKEY's compact 'HRNK' format for the on-device evaluator.

Witness of a correct decode: flat border-table length == max tree condition
index + 1 (== 2274 for this asset).

Output (per tree, 1000 depth-6 oblivious trees):
  'HRNK', u32 n_trees, u32 44,
  per feature f in 0..43: u16 n_borders, n_borders*f32   (border table)
  per tree: 6 x (u8 feature_id, f32 threshold) + 64 x f32 leaf
"""
import struct, sys, os

SRC = os.environ.get(
    "RANKER_SRC",
    "C:/Users/Kharki/Desktop/yandex_extracted/extracted_sections/KEYBOARD-13421_ranker")
OUT = os.environ.get(
    "RANKER_OUT",
    "C:/Users/Kharki/Desktop/yandex_extracted/ms_models/ranker_model.bin")

d = open(SRC, "rb").read()
def u32(o): return struct.unpack_from("<I", d, o)[0]
def u16(o): return struct.unpack_from("<H", d, o)[0]
def f32(o): return struct.unpack_from("<f", d, o)[0]
def f64(o): return struct.unpack_from("<d", d, o)[0]

# 1000 fixed 560-byte records anchored by the [*,528,4,64] signature.
sig = [o for o in range(0, len(d) - 16, 4)
       if u32(o + 4) == 528 and u32(o + 8) == 4 and u32(o + 12) == 64]
T = sig[-1] + 560   # border-table base

# Feature border blocks. feat 43..2 via the 0xFFFF-marker walk from T+180.
borders = {}
o = T + 180
while o < len(d) - 16:
    if (u32(o) >> 16) != 0xFFFF:
        break
    fid, c, n = u32(o + 4), u32(o + 8), u32(o + 12)
    if c != 4 or n > 4096:
        break
    borders[fid] = [f32(o + 16 + 4 * k) for k in range(n)]
    o = o + 16 + 4 * n
# feat0 @ T+9944 ([.,.,4,n]+16), feat1 @ T+9692 (irregular tail; header [8][1][4][n]).
o0 = T + 9944
assert u32(o0 + 8) == 4, "feat0 header"
borders[0] = [f32(o0 + 16 + 4 * k) for k in range(u32(o0 + 12))]
assert u32(T + 9680) == 1 and u32(T + 9684) == 4, "feat1 header"
n1 = u32(T + 9688)
borders[1] = [f32(T + 9692 + 4 * k) for k in range(n1)]

counts = [len(borders[f]) for f in range(44)]
flat = [(f, t) for f in range(44) for t in borders[f]]
condmax = max(u32(ro + 528 + 8 + 4 * c) for ro in sig for c in range(6))
assert len(flat) == condmax + 1, f"INVARIANT FAIL {len(flat)} != {condmax + 1}"
print(f"trees={len(sig)} borders={len(flat)} condmax+1={condmax + 1} INVARIANT OK")

out = bytearray(b"HRNK")
out += struct.pack("<II", len(sig), 44)
for f in range(44):
    out += struct.pack("<H", len(borders[f]))
    for t in borders[f]:
        out += struct.pack("<f", t)
for ro in sig:
    for c in range(6):
        f, t = flat[u32(ro + 528 + 8 + 4 * c)]
        out += struct.pack("<Bf", f, t)
    for i in range(64):
        out += struct.pack("<f", f64(ro + 16 + 8 * i))
os.makedirs(os.path.dirname(OUT), exist_ok=True)
open(OUT, "wb").write(out)
print(f"wrote {OUT} ({len(out)} bytes)")
