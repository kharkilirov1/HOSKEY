#!/usr/bin/env python3
"""Audit the current 1nc7 candidate materializer against reversed Yandex flow.

This is intentionally a reverse-engineering tool, not a dictionary fallback.
It parses the extracted Yandex `1nc7` trie sections and documents where the
runtime approximation still diverges from the decompiled iterator:

  FUN_00661412 -> FUN_0066242c -> FUN_00662502       (depth-0/root phase)
  FUN_00661926 -> FUN_00661b66/FUN_00661baa          (recursive phase)
  FUN_00661462                                      (terminal handoff key)

The output report is used as a guardrail so we do not "fix" bad suggestions by
silently returning to a flat word-list predictor.
"""

from __future__ import annotations

import json
import struct
from collections import defaultdict, deque
from pathlib import Path
from typing import Iterable


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_TRIE_PATH = Path(
    r"C:\Users\Kharki\Desktop\yandex_extracted\extracted_sections\trie"
)
OUT_MD = ROOT / "reverse" / "R3_YANDEX_MATERIALIZER_AUDIT_2026-05-01.md"
OUT_JSON = ROOT / "reverse" / "R3_YANDEX_MATERIALIZER_AUDIT_2026-05-01.json"

OFF = {
    "has_child": 0x000008,      # +0x10 raw bits source for FUN_00661412 test
    "louds": 0x0278D8,          # +0x78 raw bits source for FUN_00661646/select
    "terminal": 0x0753C8,       # +0x120 terminal raw bits source
    "terminal_ds": 0x093BC8,    # +0x128 rank DS for terminal rank
    "intvector": 0x09B5E8,      # +0x160/+0x168/+0x170 upper bits
    "labels": 0x0FDB98,        # +0x178 low-byte labels
}

SAMPLE_PREFIXES = ["ла", "пр", "про", "ст", "не", "хор", "она"]
MAX_BRANCH_KIDS = 64
MAX_GRAPH_DEPTH = 5


def u64(data: bytes, off: int) -> int:
    return struct.unpack_from("<Q", data, off)[0]


class RawBitVector:
    def __init__(self, data: bytes, off: int) -> None:
        self.off = off
        self.word_count = u64(data, off + 8)
        self.data_off = off + 0x10
        self.buf = data[self.data_off : self.data_off + self.word_count * 8]

    def qword(self, idx: int) -> int:
        if idx < 0 or idx >= self.word_count:
            return 0
        return struct.unpack_from("<Q", self.buf, idx * 8)[0]

    def bit(self, pos: int) -> int:
        if pos < 0 or pos >= self.word_count * 64:
            return 0
        return (self.buf[pos >> 3] >> (pos & 7)) & 1

    def rank1(self, pos: int) -> int:
        if pos <= 0:
            return 0
        full_words = min(pos >> 6, self.word_count)
        rem = pos & 63
        total = sum(self.qword(i).bit_count() for i in range(full_words))
        if full_words < self.word_count and rem:
            total += (self.qword(full_words) & ((1 << rem) - 1)).bit_count()
        return total

    def select1(self, n: int) -> int:
        if n < 0:
            return -1
        seen = 0
        for wi in range(self.word_count):
            word = self.qword(wi)
            count = word.bit_count()
            if seen + count <= n:
                seen += count
                continue
            for bi in range(64):
                if (word >> bi) & 1:
                    if seen == n:
                        return wi * 64 + bi
                    seen += 1
        return -1


class IntVector:
    def __init__(self, data: bytes, off: int) -> None:
        self.off = off
        self.bitwidth = u64(data, off)
        self.mask = u64(data, off + 8)
        self.total_bits = u64(data, off + 16)
        self.word_count = u64(data, off + 24)
        self.data_off = off + 0x20
        self.words = [
            u64(data, self.data_off + i * 8) for i in range(self.word_count)
        ]

    def get(self, idx: int) -> int:
        if self.bitwidth == 0:
            return 0
        bitpos = idx * self.bitwidth
        wi = bitpos >> 6
        bo = bitpos & 63
        if wi >= len(self.words):
            return 0
        value = self.words[wi] >> bo
        if bo + self.bitwidth > 64 and wi + 1 < len(self.words):
            value |= self.words[wi + 1] << (64 - bo)
        return value & self.mask


def cp1251_char(label_blob: bytes, idx: int) -> str:
    if idx < 0 or idx >= len(label_blob):
        return "?"
    ch = bytes([label_blob[idx]]).decode("cp1251", errors="replace")
    if ch == "\x00":
        return "∅"
    return ch if ord(ch) >= 0x20 else f"\\x{ord(ch):02X}"


