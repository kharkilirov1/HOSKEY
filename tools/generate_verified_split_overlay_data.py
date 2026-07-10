#!/usr/bin/env python3
from __future__ import annotations

from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
COMPARE_PATH = ROOT / "reverse" / "R3_keyboard_engine_compare_split_2026-04-10.txt"
TARGET = (
    ROOT
    / "entry"
    / "src"
    / "main"
    / "cpp"
    / "hosdict_engine"
    / "prototype_verified_split_data.h"
)


def parse_prefixes(text: str) -> list[str]:
    marker = "overlay_promoted_split_prefixes="
    for line in text.splitlines():
      if line.startswith(marker):
        payload = line[len(marker):].strip()
        if payload == "-" or not payload:
          return []
        return [part for part in payload.split(",") if part]
    return []


def escape_cpp(value: str) -> str:
    return value.replace("\\", "\\\\").replace('"', '\\"')


def render(prefixes: list[str]) -> str:
    lines = [
        "#pragma once",
        "",
        "#include <array>",
        "#include <string_view>",
        "",
        "namespace hoskey::engine {",
        "",
        "inline constexpr std::array<std::string_view, "
        + str(len(prefixes))
        + "> kPrototypeVerifiedSplitPrefixes = {",
    ]
    for prefix in prefixes:
        lines.append(f'    "{escape_cpp(prefix)}",')
    lines += [
        "};",
        "",
        "}  // namespace hoskey::engine",
        "",
    ]
    return "\n".join(lines)


def main() -> None:
    prefixes = parse_prefixes(COMPARE_PATH.read_text(encoding="utf-8"))
    TARGET.write_text(render(prefixes), encoding="utf-8", newline="\n")
    print(f"wrote {len(prefixes)} verified split prefixes -> {TARGET}")


if __name__ == "__main__":
    main()
