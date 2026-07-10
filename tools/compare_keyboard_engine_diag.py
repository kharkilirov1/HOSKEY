from __future__ import annotations

import argparse
from dataclasses import dataclass
from pathlib import Path


@dataclass
class CaseRecord:
    prefix: str
    split: bool
    suggestions: list[str]
    diagnostics: str


def parse_report(path: Path) -> dict[str, CaseRecord]:
    records: dict[str, CaseRecord] = {}
    for raw_line in path.read_text(encoding="utf-8").splitlines():
        if not raw_line.startswith("CASE\t"):
            continue
        _, prefix, split_raw, suggestions_raw, diagnostics = raw_line.split(
            "\t", 4
        )
        suggestions = [s for s in suggestions_raw.split("|") if s]
        records[prefix] = CaseRecord(
            prefix=prefix,
            split=split_raw == "1",
            suggestions=suggestions,
            diagnostics=diagnostics,
        )
    return records


def format_list(values: list[str]) -> str:
    return "|".join(values) if values else "-"


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--baseline", required=True)
    parser.add_argument("--overlay", required=True)
    parser.add_argument("--output", required=True)
    parser.add_argument("--top", type=int, default=25)
    args = parser.parse_args()

    baseline = parse_report(Path(args.baseline))
    overlay = parse_report(Path(args.overlay))

    all_prefixes = sorted(set(baseline) | set(overlay))
    changed: list[tuple[int, int, str, CaseRecord, CaseRecord]] = []
    split_changed: list[tuple[int, int, str, CaseRecord, CaseRecord]] = []
    split_promoted: list[str] = []
    top1_changed = 0

    for prefix in all_prefixes:
        base = baseline.get(prefix)
        over = overlay.get(prefix)
        if base is None or over is None:
            continue
        if over.split and not base.split:
            split_promoted.append(prefix)
        if base.suggestions[:1] != over.suggestions[:1]:
            top1_changed += 1
        if base.suggestions == over.suggestions and base.split == over.split:
            continue
        added = [s for s in over.suggestions if s not in base.suggestions]
        removed = [s for s in base.suggestions if s not in over.suggestions]
        row = (len(added), len(removed), prefix, base, over)
        changed.append(row)
        if base.split or over.split:
            split_changed.append(row)

    changed.sort(key=lambda item: (-item[0], -item[1], item[2]))
    split_changed.sort(key=lambda item: (-item[0], -item[1], item[2]))

    lines: list[str] = []
    lines.append("KeyboardEngine baseline vs overlay comparison")
    lines.append(f"baseline={Path(args.baseline)}")
    lines.append(f"overlay={Path(args.overlay)}")
    lines.append("")
    lines.append(f"baseline_cases={len(baseline)}")
    lines.append(f"overlay_cases={len(overlay)}")
    lines.append(f"baseline_split_cases={sum(1 for r in baseline.values() if r.split)}")
    lines.append(f"overlay_split_cases={sum(1 for r in overlay.values() if r.split)}")
    lines.append(f"changed_suggestion_cases={len(changed)}")
    lines.append(f"changed_split_cases={len(split_changed)}")
    lines.append(f"changed_non_split_cases={len(changed) - len(split_changed)}")
    lines.append(f"top1_changed_cases={top1_changed}")
    lines.append(f"overlay_promoted_split_cases={len(split_promoted)}")
    lines.append(
        "overlay_promoted_split_prefixes="
        + (",".join(split_promoted) if split_promoted else "-")
    )
    lines.append("")
    lines.append("split_case_details")
    for _, _, prefix, base, over in split_changed:
        added = [s for s in over.suggestions if s not in base.suggestions]
        removed = [s for s in base.suggestions if s not in over.suggestions]
        lines.append(f"prefix={prefix}")
        lines.append(f"  baseline_split={1 if base.split else 0}")
        lines.append(f"  overlay_split={1 if over.split else 0}")
        lines.append(f"  baseline_suggestions={format_list(base.suggestions)}")
        lines.append(f"  overlay_suggestions={format_list(over.suggestions)}")
        lines.append(f"  added={format_list(added)}")
        lines.append(f"  removed={format_list(removed)}")
        lines.append(f"  baseline_diag={base.diagnostics}")
        lines.append(f"  overlay_diag={over.diagnostics}")

    lines.append("")
    lines.append(f"top_changed_cases_limit={args.top}")

    for _, _, prefix, base, over in changed[: args.top]:
        added = [s for s in over.suggestions if s not in base.suggestions]
        removed = [s for s in base.suggestions if s not in over.suggestions]
        lines.append(f"prefix={prefix}")
        lines.append(f"  baseline_split={1 if base.split else 0}")
        lines.append(f"  overlay_split={1 if over.split else 0}")
        lines.append(f"  baseline_suggestions={format_list(base.suggestions)}")
        lines.append(f"  overlay_suggestions={format_list(over.suggestions)}")
        lines.append(f"  added={format_list(added)}")
        lines.append(f"  removed={format_list(removed)}")
        lines.append(f"  baseline_diag={base.diagnostics}")
        lines.append(f"  overlay_diag={over.diagnostics}")

    output = Path(args.output)
    output.write_text("\n".join(lines) + "\n", encoding="utf-8")
    print(output.read_text(encoding="utf-8"))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