def is_cyr(ch: str) -> bool:
    return ("а" <= ch <= "я") or ch == "ё"


def first_child(has_child: RawBitVector, louds: RawBitVector, node_id: int) -> int:
    if has_child.bit(node_id) == 0:
        return -1
    pos = louds.select1(has_child.rank1(node_id))
    return pos + 1 if pos >= 0 else -1


def children(has_child: RawBitVector, louds: RawBitVector, node_id: int) -> list[int]:
    start = first_child(has_child, louds, node_id)
    if start < 0:
        return []
    out: list[int] = []
    node = start
    for _ in range(MAX_BRANCH_KIDS):
        out.append(node)
        if louds.bit(node):
            break
        node += 1
    return out


def build_visible_graph(
    has_child: RawBitVector,
    louds: RawBitVector,
    terminal: RawBitVector,
    labels: bytes,
) -> dict[str, list[tuple[int, int, int]]]:
    graph: dict[str, list[tuple[int, int, int]]] = defaultdict(list)
    q: deque[tuple[str, int, int]] = deque()
    for node in children(has_child, louds, 0):
        ch = cp1251_char(labels, node)
        if is_cyr(ch):
            q.append((ch, node, 1))
    seen: set[tuple[str, int, int]] = set()
    while q:
        prefix, node, depth = q.popleft()
        if (prefix, node, depth) in seen:
            continue
        seen.add((prefix, node, depth))
        graph[prefix].append((node, terminal.bit(node), has_child.bit(node)))
        if depth >= MAX_GRAPH_DEPTH:
            continue
        for child in children(has_child, louds, node):
            ch = cp1251_char(labels, child)
            if is_cyr(ch):
                q.append((prefix + ch, child, depth + 1))
    return graph


def walk_prefix(
    prefix: str,
    has_child: RawBitVector,
    louds: RawBitVector,
    terminal: RawBitVector,
    labels: bytes,
) -> list[tuple[int, int, int]]:
    states = [0]
    for ch in prefix:
        next_states: list[int] = []
        for node in states:
            for child in children(has_child, louds, node):
                if cp1251_char(labels, child) == ch:
                    next_states.append(child)
        states = next_states
    return [(node, terminal.bit(node), has_child.bit(node)) for node in sorted(set(states))]


def current_root_dfs_candidates(
    prefix: str,
    has_child: RawBitVector,
    louds: RawBitVector,
    terminal: RawBitVector,
    labels: bytes,
    limit: int = 10,
    max_extra: int = 4,
) -> list[dict[str, object]]:
    """Approximate the existing root-level DFS mistake for audit examples."""
    out: list[dict[str, object]] = []
    start = walk_prefix(prefix, has_child, louds, terminal, labels)
    q: deque[tuple[int, str, int, list[int]]] = deque()
    for node, term, _ in start:
        if term:
            out.append(
                {
                    "text": prefix,
                    "terminal_node": node,
                    "path": [node],
                    "audit": "terminal would be epsilon/handoff in FUN_0066242c/FUN_00661926, not a confirmed word",
                }
            )
        for child in children(has_child, louds, node):
            ch = cp1251_char(labels, child)
            if is_cyr(ch):
                q.append((child, prefix + ch, 1, [node, child]))

    while q and len(out) < limit:
        node, text, depth, path = q.popleft()
        if terminal.bit(node):
            out.append(
                {
                    "text": text,
                    "terminal_node": node,
                    "path": path,
                    "audit": "current DFS would emit this terminal; Yandex iterator treats terminal nodes as handoff edges",
                }
            )
        if depth >= max_extra:
            continue
        for child in children(has_child, louds, node):
            ch = cp1251_char(labels, child)
            if is_cyr(ch):
                q.append((child, text + ch, depth + 1, path + [child]))
    return out[:limit]


def format_state_rows(
    rows: Iterable[tuple[int, int, int]],
    has_child: RawBitVector,
    louds: RawBitVector,
    terminal: RawBitVector,
    labels: bytes,
) -> list[dict[str, object]]:
    out: list[dict[str, object]] = []
    for node, term, has in rows:
        clean_kids = [
            cp1251_char(labels, child)
            for child in children(has_child, louds, node)
            if is_cyr(cp1251_char(labels, child))
        ]
        out.append(
            {
                "node": node,
                "terminal": bool(term),
                "has_child": bool(has),
                "kids": "".join(clean_kids[:40]),
            }
        )
    return out


