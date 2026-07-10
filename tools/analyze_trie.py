#!/usr/bin/env python3
from __future__ import annotations

import argparse
import collections
import pathlib
import sys
import struct
import statistics
import math


def cp1251ish_ratio(chunk: bytes) -> float:
    if not chunk:
        return 0.0
    hits = sum(1 for b in chunk if 0xC0 <= b <= 0xFF or b in (0xA8, 0xB8))
    return hits / len(chunk)


def bit_density(chunk: bytes) -> float:
    if not chunk:
        return 0.0
    return sum(int(byte).bit_count() for byte in chunk) / (len(chunk) * 8.0)


def is_cp1251_byte(b: int) -> bool:
    return b in (0xA8, 0xB8) or 0xC0 <= b <= 0xFF


def decode_preview(chunk: bytes, limit: int = 32) -> str:
    out = []
    for b in chunk[:limit]:
        if b == 0:
            out.append("·")
        elif 32 <= b < 127:
            out.append(chr(b))
        elif is_cp1251_byte(b):
            out.append(bytes([b]).decode("cp1251", errors="replace"))
        else:
            out.append(f"[{b:02x}]")
    return "".join(out)


def iter_cp1251_runs(data: bytes, min_len: int = 2):
    i = 0
    while i < len(data):
        if is_cp1251_byte(data[i]):
            j = i
            while j < len(data) and is_cp1251_byte(data[j]):
                j += 1
            if j - i >= min_len:
                yield i, data[i:j].decode("cp1251", errors="replace")
            i = j
        else:
            i += 1


def pearson(xs: list[float], ys: list[float]) -> float:
    if len(xs) != len(ys) or len(xs) < 2:
        return 0.0
    mx = sum(xs) / len(xs)
    my = sum(ys) / len(ys)
    num = sum((x - mx) * (y - my) for x, y in zip(xs, ys))
    den_x = sum((x - mx) ** 2 for x in xs)
    den_y = sum((y - my) ** 2 for y in ys)
    den = math.sqrt(den_x * den_y)
    return 0.0 if den == 0.0 else num / den


def classify_tail_slice(chunk: bytes) -> str:
    if not chunk:
        return "empty"
    head = chunk[0]
    prefix = chunk[: min(32, len(chunk))]
    cp_ratio = cp1251ish_ratio(prefix)
    zero_ratio = prefix.count(0) / len(prefix)
    if head in (0, 1, 2) or (head < 0x20 and not is_cp1251_byte(head)):
        return "control-leading"
    if is_cp1251_byte(head):
        return "cp-leading"
    if cp_ratio >= 0.50:
        return "cp-dense-mixed"
    if zero_ratio >= 0.15:
        return "zero-mixed"
    return "mixed"


def first_cp_run(chunk: bytes) -> bytes:
    i = 0
    while i < len(chunk) and is_cp1251_byte(chunk[i]):
        i += 1
    return chunk[:i]


def class_label(run: bytes) -> str:
    s = run.decode("cp1251", errors="replace")
    if not s:
        return "EMPTY"
    if s.startswith("ей"):
        return "EY"
    if s.startswith("юя") or s.startswith("ю"):
        return "YU"
    if s.startswith("уы") or s.startswith("у") or s.startswith("уЫ"):
        return "U"
    if s.startswith("ае"):
        return "AE"
    if s.startswith("аи"):
        return "AI"
    if s.startswith("и"):
        return "I"
    if s.startswith("а"):
        return "A"
    if s.startswith("е"):
        return "E"
    if s.startswith("о"):
        return "O"
    if s.startswith("ы"):
        return "Y"
    if s.startswith("я"):
        return "YA"
    if s.startswith("л"):
        return "L"
    if s.startswith("н"):
        return "N"
    if s.startswith("м"):
        return "M"
    if s.startswith("т"):
        return "T"
    if s.startswith("х"):
        return "H"
    if s.startswith("й"):
        return "J"
    return s[:2]


