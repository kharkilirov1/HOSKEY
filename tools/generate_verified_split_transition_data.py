#!/usr/bin/env python3
from __future__ import annotations

import re
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
PREFIX_PATH = (
    ROOT
    / "entry"
    / "src"
    / "main"
    / "cpp"
    / "hosdict_engine"
    / "prototype_verified_split_data.h"
)
TRANSITION_PATH = (
    ROOT
    / "entry"
    / "src"
    / "main"
    / "cpp"
    / "hosdict_engine"
    / "prototype_transition_data.h"
)
TARGET = (
    ROOT
    / "entry"
    / "src"
    / "main"
    / "cpp"
    / "hosdict_engine"
    / "prototype_verified_transition_data.h"
)


PREFIX_RE = re.compile(r'"([^"]+)"')
RECORD_RE = re.compile(
    r'\{"([^"]+)",\s*(true|false),\s*(true|false),\s*"([^"]*)",\s*"([^"]*)"\}'
)


def read_verified_prefixes() -> list[str]:
    text = PREFIX_PATH.read_text(encoding="utf-8")
    prefixes: list[str] = []
    for line in text.splitlines():
      match = PREFIX_RE.search(line)
      if match:
        prefixes.append(match.group(1))
    return prefixes


def read_transition_records() -> dict[str, tuple[bool, bool, str, str]]:
    text = TRANSITION_PATH.read_text(encoding="utf-8")
    records: dict[str, tuple[bool, bool, str, str]] = {}
    for match in RECORD_RE.finditer(text):
        prefix, filter_direct, filter_handoff, direct_utf8, handoff_utf8 = match.groups()
        records[prefix] = (
            filter_direct == "true",
            filter_handoff == "true",
            direct_utf8,
            handoff_utf8,
        )
    return records


def render(
    prefixes: list[str], records: dict[str, tuple[bool, bool, str, str]]
) -> str:
    lines = [
        "#pragma once",
        "",
        "#include <cstddef>",
        "",
        "namespace hoskey::engine {",
        "",
        "inline constexpr PrototypeTransitionRecord kPrototypeVerifiedTransitionRecords[] = {",
    ]
    for prefix in prefixes:
        if prefix not in records:
            raise KeyError(f"missing verified transition record for prefix: {prefix}")
        filter_direct, filter_handoff, direct_utf8, handoff_utf8 = records[prefix]
        lines.append(
            '    {"%s", %s, %s, "%s", "%s"},'
            % (
                prefix.replace("\\", "\\\\").replace('"', '\\"'),
                "true" if filter_direct else "false",
                "true" if filter_handoff else "false",
                direct_utf8.replace("\\", "\\\\").replace('"', '\\"'),
                handoff_utf8.replace("\\", "\\\\").replace('"', '\\"'),
            )
        )
    lines += [
        "};",
        "",
        "inline constexpr std::size_t kPrototypeVerifiedTransitionRecordCount =",
        "    sizeof(kPrototypeVerifiedTransitionRecords) / sizeof(kPrototypeVerifiedTransitionRecords[0]);",
        "",
        "}  // namespace hoskey::engine",
        "",
    ]
    return "\n".join(lines)


def main() -> None:
    prefixes = read_verified_prefixes()
    records = read_transition_records()
    TARGET.write_text(render(prefixes, records), encoding="utf-8", newline="\n")
    print(f"wrote {len(prefixes)} verified transition records -> {TARGET}")


if __name__ == "__main__":
    main()
