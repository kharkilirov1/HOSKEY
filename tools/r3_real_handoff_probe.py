#!/usr/bin/env python3
"""Use real FUN_00661462 handoff keys with rank/select style recursive probing."""

from __future__ import annotations

import itertools
import struct
from pathlib import Path

TRIE_PATH = Path(r"C:\Users\Kharki\Desktop\yandex_extracted\extracted_sections\trie")
OUT_PATH = Path(r"C:\Users\Kharki\DevEcoStudioProjects\HOSKEY\reverse\R3_real_handoff_probe_2026-04-08.txt")
LEVEL0 = {"bv1_ds": 0x1FEB8, "bv2_ds": 0x47788, "ds3": 0x6D9A8, "ds4": 0x93BC8, "blob1": 0xFDB98, "intv": 0x9B5E8}
LEVEL1 = {"bv1_ds": 0x1F3118, "bv2_ds": 0x1F4C60, "ds3": 0x1F6668, "ds4": 0x1F8070, "blob1": 0x1FA530}
ROOT_IDS = [3,4,5,6,7,8]


def u64(data, off): return struct.unpack_from('<Q', data, off)[0]

def cp1251_char(buf, idx):
    if idx < 0 or idx >= len(buf): return '?'
    ch = bytes([buf[idx]]).decode('cp1251', errors='replace')
    return ch if ord(ch) >= 0x20 else '?'

class BV:
    def __init__(self, buf, shift=64): self.buf=buf; self.shift=shift; self.total_bits=len(buf)*8-shift
    def bit(self,pos):
        pos += self.shift
        if pos < 0 or pos >= len(self.buf)*8: return 0
        return (self.buf[pos>>3] >> (pos & 7)) & 1
    def rank1_exclusive(self,pos):
        return sum(self.bit(i) for i in range(pos))
    def select1(self,n):
        seen=0
        for pos in range(self.total_bits):
            if self.bit(pos):
                if seen==n: return pos
                seen+=1
        return -1

def ds_bits(data, off):
    count = u64(data, off+8)
    return data[off+24:off+24+count*8]

def parse_iv(data, off):
    bw=u64(data, off); mask=u64(data, off+8); wc=u64(data, off+24); data_off=off+0x20
    words=[u64(data, data_off+i*8) for i in range(wc)]
    return bw, mask, words

def iv_get(iv, idx):
    bw, mask, words = iv
    bitpos = idx * bw; wi = bitpos >> 6; bo = bitpos & 63
    if wi >= len(words): return 0
    val = words[wi] >> bo
    if bo + bw > 64 and wi + 1 < len(words): val |= words[wi+1] << (64 - bo)
    return val & mask

def fun_00661462(iv, blob1, node_id, rank_index):
    return (iv_get(iv, rank_index) << 8) | blob1[node_id]

data = TRIE_PATH.read_bytes()
root_blob = data[LEVEL0['blob1']:LEVEL0['blob1']+64]
iv0 = parse_iv(data, LEVEL0['intv'])
root_terms = {n: BV(ds_bits(data, LEVEL0[n])) for n in ('bv1_ds','bv2_ds','ds3','ds4')}
lvl1_bvs = {n: BV(ds_bits(data, LEVEL1[n])) for n in ('bv1_ds','bv2_ds','ds3','ds4')}
lvl1_blob = data[LEVEL1['blob1']:LEVEL1['blob1']+512]

rows=[]
for root_term_name, root_term in root_terms.items():
    keys=[]; source=[]
    for nid in ROOT_IDS:
        if root_term.bit(nid)==0: continue
        rank = root_term.rank1_exclusive(nid)
        key = fun_00661462(iv0, root_blob, nid, rank)
        keys.append(key)
        source.append((nid, cp1251_char(root_blob, nid), key))
    if not keys: continue
    for hc_name, loud_name in itertools.permutations(lvl1_bvs.keys(), 2):
        hc=lvl1_bvs[hc_name]; louds=lvl1_bvs[loud_name]
        for mode in (0,1):
            chars=[]; hits=0; cyr=0
            for key in keys:
                r = hc.rank1_exclusive(key)
                child = louds.select1(r - 1 + mode)
                ch = cp1251_char(lvl1_blob, child)
                chars.append(ch)
                if ch!='?':
                    hits += 1
                    if 'а' <= ch <= 'я' or ch=='ё': cyr += 1
            if hits:
                score = cyr*10 + hits
                rows.append((score, root_term_name, hc_name, loud_name, mode, source, chars))

rows.sort(reverse=True)
lines=[]
lines.append(f'TRIE_PATH={TRIE_PATH}')
lines.append('Probe: use real level-0 FUN_00661462 outputs as level-1 keys with rank/select recursion model')
lines.append('')
for score, root_term_name, hc_name, loud_name, mode, source, chars in rows[:40]:
    lines.append(f"score={score:3d} root_term={root_term_name:6s} hc={hc_name:6s} louds={loud_name:6s} mode={mode} source={[(nid,ch,hex(key)) for nid,ch,key in source]} -> {chars}")
if rows:
    best=rows[0]
    lines.append('')
    lines.append('Best current real-key candidate:')
    lines.append(f"root_term={best[1]} hc={best[2]} louds={best[3]} mode={best[4]} source={[(nid,ch,hex(key)) for nid,ch,key in best[5]]} -> {best[6]}")
else:
    lines.append('No candidates even under rank/select probing.')
lines.append('')
lines.append('Takeaway:')
lines.append('- this removes the fake delta transform and feeds actual FUN_00661462 outputs into the recursive approximation')
lines.append('- if clean candidates are absent here too, then either the root terminal family is still wrong or another mapping layer exists before recursive lookup')
report='\n'.join(lines)+'\n'
OUT_PATH.write_text(report, encoding='utf-8')
print(report)
print(f'written to {OUT_PATH}')