def main() -> None:
    trie_path = DEFAULT_TRIE_PATH
    if not trie_path.exists():
        raise SystemExit(f"missing extracted trie: {trie_path}")

    data = trie_path.read_bytes()
    has_child = RawBitVector(data, OFF["has_child"])
    louds = RawBitVector(data, OFF["louds"])
    terminal = RawBitVector(data, OFF["terminal"])
    intvector = IntVector(data, OFF["intvector"])
    labels = data[OFF["labels"] : OFF["labels"] + 500_000]

    graph = build_visible_graph(has_child, louds, terminal, labels)
    ambiguous = [
        (prefix, sorted(set(rows)))
        for prefix, rows in graph.items()
        if len(set(rows)) > 1
    ]
    ambiguous.sort(key=lambda x: (len(x[0]), x[0]))

    samples: dict[str, object] = {}
    for prefix in SAMPLE_PREFIXES:
        states = sorted(set(graph.get(prefix) or walk_prefix(prefix, has_child, louds, terminal, labels)))
        samples[prefix] = {
            "states": format_state_rows(states, has_child, louds, terminal, labels),
            "current_root_dfs_terminals": current_root_dfs_candidates(
                prefix, has_child, louds, terminal, labels
            ),
        }

    # The known "ла" split is the smallest clear proof that visible prefix
    # materialization is active-state based and terminal nodes are not words.
    la_states = sorted(set(graph.get("ла", [])))
    la_terminal_keys = []
    for node, term, _ in la_states:
        if term:
            rank = terminal.rank1(node)
            upper = intvector.get(rank)
            low = labels[node]
            la_terminal_keys.append(
                {
                    "node": node,
                    "terminal_rank": rank,
                    "upper": upper,
                    "key_hex": f"0x{((upper << 8) | low):X}",
                    "low_cp1251": cp1251_char(labels, node),
                }
            )

    current_cpp = ROOT / "entry" / "src" / "main" / "cpp" / "hosdict_engine" / "one_nc7_trie.cpp"
    cpp_text = current_cpp.read_text(encoding="utf-8")
    code_findings = {
        "direct_all_level_handoff_loop": "for (size_t li = 1; li < level_views_.size(); ++li)" in cpp_text,
        "records_terminal_frame_text": "results.push_back({frame.text" in cpp_text,
        "has_suffix_collector": "CollectSuffixCompletions" in cpp_text,
        "ported_fun_00661926_stepper": "StepRecursiveMaterializer" in cpp_text,
        "ported_root_terminal_handoff": "MaterializeRootTerminalContinuation" in cpp_text,
        "clean_iterator_end_status": "MaterializerStepStatus" in cpp_text,
        "terminal_root_not_queued_as_visible": "state.has_child && !state.terminal" in cpp_text,
    }
    iterator_complete_ported = (
        not code_findings["records_terminal_frame_text"]
        and not code_findings["has_suffix_collector"]
        and code_findings["ported_fun_00661926_stepper"]
        and code_findings["ported_root_terminal_handoff"]
        and code_findings["clean_iterator_end_status"]
        and code_findings["terminal_root_not_queued_as_visible"]
    )

    report = {
        "trie_path": str(trie_path),
        "offsets": OFF,
        "graph_prefixes": len(graph),
        "ambiguous_visible_prefixes": len(ambiguous),
        "shortest_ambiguous": [
            {
                "prefix": prefix,
                "states": format_state_rows(rows, has_child, louds, terminal, labels),
            }
            for prefix, rows in ambiguous[:12]
        ],
        "la_terminal_keys": la_terminal_keys,
        "samples": samples,
        "code_findings": code_findings,
        "reverse_conclusion": [
            "Yandex materialization is not a flat dictionary and not a plain LOUDS DFS.",
            "Terminal nodes are epsilon/handoff edges: FUN_0066242c/FUN_00661926 compute FUN_00661462 and call FUN_00661baa instead of emitting the node label.",
            "The recursive state key is q[depth], with FUN_00661baa writing q[depth]=child_pos and q[depth+1]=terminal_key, then incrementing depth.",
            "Leaf output uses the +0x180 continuation/suffix pool; Complete() must drive that pool through the reversed iterator state machine.",
        ],
        "next_reverse_target": (
            [
                "Validate iterator-produced candidates against a live Yandex oracle for top-K order and boundaries.",
                "Recover native frequency/ngram/normdict scoring so iterator candidates stop ranking like raw trie fragments.",
                "Keep lexicon.predictor absent unless explicit debug --predictor-fallback is set.",
            ]
            if iterator_complete_ported
            else [
                "Implement a byte iterator equivalent to FUN_00661926, including the lVar6==0 leaf +0x180 path.",
                "Replace Complete() terminal recording with iterator-produced strings, then let NNLM/ranker reorder candidates.",
                "Keep lexicon.predictor absent unless explicit debug --predictor-fallback is set.",
            ]
        ),
    }

    OUT_JSON.write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")

    lines: list[str] = []
    lines.append("# R3 Yandex materializer audit — 2026-05-01")
    lines.append("")
    lines.append(f"`TRIE_PATH={trie_path}`")
    lines.append("")
    lines.append("## Verdict")
    lines.append("")
    if iterator_complete_ported:
        lines.append("Мы не должны возвращаться к словарю. Текущий `Complete()` уже использует iterator-backed terminal handoff path; следующий риск — не traversal presence, а oracle parity/ranking/boundaries.")
    else:
        lines.append("Мы не должны возвращаться к словарю. Проблема сейчас в другом: `Complete()` всё ещё делает приближённый root/LOUDS DFS, а настоящий Yandex-путь — iterator/state-machine.")
    lines.append("")
    lines.append("Reverse facts:")
    for item in report["reverse_conclusion"]:
        lines.append(f"- {item}")
    lines.append("")
    lines.append("## Concrete proof: active states, not single node")
    lines.append("")
    lines.append(f"- Visible prefixes scanned: `{report['graph_prefixes']}`")
    lines.append(f"- Ambiguous visible prefixes: `{report['ambiguous_visible_prefixes']}`")
    lines.append("")
    lines.append("Shortest ambiguous prefixes:")
    for row in report["shortest_ambiguous"]:
        state_text = ", ".join(
            f"node={s['node']}:term={int(s['terminal'])}:child={int(s['has_child'])}"
            for s in row["states"]
        )
        lines.append(f"- `{row['prefix']}` -> {state_text}")
    lines.append("")
    lines.append("`ла` handoff keys:")
    for row in la_terminal_keys:
        lines.append(
            f"- node={row['node']} rank={row['terminal_rank']} upper={row['upper']} key={row['key_hex']} low={row['low_cp1251']}"
        )
    lines.append("")
    lines.append("## Current approximation samples")
    lines.append("")
    for prefix in SAMPLE_PREFIXES:
        sample = samples[prefix]
        lines.append(f"### `{prefix}`")
        lines.append("")
        lines.append("Active root states:")
        for state in sample["states"]:
            lines.append(
                f"- node={state['node']} terminal={int(state['terminal'])} has_child={int(state['has_child'])} kids=`{state['kids']}`"
            )
        lines.append("")
        lines.append("Current root DFS terminal examples that are not yet verified Yandex materialization:")
        for cand in sample["current_root_dfs_terminals"][:8]:
            path = "→".join(str(x) for x in cand["path"])
            lines.append(f"- `{cand['text']}` node={cand['terminal_node']} path={path}")
        lines.append("")
    lines.append("## Code audit")
    lines.append("")
    lines.append(f"- direct all-level terminal handoff loop present: `{code_findings['direct_all_level_handoff_loop']}`")
    lines.append(f"- terminal frame text recording present: `{code_findings['records_terminal_frame_text']}`")
    lines.append(f"- suffix collector exists but is not iterator-driven: `{code_findings['has_suffix_collector']}`")
    lines.append(f"- FUN_00661926-style recursive stepper ported: `{code_findings['ported_fun_00661926_stepper']}`")
    lines.append(f"- root terminal FUN_00661462/FUN_00661baa handoff ported: `{code_findings['ported_root_terminal_handoff']}`")
    lines.append(f"- clean iterator end/invalid status ported: `{code_findings['clean_iterator_end_status']}`")
    lines.append(f"- terminal root states excluded from visible queue: `{code_findings['terminal_root_not_queued_as_visible']}`")
    lines.append("")
    lines.append("## Next implementation target")
    lines.append("")
    for item in report["next_reverse_target"]:
        lines.append(f"- {item}")
    lines.append("")
    lines.append("Generated by `tools/r3_yandex_materializer_audit.py`.")
    OUT_MD.write_text("\n".join(lines) + "\n", encoding="utf-8")

    print(f"written {OUT_MD}")
    print(f"written {OUT_JSON}")
    print(json.dumps({"ambiguous": len(ambiguous), "samples": SAMPLE_PREFIXES}, ensure_ascii=False))


if __name__ == "__main__":
    main()
