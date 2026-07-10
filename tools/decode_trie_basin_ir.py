#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
import pathlib
import struct
import sys
from collections import Counter, defaultdict

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import analyze_trie as at  # noqa: E402


RECORD_RANGES = [
    (2043, 2531),
    (4574, 5062),
    (7015, 7503),
    (9455, 9943),
]

BASIN_MAP = {
    ("OP_89", "EY"): "B_EY",
    ("OP_0A", "U"): "B_U",
    ("OP_AE", "YU"): "B_YU",
}


def classify_signature_action(
    basin_sequence: tuple[str, ...],
    transitions: tuple[tuple[str, str, tuple[str, ...]], ...],
) -> str:
    if basin_sequence == ("B_EY",) and not transitions:
        return "ACTION_LOCAL_EY"
    if basin_sequence == ("B_U",) and not transitions:
        return "ACTION_LOCAL_U"
    if basin_sequence == ("B_YU",) and not transitions:
        return "ACTION_LOCAL_YU"

    if basin_sequence == ("B_EY", "B_EY"):
        via = transitions[0][2] if transitions else ()
        if via == ("OP_89",):
            return "ACTION_LOOP_EY_CORE"
        if via == ("OP_05", "OP_89"):
            return "ACTION_LOOP_EY_REFINE"
        if via == ("OP_94", "OP_89"):
            return "ACTION_LOOP_EY_DISPATCH"
        return "ACTION_LOOP_EY"

    if basin_sequence == ("B_U", "B_U"):
        via = transitions[0][2] if transitions else ()
        if via == ("OP_0A",):
            return "ACTION_LOOP_U_CORE"
        return "ACTION_LOOP_U"

    if basin_sequence == ("B_YU", "B_YU"):
        via = transitions[0][2] if transitions else ()
        if via == ("OP_AE",):
            return "ACTION_LOOP_YU_CORE"
        return "ACTION_LOOP_YU"

    if basin_sequence == ("B_EY", "B_U"):
        return "ACTION_BRIDGE_EY_TO_U"
    if basin_sequence == ("B_U", "B_EY"):
        return "ACTION_BRIDGE_U_TO_EY"
    if basin_sequence == ("B_YU", "B_U"):
        return "ACTION_BRIDGE_YU_TO_U"
    if basin_sequence == ("B_U", "B_YU"):
        return "ACTION_BRIDGE_U_TO_YU"
    if basin_sequence == ("B_YU", "B_EY"):
        return "ACTION_BRIDGE_YU_TO_EY"
    if basin_sequence == ("B_EY", "B_YU"):
        return "ACTION_BRIDGE_EY_TO_YU"

    if basin_sequence == ("B_U", "B_YU", "B_U"):
        return "ACTION_BRIDGE_U_YU_U"
    if basin_sequence == ("B_U", "B_EY", "B_EY"):
        return "ACTION_BRIDGE_U_TO_EY_LOOP"
    if basin_sequence == ("B_EY", "B_U", "B_EY"):
        return "ACTION_BRIDGE_EY_U_EY"
    if basin_sequence == ("B_YU", "B_YU", "B_U"):
        return "ACTION_LOOP_YU_TO_U"

    return "ACTION_COMPLEX_PATH"


def parse_ir(chunk: bytes, max_runs: int = 9, max_gaps: int = 8) -> tuple[list[str], list[str], list[str]]:
    pos = 0
    runs: list[bytes] = []
    gaps: list[bytes] = []
    while pos < len(chunk) and len(runs) < max_runs and len(gaps) < max_gaps:
        pos2 = pos
        while pos2 < len(chunk) and at.is_cp1251_byte(chunk[pos2]):
            pos2 += 1
        run = chunk[pos:pos2]
        if not run:
            break
        runs.append(run)
        pos = pos2
        if pos >= len(chunk):
            break
        gap_start = pos
        while pos < len(chunk) and not at.is_cp1251_byte(chunk[pos]):
            pos += 1
        gap = chunk[gap_start:pos]
        if not gap:
            break
        gaps.append(gap)
    classes = [at.class_label(run) for run in runs]
    ops = [at.operator_label(gap) for gap in gaps]
    raw_gaps = [gap.hex() for gap in gaps]
    return classes, ops, raw_gaps