def operator_label(gap: bytes) -> str:
    if not gap:
        return "OP_EMPTY"
    first = gap[0]
    known = {
        0x89: "OP_89",
        0x0A: "OP_0A",
        0xAE: "OP_AE",
        0x94: "OP_94",
        0x20: "OP_20",
        0xB3: "OP_B3",
        0x67: "OP_67",
        0x57: "OP_57",
        0x30: "OP_30",
        0x7D: "OP_7D",
        0x0C: "OP_0C",
        0x23: "OP_23",
    }
    return known.get(first, f"OP_{first:02X}")


def analyze(path: pathlib.Path, block_size: int) -> None:
    data = path.read_bytes()
    print(f"file={path}")
    print(f"size={len(data)}")
    header = None
    if len(data) >= 24:
        header = struct.unpack_from("<6I", data, 0)
        print("header_dwords=", [hex(v) for v in header], header)
        payload_hint = header[2]
        count_hint = header[4]
        print(f"payload_hint={payload_hint} count_hint={count_hint}")
        if 0 < payload_hint < len(data):
            sample = data[payload_hint: payload_hint + 64]
            print(f"payload_hint_sample={sample.hex()}")

            body = data[24:payload_hint]
            full_rows, remainder = divmod(len(body), 64)
            print("\nrow_matrix_hypothesis:")
            print(f"  dense_region_len={len(body)}")
            print(f"  64_byte_rows={full_rows} remainder={remainder}")
            print(f"  count_hint={count_hint}")
            print(f"  payload_hint - count_hint*64 = {payload_hint - count_hint * 64}")
            print(f"  dense_region_len - (count_hint-1)*64 = {len(body) - (count_hint - 1) * 64}")
            if full_rows > 0:
                pops = [sum(int(x).bit_count() for x in body[i * 64:(i + 1) * 64]) for i in range(full_rows)]
                counts = collections.Counter(pops)
                print(
                    f"  popcount_stats min={min(pops)} max={max(pops)} "
                    f"mean={statistics.mean(pops):.2f} median={statistics.median(pops):.2f}"
                )
                print(f"  top_popcounts={counts.most_common(12)}")
                for idx in [0, 1, 2, full_rows - 3, full_rows - 2, full_rows - 1]:
                    if 0 <= idx < full_rows:
                        row = body[idx * 64:(idx + 1) * 64]
                        print(
                            f"  row[{idx}] popcount={sum(int(x).bit_count() for x in row)} "
                            f"nonzero={sum(1 for x in row if x)} first16={row[:16].hex()}"
                        )

                print("\nsection_hypothesis:")
                u32_table_flags = []
                rows = [body[i * 64:(i + 1) * 64] for i in range(full_rows)]
                for row in rows:
                    vals = struct.unpack("<16I", row)
                    small = sum(1 for v in vals if v < 100000)
                    nondec = sum(1 for i in range(1, 16) if vals[i] >= vals[i - 1])
                    u32_table_flags.append(small >= 12 and nondec >= 12)

                ranges = []
                start = None
                for i, flag in enumerate(u32_table_flags):
                    if flag and start is None:
                        start = i
                    elif not flag and start is not None:
                        ranges.append((start, i))
                        start = None
                if start is not None:
                    ranges.append((start, len(u32_table_flags)))

                if ranges:
                    print("  monotonic_u32_row_ranges:")
                    for start, end in ranges[:16]:
                        abs_off = 24 + start * 64
                        end_off = 24 + end * 64
                        print(
                            f"    rows[{start}:{end}] abs=[{abs_off},{end_off}) "
                            f"bytes={end_off - abs_off}"
                        )
                        sample = struct.unpack("<16I", rows[start])
                        print(f"      first_row_u32={sample}")
                        sample_last = struct.unpack("<16I", rows[end - 1])
                        print(f"      last_row_u32={sample_last}")

                record_ranges = []
                record_flags = []
                for row in rows:
                    vals = struct.unpack("<16I", row)
                    zero4 = vals[3] == 0 and vals[7] == 0 and vals[11] == 0 and vals[15] == 0
                    small3 = sum(1 for idx in (2, 6, 10, 14) if vals[idx] < 2_000_000)
                    record_flags.append(zero4 and small3 >= 3)

                start = None
                for i, flag in enumerate(record_flags):
                    if flag and start is None:
                        start = i
                    elif not flag and start is not None:
                        record_ranges.append((start, i))
                        start = None
                if start is not None:
                    record_ranges.append((start, len(record_flags)))

                if record_ranges:
                    print("  four_record_row_ranges:")
                    for start, end in record_ranges[:16]:
                        abs_off = 24 + start * 64
                        end_off = 24 + end * 64
                        vals = struct.unpack("<16I", rows[start])
                        recs = [vals[i:i + 4] for i in range(0, 16, 4)]
                        vals_last = struct.unpack("<16I", rows[min(end - 1, start + 2)])
                        recs_last = [vals_last[i:i + 4] for i in range(0, 16, 4)]
                        thirds = []
                        for row in rows[start:end]:
                            vals_r = struct.unpack("<16I", row)
                            thirds.extend([vals_r[2], vals_r[6], vals_r[10], vals_r[14]])
                        print(
                            f"    rows[{start}:{end}] abs=[{abs_off},{end_off}) "
                            f"bytes={end_off - abs_off}"
                        )
                        print(f"      first_records={recs}")
                        print(f"      sample_records={recs_last}")
                        print(
                            f"      record_count={len(thirds)} "
                            f"third_min={min(thirds)} third_max={max(thirds)} "
                            f"third_last8={thirds[-8:]}"
                        )

            tail = data[payload_hint:]
            print("\ntail_phase_hypothesis:")
            transition = None
            for off in range(0, min(len(tail), 512 * 200), 512):
                chunk = tail[off: off + 512]
                cp_ratio = cp1251ish_ratio(chunk)
                if cp_ratio > 0.50:
                    transition = off
                    break
            print(f"  first_cp1251_dense_512_block={transition}")
            if transition is not None:
                print(
                    f"  before_transition_cp1251={cp1251ish_ratio(tail[max(0, transition - 512):transition]):.3f} "
                    f"after_transition_cp1251={cp1251ish_ratio(tail[transition:transition + 512]):.3f}"
                )
                print(f"  preamble_preview={decode_preview(tail[max(0, transition - 64):transition], 64)}")
                print(f"  string_arena_preview={decode_preview(tail[transition:transition + 64], 64)}")

                arena = tail[transition:min(len(tail), transition + 200000)]
                runs = []
                i = 0
                while i < len(arena):
                    if is_cp1251_byte(arena[i]):
                        j = i
                        while j < len(arena) and is_cp1251_byte(arena[j]):
                            j += 1
                        if j - i >= 3:
                            runs.append(arena[i:j])
                        i = j
                    else:
                        i += 1
                if runs:
                    run_lengths = [len(run) for run in runs]
                    monotonic = sum(
                        1 for run in runs
                        if all(run[k + 1] >= run[k] for k in range(len(run) - 1))
                    )
                    one_drop = sum(
                        1 for run in runs
                        if sum(run[k + 1] < run[k] for k in range(len(run) - 1)) <= 1
                    )
                    print("  arena_run_stats:")
                    print(
                        f"    run_count={len(runs)} mean_len={statistics.mean(run_lengths):.2f} "
                        f"median_len={statistics.median(run_lengths):.2f}"
                    )
                    print(
                        f"    monotonic_ratio={monotonic / len(runs):.3f} "
                        f"one_drop_ratio={one_drop / len(runs):.3f}"
                    )
                    printed = 0
                    print("    sample_monotonic_runs:")
                    for run in runs:
                        if len(run) >= 8 and all(run[k + 1] >= run[k] for k in range(len(run) - 1)):
                            print(f"      {run[:40].decode('cp1251', errors='replace')}")
                            printed += 1
                            if printed >= 12:
                                break

                if record_ranges:
                    print("  record_tail_linkage:")
                    for idx, (start, end) in enumerate(record_ranges[:8], start=1):
                        recs = []
                        for row in rows[start:end]:
                            for slot in range(0, 64, 16):
                                u64, off, zero = struct.unpack_from("<QII", row, slot)
                                recs.append((u64, off, zero))
                        offs = [off for _, off, _ in recs]
                        valid_lens = [
                            b - a
                            for a, b in zip(offs, offs[1:])
                            if b > a
                        ]
                        first_bytes = [
                            tail[off] for off in offs
                            if 0 <= off < len(tail)
                        ]
                        u64_byte7 = [u64.to_bytes(8, "little")[7] for u64, _, _ in recs[:-1]]
                        lens_for_corr = [
                            b - a for a, b in zip(offs, offs[1:]) if b > a
                        ]
                        offs_for_corr = offs[:-1][:len(lens_for_corr)]
                        print(
                            f"    RECORD_{idx}: offsets_in_preamble={sum(1 for off in offs if off < transition)} "
                            f"offsets_in_string_arena={sum(1 for off in offs if off >= transition)}"
                        )
                        if valid_lens:
                            print(
                                f"      len_mean={statistics.mean(valid_lens):.2f} "
                                f"len_median={statistics.median(valid_lens):.2f} "
                                f"len_q1_q3={statistics.quantiles(valid_lens, n=4)[0]:.2f},"
                                f"{statistics.quantiles(valid_lens, n=4)[2]:.2f}"
                            )
                        if first_bytes:
                            print(
                                f"      first_bytes_top={collections.Counter(first_bytes).most_common(10)}"
                            )
                        if len(u64_byte7) == len(lens_for_corr) and lens_for_corr:
                            print(
                                f"      corr(byte7,len)={pearson(u64_byte7, lens_for_corr):.4f} "
                                f"corr(byte7,off)={pearson(u64_byte7, offs_for_corr):.4f}"
                            )

                        categories = collections.Counter()
                        category_lens: dict[str, list[int]] = collections.defaultdict(list)
                        category_firsts: dict[str, collections.Counter[int]] = collections.defaultdict(collections.Counter)
                        cp_run_lengths = []
                        cp_run_monotonic = 0
                        cp_run_one_drop = 0
                        cp_run_starts = collections.Counter()
                        for (u64, off, _), next_off in zip(recs, offs[1:]):
                            if next_off <= off:
                                continue
                            chunk = tail[off:next_off]
                            cat = classify_tail_slice(chunk)
                            categories[cat] += 1
                            category_lens[cat].append(len(chunk))
                            category_firsts[cat][chunk[0]] += 1
                            if cat == "cp-leading":
                                run = first_cp_run(chunk)
                                if run:
                                    cp_run_lengths.append(len(run))
                                    drops = sum(run[k + 1] < run[k] for k in range(len(run) - 1))
                                    if drops == 0:
                                        cp_run_monotonic += 1
                                    if drops <= 1:
                                        cp_run_one_drop += 1
                                    cp_run_starts[run[: min(3, len(run))].decode("cp1251", errors="replace")] += 1
                        if categories:
                            print(f"      slice_categories={categories.most_common()}")
                            for cat, count in categories.most_common():
                                lens = category_lens[cat]
                                print(
                                    f"        {cat}: mean_len={statistics.mean(lens):.2f} "
                                    f"median_len={statistics.median(lens):.2f} "
                                    f"top_firsts={category_firsts[cat].most_common(8)}"
                                )
                        if cp_run_lengths:
                            print(
                                f"      cp_first_run mean={statistics.mean(cp_run_lengths):.2f} "
                                f"median={statistics.median(cp_run_lengths):.2f} "
                                f"monotonic_ratio={cp_run_monotonic / len(cp_run_lengths):.3f} "
                                f"one_drop_ratio={cp_run_one_drop / len(cp_run_lengths):.3f} "
                                f"top_starts={cp_run_starts.most_common(10)}"
                            )

                        cp_patterns = collections.Counter()
                        cp_gap_pairs = collections.Counter()
                        for (u64, off, _), next_off in zip(recs, offs[1:]):
                            if next_off <= off:
                                continue
                            chunk = tail[off:next_off]
                            if classify_tail_slice(chunk) != "cp-leading":
                                continue
                            p1 = len(first_cp_run(chunk))
                            j = p1
                            while j < len(chunk) and not is_cp1251_byte(chunk[j]):
                                j += 1
                            gap1 = j - p1
                            p2 = len(first_cp_run(chunk[j:]))
                            k = j + p2
                            while k < len(chunk) and not is_cp1251_byte(chunk[k]):
                                k += 1
                            gap2 = k - (j + p2)
                            p3 = len(first_cp_run(chunk[k:]))
                            cp_patterns[f"C{p1}-N{gap1}-C{p2}-N{gap2}-C{p3}"] += 1
                            cp_gap_pairs[(gap1, gap2)] += 1
                        if cp_patterns:
                            print(
                                f"      cp_grammar_top={cp_patterns.most_common(10)} "
                                f"gap_pairs={cp_gap_pairs.most_common(8)}"
                            )

                        separator_counts = collections.Counter()
                        separator_next = collections.defaultdict(collections.Counter)
                        separator_prev = collections.defaultdict(collections.Counter)
                        separator_pair_next = collections.defaultdict(collections.Counter)
                        for (u64, off, _), next_off in zip(recs, offs[1:]):
                            if next_off <= off:
                                continue
                            chunk = tail[off:next_off]
                            if classify_tail_slice(chunk) != "cp-leading":
                                continue
                            run1 = first_cp_run(chunk)
                            p1 = len(run1)
                            if p1 >= len(chunk):
                                continue
                            j = p1
                            while j < len(chunk) and not is_cp1251_byte(chunk[j]):
                                j += 1
                            gap1 = chunk[p1:j]
                            run2 = first_cp_run(chunk[j:])
                            if not gap1 or not run2:
                                continue
                            s1 = gap1[0]
                            prev_char = run1[:1].decode("cp1251", errors="replace")
                            next_char = run2[:1].decode("cp1251", errors="replace")
                            separator_counts[s1] += 1
                            separator_next[s1][next_char] += 1
                            separator_prev[s1][prev_char] += 1
                            if len(gap1) >= 2:
                                separator_pair_next[gap1[:2].hex()][next_char] += 1
                        if separator_counts:
                            top_seps = separator_counts.most_common(8)
                            print(f"      separator_top={top_seps}")
                            for sep, cnt in top_seps[:5]:
                                print(
                                    f"        sep=0x{sep:02x} next={separator_next[sep].most_common(6)} "
                                    f"prev={separator_prev[sep].most_common(6)}"
                                )
                            pair_items = list(separator_pair_next.items())[:0]
                            pair_counts = collections.Counter({k: sum(v.values()) for k, v in separator_pair_next.items()})
                            print(
                                f"      separator_pairs_top="
                                f"{[(k, c, separator_pair_next[k].most_common(4)) for k, c in pair_counts.most_common(6)]}"
                            )
                            typed = []
                            for sep, cnt in top_seps[:5]:
                                next_items = separator_next[sep].most_common(2)
                                if next_items:
                                    typed.append(
                                        (f"0x{sep:02x}", cnt, next_items[0], next_items[1] if len(next_items) > 1 else None)
                                    )
                            print(f"      separator_hints={typed}")

                    motif2 = collections.Counter()
                    motif3 = collections.Counter()
                    op_pairs = collections.Counter()
                    for row in rows[start:end]:
                        for slot in range(0, 64, 16):
                            pass
                    for (u64, off, _), next_off in zip(recs, offs[1:]):
                        if next_off <= off:
                            continue
                        chunk = tail[off:next_off]
                        if classify_tail_slice(chunk) != "cp-leading":
                            continue
                        pos = 0
                        runs_local = []
                        gaps_local = []
                        while pos < len(chunk) and len(runs_local) < 4 and len(gaps_local) < 3:
                            run, pos2 = chunk[pos:pos], pos
                            while pos2 < len(chunk) and is_cp1251_byte(chunk[pos2]):
                                pos2 += 1
                            run = chunk[pos:pos2]
                            if not run:
                                break
                            runs_local.append(run)
                            pos = pos2
                            if pos >= len(chunk):
                                break
                            gstart = pos
                            while pos < len(chunk) and not is_cp1251_byte(chunk[pos]):
                                pos += 1
                            gap = chunk[gstart:pos]
                            if not gap:
                                break
                            gaps_local.append(gap)
                        if len(runs_local) >= 2 and len(gaps_local) >= 1:
                            ir = [class_label(runs_local[0]), operator_label(gaps_local[0]), class_label(runs_local[1])]
                            if len(runs_local) >= 3 and len(gaps_local) >= 2:
                                ir += [operator_label(gaps_local[1]), class_label(runs_local[2])]
                            if len(runs_local) >= 4 and len(gaps_local) >= 3:
                                ir += [operator_label(gaps_local[2]), class_label(runs_local[3])]
                            motif2[tuple(ir[:3])] += 1
                            if len(ir) >= 5:
                                motif3[tuple(ir[:5])] += 1
                                op_pairs[(ir[1], ir[3])] += 1
                    if motif2:
                        print(f"      ir_motif2_top={motif2.most_common(10)}")
                    if motif3:
                        print(f"      ir_motif3_top={motif3.most_common(8)}")
                    if op_pairs:
                        print(f"      ir_op_pairs_top={op_pairs.most_common(8)}")

    print("\nblock_scan:")
    rows = []
    for off in range(0, len(data), block_size):
        chunk = data[off: off + block_size]
        rows.append(
            (
                off,
                round(bit_density(chunk), 3),
                round(cp1251ish_ratio(chunk), 3),
                round(chunk.count(0) / len(chunk), 3),
                round(sum(chunk) / len(chunk), 1),
            )
        )

    interesting = [
        row for row in rows
        if row[2] > 0.10 or row[3] > 0.10 or row[1] < 0.25 or row[1] > 0.70
    ]
    for row in interesting[:120]:
        print(
            f"  off={row[0]:>8} density={row[1]:.3f} cp1251={row[2]:.3f} "
            f"zero={row[3]:.3f} mean={row[4]:.1f}"
        )
    if len(interesting) > 120:
        print(f"  ... truncated {len(interesting) - 120} rows")

    print("\ncp1251_runs:")
    runs = list(iter_cp1251_runs(data))
    for off, text in runs[:80]:
        preview = text[:64]
        print(f"  off={off}: {preview}")
    if len(runs) > 80:
        print(f"  ... truncated {len(runs) - 80} runs")

    if header is not None and 0 < header[2] < len(data):
        tail = data[header[2]: min(len(data), header[2] + 65536)]
        print("\ntail_top_bytes:")
        for byte, count in collections.Counter(tail).most_common(40):
            print(f"  {byte:3d} 0x{byte:02x} count={count}")


def main() -> None:
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    parser = argparse.ArgumentParser(description="Inspect Yandex default trie structure.")
    parser.add_argument(
        "path",
        nargs="?",
        default="entry/src/main/resources/rawfile/dict/trie",
        help="path to trie blob",
    )
    parser.add_argument("--block-size", type=int, default=4096)
    args = parser.parse_args()
    try:
        analyze(pathlib.Path(args.path), args.block_size)
    except (BrokenPipeError, OSError):
        pass


if __name__ == "__main__":
    main()
