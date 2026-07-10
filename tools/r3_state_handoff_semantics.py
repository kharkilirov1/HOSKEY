#!/usr/bin/env python3
"""Model succinct trie iterator state handoff semantics.

This does not guess new blob offsets. It encodes the confirmed control-flow contract
between FUN_0066242c / FUN_00661baa / FUN_00661b66 / FUN_00661926 and shows which
value becomes the next recursive key.
"""

from __future__ import annotations

from dataclasses import dataclass, field
from pathlib import Path

OUT_PATH = Path(r"C:\Users\Kharki\DevEcoStudioProjects\HOSKEY\reverse\R3_state_handoff_semantics_2026-04-08.txt")


@dataclass
class State:
    q: list[int] = field(default_factory=lambda: [0, 0, 0, 0])
    depth: int = 0

    def copy(self) -> "State":
        return State(self.q[:], self.depth)


def fun_00661b66(src: State, param3: int) -> State:
    dst = src.copy()
    if dst.depth == 0:
        raise ValueError("FUN_00661b66 requires non-zero depth")
    dst.q[dst.depth] = param3
    return dst


def fun_00661baa(src: State, param3: int, param4: int) -> State:
    dst = src.copy()
    if dst.depth != 0:
        dst.q[dst.depth] = param3
        cur = dst.depth
    else:
        dst.q[0] = param3
        cur = 0
    dst.q[cur + 1] = param4
    dst.depth = dst.depth + 1
    return dst


def fun_00661926_reads_key(state: State) -> int:
    if state.depth == 0:
        raise ValueError("FUN_00661926 aborts when depth == 0")
    return state.q[state.depth]


def fmt_state(name: str, st: State) -> str:
    return f"{name}: depth={st.depth} q0={st.q[0]} q1={st.q[1]} q2={st.q[2]} q3={st.q[3]} next_key=q[depth]={st.q[st.depth] if st.depth < len(st.q) else 'OOB'}"


def main() -> None:
    lines: list[str] = []
    lines.append("Confirmed stack semantics from decomp")
    lines.append("====================================")
    lines.append("")
    lines.append("Key code facts:")
    lines.append("- FUN_00661926 reads current recursive key as `state.q[state.depth]`.")
    lines.append("- FUN_00661b66 copies state and overwrites `state.q[state.depth] = param3`; depth is unchanged.")
    lines.append("- FUN_00661baa copies state, writes `state.q[state.depth] = param3` (or q0 when depth=0),")
    lines.append("  then writes `state.q[state.depth+1] = param4`, and finally increments depth.")
    lines.append("")

    initial = State()
    lines.append(fmt_state("initial", initial))
    lines.append("initial depth=0 matches the root-entry check in FUN_0066268c: `if (param_2[4] == 0)`.")
    lines.append("")

    root_terminal = fun_00661baa(initial, param3=3, param4=0xE0)
    lines.append("Scenario A: root terminal hit via FUN_0066242c -> FUN_00661baa")
    lines.append("  param3 = root node id / child id")
    lines.append("  param4 = FUN_00661462(root_trie, node_id, terminal_rank)")
    lines.append(fmt_state("root_terminal", root_terminal))
    lines.append(f"FUN_00661926 would now read key=0x{fun_00661926_reads_key(root_terminal):X} from q1, NOT from q0.")
    lines.append("=> after a terminal root hit, the next recursive key is the FUN_00661462 output, not the raw node id.")
    lines.append("")

    root_terminal_wide = fun_00661baa(initial, param3=3, param4=0x01E0)
    lines.append("Scenario B: same as A, but FUN_00661462 contributes upper bits")
    lines.append(fmt_state("root_terminal_wide", root_terminal_wide))
    lines.append(f"FUN_00661926 would read key=0x{fun_00661926_reads_key(root_terminal_wide):X} from q1.")
    lines.append("=> if raw cp1251 byte 0xE0 fails offline, that is fully consistent with FUN_00661462 producing a widened/non-raw key.")
    lines.append("")

    recursive_nonterminal = fun_00661b66(root_terminal_wide, param3=0x2A)
    lines.append("Scenario C: inside FUN_00661926, non-terminal child branch")
    lines.append("  branch code does `FUN_00661b66(&local_58, state, uVar4)`")
    lines.append("  where `uVar4 = FUN_00661646(...)` from the child trie")
    lines.append(fmt_state("recursive_nonterminal", recursive_nonterminal))
    lines.append(f"FUN_00661926 would recurse with key=0x{fun_00661926_reads_key(recursive_nonterminal):X} from the SAME depth slot.")
    lines.append("=> non-terminal descent keeps the same depth and swaps in the next child-id-like key.")
    lines.append("")

    recursive_terminal = fun_00661baa(root_terminal_wide, param3=0x2A, param4=0x0138)
    lines.append("Scenario D: inside FUN_00661926, terminal child branch")
    lines.append("  branch code does `FUN_00661baa(&local_58, state, uVar4, uVar5)`")
    lines.append("  where `uVar5 = FUN_00661462(child_trie, uVar1, terminal_rank)`")
    lines.append(fmt_state("recursive_terminal", recursive_terminal))
    lines.append(f"FUN_00661926 would recurse with key=0x{fun_00661926_reads_key(recursive_terminal):X} from the NEW deeper slot.")
    lines.append("=> terminal descent is exactly the cross-level handoff: `uVar5` becomes the next depth key.")
    lines.append("")

    lines.append("Hard takeaway")
    lines.append("-------------")
    lines.append("The previously tested handoff `state_key = raw cp1251 byte` is too weak.")
    lines.append("The decomp now shows the exact recursive handoff key comes from FUN_00661462 on terminal transitions.")
    lines.append("So the remaining blocker is narrower:")
    lines.append("- recover / emulate FUN_00661462 values at the root and child levels")
    lines.append("- especially the optional upper-bit extension driven by trie fields +0x160 / +0x168 / +0x170")
    lines.append("- only then re-run recursive traversal / extraction")
    lines.append("")
    lines.append("Implication for the current offline brute-force results")
    lines.append("-----------------------------------------------")
    lines.append("The earlier delta~-86 family was probably approximating the NEXT-LEVEL key space indirectly.")
    lines.append("But the exact target is no longer mysterious: it is `uVar5 = FUN_00661462(...)`, not a naked blob1 byte.")

    report = "\n".join(lines) + "\n"
    OUT_PATH.write_text(report, encoding="utf-8")
    print(report)
    print(f"written to {OUT_PATH}")


if __name__ == "__main__":
    main()