def load_body_and_tail(path: pathlib.Path) -> tuple[list[bytes], bytes]:
    data = path.read_bytes()
    header = struct.unpack_from("<6I", data, 0)
    payload_hint = header[2]
    body = data[24:payload_hint]
    rows = [body[i * 64:(i + 1) * 64] for i in range(len(body) // 64)]
    tail = data[payload_hint:]
    return rows, tail


def iter_cp_leading_slices(path: pathlib.Path):
    rows, tail = load_body_and_tail(path)
    slice_index = 0
    for record_id, (start, end) in enumerate(RECORD_RANGES, start=1):
        recs = []
        for row in rows[start:end]:
            for slot in range(0, 64, 16):
                recs.append(struct.unpack_from("<QII", row, slot))
        offs = [off for _, off, _ in recs]
        for (_, off, _), next_off in zip(recs, offs[1:]):
            if next_off <= off:
                continue
            chunk = tail[off:next_off]
            if at.classify_tail_slice(chunk) != "cp-leading":
                continue
            yield {
                "slice_index": slice_index,
                "record_id": record_id,
                "offset": off,
                "next_offset": next_off,
                "chunk": chunk,
            }
            slice_index += 1


def basin_view_for_slice(chunk: bytes) -> dict:
    classes, ops, raw_gaps = parse_ir(chunk)
    rooted = []
    for i in range(min(len(ops), len(classes) - 1)):
        pair = (ops[i], classes[i + 1])
        basin = BASIN_MAP.get(pair)
        if basin:
            rooted.append(
                {
                    "pair_index": i,
                    "basin": basin,
                    "op": ops[i],
                    "class": classes[i + 1],
                }
            )

    basin_seq = [item["basin"] for item in rooted]
    transitions = []
    for left, right in zip(rooted, rooted[1:]):
        inter_ops = ops[left["pair_index"] + 1:right["pair_index"] + 1]
        inter_classes = classes[left["pair_index"] + 2:right["pair_index"] + 2]
        transitions.append(
            {
                "from": left["basin"],
                "to": right["basin"],
                "via_ops": inter_ops,
                "via_classes": inter_classes,
            }
        )

    return {
        "preview": at.decode_preview(chunk, 120),
        "classes": classes,
        "ops": ops,
        "raw_gaps": raw_gaps,
        "rooted_pairs": rooted,
        "basin_sequence": basin_seq,
        "transitions": transitions,
        "action": classify_signature_action(
            tuple(basin_seq),
            tuple((t["from"], t["to"], tuple(t["via_ops"])) for t in transitions),
        ),
    }


def build_summary(path: pathlib.Path, limit_examples: int) -> dict:
    seq_counts: Counter[tuple[str, ...]] = Counter()
    transition_counts: Counter[tuple[str, str]] = Counter()
    transition_realizations: Counter[tuple[str, str, tuple[str, ...]]] = Counter()
    signature_counts: Counter[tuple[tuple[str, ...], tuple[tuple[str, str, tuple[str, ...]], ...]]] = Counter()
    by_record: dict[str, Counter[tuple[str, str]]] = defaultdict(Counter)
    examples: list[dict] = []
    signature_examples: dict[
        tuple[tuple[str, ...], tuple[tuple[str, str, tuple[str, ...]], ...]],
        list[dict],
    ] = defaultdict(list)

    for item in iter_cp_leading_slices(path):
        view = basin_view_for_slice(item["chunk"])
        seq = tuple(view["basin_sequence"])
        signature = (
            seq,
            tuple(
                (t["from"], t["to"], tuple(t["via_ops"]))
                for t in view["transitions"]
            ),
        )
        if seq:
            seq_counts[seq] += 1
            signature_counts[signature] += 1
            if len(examples) < limit_examples:
                examples.append(
                    {
                        "slice_index": item["slice_index"],
                        "record_id": item["record_id"],
                        "offset": item["offset"],
                        "preview": view["preview"],
                        "basin_sequence": view["basin_sequence"],
                        "action": view["action"],
                        "classes": view["classes"][:8],
                        "ops": view["ops"][:7],
                    }
                )
            if len(signature_examples[signature]) < 4:
                signature_examples[signature].append(
                    {
                        "slice_index": item["slice_index"],
                        "record_id": item["record_id"],
                        "offset": item["offset"],
                        "preview": view["preview"],
                    }
                )
        for transition in view["transitions"]:
            edge = (transition["from"], transition["to"])
            transition_counts[edge] += 1
            by_record[f"RECORD_{item['record_id']}"][edge] += 1
            transition_realizations[(transition["from"], transition["to"], tuple(transition["via_ops"]))] += 1

    return {
        "seq_counts": [{"sequence": list(seq), "count": count} for seq, count in seq_counts.most_common(20)],
        "transition_counts": [
            {"edge": list(edge), "count": count}
            for edge, count in transition_counts.most_common(20)
        ],
        "transition_realizations": [
            {"edge": [src, dst], "via_ops": list(via), "count": count}
            for (src, dst, via), count in transition_realizations.most_common(24)
        ],
        "signature_counts": [
            {
                "sequence": list(seq),
                "transitions": [
                    {"from": src, "to": dst, "via_ops": list(via)}
                    for src, dst, via in transitions
                ],
                "action": classify_signature_action(seq, transitions),
                "count": count,
                "examples": signature_examples[(seq, transitions)],
            }
            for (seq, transitions), count in signature_counts.most_common(20)
        ],
        "by_record": {
            record: [
                {"edge": list(edge), "count": count}
                for edge, count in counter.most_common(12)
            ]
            for record, counter in by_record.items()
        },
        "examples": examples,
    }


def main() -> None:
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")

    parser = argparse.ArgumentParser(description="Emit basin-level IR from cp-leading trie slices.")
    parser.add_argument(
        "path",
        nargs="?",
        default="entry/src/main/resources/rawfile/dict/trie",
        help="path to trie blob",
    )
    parser.add_argument("--slice-index", type=int, help="show one cp-leading slice by index")
    parser.add_argument("--examples", type=int, default=8, help="number of summary examples")
    parser.add_argument("--json", action="store_true", help="emit JSON")
    args = parser.parse_args()

    trie_path = pathlib.Path(args.path)

    if args.slice_index is not None:
        for item in iter_cp_leading_slices(trie_path):
            if item["slice_index"] == args.slice_index:
                view = basin_view_for_slice(item["chunk"])
                payload = {
                    "slice_index": item["slice_index"],
                    "record_id": item["record_id"],
                    "offset": item["offset"],
                    **view,
                }
                if args.json:
                    print(json.dumps(payload, ensure_ascii=False, indent=2))
                else:
                    print(f"slice_index={payload['slice_index']} record={payload['record_id']} offset={payload['offset']}")
                    print(f"preview={payload['preview']}")
                    print(f"basin_sequence={payload['basin_sequence']}")
                    print(f"action={payload['action']}")
                    print(f"rooted_pairs={payload['rooted_pairs']}")
                    print(f"transitions={payload['transitions']}")
                    print(f"classes={payload['classes']}")
                    print(f"ops={payload['ops']}")
                return
        raise SystemExit(f"slice_index {args.slice_index} not found")

    summary = build_summary(trie_path, args.examples)
    if args.json:
        print(json.dumps(summary, ensure_ascii=False, indent=2))
        return

    print("top_sequences:")
    for row in summary["seq_counts"]:
        print(f"  {row['sequence']} count={row['count']}")

    print("\ntransition_counts:")
    for row in summary["transition_counts"]:
        print(f"  {row['edge']} count={row['count']}")

    print("\ntransition_realizations:")
    for row in summary["transition_realizations"]:
        print(f"  {row['edge']} via={row['via_ops']} count={row['count']}")

    print("\nsignature_counts:")
    for row in summary["signature_counts"]:
        rendered = [
            (t["from"], t["to"], t["via_ops"])
            for t in row["transitions"]
        ]
        print(
            f"  seq={row['sequence']} action={row['action']} "
            f"transitions={rendered} count={row['count']}"
        )

    print("\nexamples:")
    for row in summary["examples"]:
        print(
            f"  slice={row['slice_index']} record={row['record_id']} "
            f"seq={row['basin_sequence']} action={row['action']} preview={row['preview']}"
        )


if __name__ == "__main__":
    main()
