#!/usr/bin/env python3
"""Strict structural and provenance-aware gate for HSKD v1 containers."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import struct
import sys
import zipfile
from collections.abc import Callable
from dataclasses import dataclass
from pathlib import Path
from typing import Any


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_HOSDICT = (
    ROOT / "entry/src/main/resources/rawfile/dict/ru.hoskey.hosdict"
)

HSKD_MAGIC = 0x444B5348  # "HSKD" little-endian
HSKD_VERSION = 1
ALIGNMENT = 16
MANIFEST_VERSION = "hosdict.manifest/v2"
ENGINE_ABI = "hoskey.engine/v2"
SHA256_RE = re.compile(r"[0-9a-fA-F]{64}\Z")


class ValidationError(ValueError):
    """The container or package violates a format invariant."""


class ProvenanceBlocked(ValidationError):
    """Structure is valid, but required provenance cannot be established."""


@dataclass(frozen=True)
class AssetEntry:
    key: str
    format: str
    offset: int
    size: int

    @property
    def end(self) -> int:
        return self.offset + self.size


@dataclass(frozen=True)
class ValidationReport:
    version: int
    manifest: dict[str, Any]
    entries: tuple[AssetEntry, ...]
    index_end: int
    data_start: int
    sha256: str
    blockers: tuple[str, ...]


class _DuplicateManifestKey(ValueError):
    def __init__(self, key: str) -> None:
        super().__init__(key)
        self.key = key


def _align(value: int) -> int:
    return (value + ALIGNMENT - 1) // ALIGNMENT * ALIGNMENT


def _require_span(total: int, offset: int, size: int, label: str) -> None:
    if offset < 0 or size < 0 or offset > total or size > total - offset:
        raise ValidationError(f"{label} is outside file bounds")


def _decode_utf8(raw: bytes, label: str) -> str:
    try:
        return raw.decode("utf-8")
    except UnicodeDecodeError as exc:
        raise ValidationError(f"{label} is not valid UTF-8: {exc}") from exc


def _json_object(pairs: list[tuple[str, Any]]) -> dict[str, Any]:
    result: dict[str, Any] = {}
    for key, value in pairs:
        if key in result:
            raise _DuplicateManifestKey(key)
        result[key] = value
    return result


def _reject_json_constant(value: str) -> None:
    raise ValidationError(f"invalid manifest JSON constant {value}")


def _parse_manifest(raw: bytes) -> dict[str, Any]:
    text = _decode_utf8(raw, "manifest")
    try:
        value = json.loads(
            text,
            object_pairs_hook=_json_object,
            parse_constant=_reject_json_constant,
        )
    except _DuplicateManifestKey as exc:
        raise ValidationError(
            f"duplicate manifest key '{exc.key}' is not allowed"
        ) from exc
    except json.JSONDecodeError as exc:
        raise ValidationError(f"invalid manifest JSON: {exc.msg}") from exc
    if not isinstance(value, dict):
        raise ValidationError("manifest JSON root must be an object")
    return value


def _require_manifest_string(
    manifest: dict[str, Any],
    field: str,
    *,
    expected: str | None = None,
) -> str:
    value = manifest.get(field)
    if not isinstance(value, str) or not value:
        raise ValidationError(
            f"manifest field '{field}' must be a non-empty string"
        )
    if expected is not None and value != expected:
        raise ValidationError(
            f"manifest field '{field}' must be '{expected}', got '{value}'"
        )
    return value


def _metadata_paths(
    value: Any,
    predicate: Callable[[str], bool],
    path: str = "$",
) -> list[tuple[str, Any]]:
    found: list[tuple[str, Any]] = []
    if isinstance(value, dict):
        for key, child in value.items():
            child_path = f"{path}.{key}"
            if predicate(key):
                found.append((child_path, child))
            found.extend(_metadata_paths(child, predicate, child_path))
    elif isinstance(value, list):
        for index, child in enumerate(value):
            found.extend(_metadata_paths(child, predicate, f"{path}[{index}]"))
    return found


def _validate_manifest_metadata(
    manifest: dict[str, Any],
) -> tuple[list[str], list[str]]:
    _require_manifest_string(
        manifest, "manifest_version", expected=MANIFEST_VERSION
    )
    _require_manifest_string(manifest, "engine_abi", expected=ENGINE_ABI)
    _require_manifest_string(manifest, "package_name")
    _require_manifest_string(manifest, "locale")

    sha_paths: list[str] = []
    for path, value in _metadata_paths(
        manifest, lambda key: key.casefold() == "sha256"
    ):
        if not isinstance(value, str) or SHA256_RE.fullmatch(value) is None:
            raise ValidationError(
                f"manifest sha256 metadata at {path} must be 64 hexadecimal characters"
            )
        sha_paths.append(path)

    variant_paths = [
        path
        for path, _ in _metadata_paths(
            manifest, lambda key: "variant" in key.casefold()
        )
    ]
    return sha_paths, variant_paths


def _read_u32(data: bytes, offset: int, label: str) -> int:
    _require_span(len(data), offset, 4, label)
    return struct.unpack_from("<I", data, offset)[0]


def _parse_entries(
    data: bytes,
    cursor: int,
    asset_count: int,
) -> tuple[tuple[AssetEntry, ...], int]:
    entries: list[AssetEntry] = []
    for index in range(asset_count):
        key_length = _read_u32(
            data, cursor, f"asset index entry {index} key length"
        )
        cursor += 4
        _require_span(
            len(data), cursor, key_length, f"asset index entry {index} key"
        )
        key = _decode_utf8(
            data[cursor:cursor + key_length], f"asset index entry {index} key"
        )
        cursor += key_length

        format_length = _read_u32(
            data, cursor, f"asset index entry {index} format length"
        )
        cursor += 4
        _require_span(
            len(data),
            cursor,
            format_length,
            f"asset index entry {index} format",
        )
        asset_format = _decode_utf8(
            data[cursor:cursor + format_length],
            f"asset index entry {index} format",
        )
        cursor += format_length

        _require_span(
            len(data),
            cursor,
            16,
            f"asset index entry {index} offset/size",
        )
        offset, size = struct.unpack_from("<QQ", data, cursor)
        cursor += 16
        entries.append(AssetEntry(key, asset_format, offset, size))
    return tuple(entries), cursor


def _validate_entries(
    entries: tuple[AssetEntry, ...],
    *,
    file_size: int,
    data_start: int,
) -> None:
    seen: dict[str, int] = {}
    for index, entry in enumerate(entries):
        previous = seen.get(entry.key)
        if previous is not None:
            raise ValidationError(
                f"duplicate asset key '{entry.key}' at entries {previous} and {index}; "
                "runtime is first-wins while dict/map validators are last-wins"
            )
        seen[entry.key] = index

    for entry in entries:
        if (
            entry.offset > file_size
            or entry.size > file_size - entry.offset
        ):
            raise ValidationError(
                f"asset '{entry.key}' data span [{entry.offset}, "
                f"{entry.offset + entry.size}) is outside file bounds"
            )
        if entry.offset % ALIGNMENT != 0:
            raise ValidationError(
                f"asset '{entry.key}' offset {entry.offset} is not "
                f"{ALIGNMENT}-byte aligned"
            )
        if entry.offset < data_start:
            raise ValidationError(
                f"asset '{entry.key}' data at {entry.offset} overlaps "
                f"header/manifest/index ending at aligned offset {data_start}"
            )

    non_empty = sorted(
        (entry for entry in entries if entry.size > 0),
        key=lambda entry: (entry.offset, entry.end, entry.key),
    )
    if not non_empty:
        return
    active = non_empty[0]
    for entry in non_empty[1:]:
        if entry.offset < active.end:
            raise ValidationError(
                f"asset data overlap: '{active.key}' [{active.offset}, {active.end}) "
                f"overlaps '{entry.key}' [{entry.offset}, {entry.end})"
            )
        if entry.end > active.end:
            active = entry


def validate_hosdict(
    data: bytes,
    *,
    expected_sha256: str | None = None,
    require_variant_manifest: bool = False,
) -> ValidationReport:
    if len(data) < 16:
        raise ValidationError("container is too small for the 16-byte HSKD header")

    magic, version, asset_count, manifest_size = struct.unpack_from(
        "<IIII", data, 0
    )
    if magic != HSKD_MAGIC:
        raise ValidationError(
            f"container magic mismatch: expected HSKD, got 0x{magic:08x}"
        )
    if version != HSKD_VERSION:
        raise ValidationError(
            f"unsupported container version {version}; expected {HSKD_VERSION}"
        )

    manifest_offset = 16
    _require_span(
        len(data), manifest_offset, manifest_size, "manifest"
    )
    manifest_end = manifest_offset + manifest_size
    manifest = _parse_manifest(data[manifest_offset:manifest_end])
    sha_paths, variant_paths = _validate_manifest_metadata(manifest)

    entries, index_end = _parse_entries(data, manifest_end, asset_count)
    data_start = _align(index_end)
    _validate_entries(entries, file_size=len(data), data_start=data_start)

    digest = hashlib.sha256(data).hexdigest()
    if expected_sha256 is not None:
        if SHA256_RE.fullmatch(expected_sha256) is None:
            raise ValidationError(
                "expected SHA-256 must contain exactly 64 hexadecimal characters"
            )
        if digest != expected_sha256.casefold():
            raise ValidationError(
                f"package SHA-256 mismatch: expected {expected_sha256.casefold()}, "
                f"got {digest}"
            )

    blockers: list[str] = []
    if variant_paths:
        blockers.append(
            "variant metadata is present at "
            + ", ".join(variant_paths)
            + ", but the current HSKD v1 manifest has no defined schema; "
              "provenance cannot be validated"
        )
    else:
        blockers.append(
            "variant manifest is absent; the current HSKD v1 format defines "
            "no variant metadata schema, so provenance cannot be validated"
        )
    if sha_paths:
        blockers.append(
            "embedded sha256 metadata at "
            + ", ".join(sha_paths)
            + " is syntactically valid, but HSKD v1 defines no binding from "
              "those fields to index entries; content integrity is not verified"
        )

    report = ValidationReport(
        version=version,
        manifest=manifest,
        entries=entries,
        index_end=index_end,
        data_start=data_start,
        sha256=digest,
        blockers=tuple(blockers),
    )
    if require_variant_manifest:
        raise ProvenanceBlocked(blockers[0])
    return report


def validate_hosdict_file(
    path: Path,
    *,
    expected_sha256: str | None = None,
    require_variant_manifest: bool = False,
) -> ValidationReport:
    try:
        data = path.read_bytes()
    except OSError as exc:
        raise ValidationError(f"cannot read .hosdict '{path}': {exc}") from exc
    return validate_hosdict(
        data,
        expected_sha256=expected_sha256,
        require_variant_manifest=require_variant_manifest,
    )


def load_hosdict_source(
    *,
    hosdict_path: Path | None = None,
    hap_path: Path | None = None,
) -> tuple[bytes, str]:
    if (hosdict_path is None) == (hap_path is None):
        raise ValidationError("select exactly one of hosdict_path or hap_path")

    if hosdict_path is not None:
        try:
            return hosdict_path.read_bytes(), f"hosdict={hosdict_path}"
        except OSError as exc:
            raise ValidationError(
                f"cannot read .hosdict '{hosdict_path}': {exc}"
            ) from exc

    assert hap_path is not None
    try:
        with zipfile.ZipFile(hap_path) as archive:
            infos = archive.infolist()
            seen: set[str] = set()
            duplicates: set[str] = set()
            for info in infos:
                if info.filename in seen:
                    duplicates.add(info.filename)
                seen.add(info.filename)
            if duplicates:
                raise ValidationError(
                    "duplicate ZIP member names are ambiguous: "
                    + ", ".join(sorted(duplicates))
                )

            hosdict_infos = [
                info
                for info in infos
                if not info.is_dir() and info.filename.casefold().endswith(".hosdict")
            ]
            if len(hosdict_infos) != 1:
                names = [info.filename for info in hosdict_infos]
                raise ValidationError(
                    f"expected exactly one .hosdict in HAP, got {names}"
                )
            info = hosdict_infos[0]
            return archive.read(info), f"hap={hap_path}::{info.filename}"
    except ValidationError:
        raise
    except (OSError, zipfile.BadZipFile) as exc:
        raise ValidationError(f"cannot read HAP '{hap_path}': {exc}") from exc


def _print_report(
    heading: str,
    source: str,
    report: ValidationReport,
    *,
    stream: Any = sys.stdout,
    allowed: bool = False,
) -> None:
    print(heading, file=stream)
    print(f"  source={source}", file=stream)
    print(f"  container_version={report.version}", file=stream)
    print(f"  assets={len(report.entries)}", file=stream)
    print(f"  index_end={report.index_end}", file=stream)
    print(f"  data_start={report.data_start}", file=stream)
    print(f"  sha256={report.sha256}", file=stream)
    prefix = "BLOCKER (allowed)" if allowed else "BLOCKER"
    for blocker in report.blockers:
        print(f"  {prefix}: {blocker}", file=stream)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        description="Validate HSKD v1 structure without hiding provenance gaps"
    )
    source = parser.add_mutually_exclusive_group()
    source.add_argument("--hosdict", type=Path, help="raw .hosdict to validate")
    source.add_argument("--hap", type=Path, help="HAP containing one .hosdict")
    parser.add_argument(
        "--expected-sha256",
        help="optional out-of-band SHA-256 bound to the complete .hosdict bytes",
    )
    parser.add_argument(
        "--allow-missing-variant-manifest",
        action="store_true",
        help=(
            "return success for structural diagnostics only; the missing variant "
            "manifest remains printed as an allowed blocker"
        ),
    )
    args = parser.parse_args(argv)

    hosdict_path = args.hosdict
    if hosdict_path is None and args.hap is None:
        hosdict_path = DEFAULT_HOSDICT

    try:
        data, source_label = load_hosdict_source(
            hosdict_path=hosdict_path, hap_path=args.hap
        )
        report = validate_hosdict(data, expected_sha256=args.expected_sha256)
    except ValidationError as exc:
        print(f"STRICT HOSDICT GATE FAIL: {exc}", file=sys.stderr)
        return 1

    missing_variant_only = (
        len(report.blockers) == 1
        and report.blockers[0].startswith("variant manifest is absent;")
    )
    if report.blockers and not (
        args.allow_missing_variant_manifest and missing_variant_only
    ):
        _print_report(
            "STRICT HOSDICT GATE BLOCKED",
            source_label,
            report,
            stream=sys.stderr,
        )
        return 2

    if report.blockers:
        _print_report(
            "STRICT HOSDICT STRUCTURE OK",
            source_label,
            report,
            allowed=True,
        )
    else:
        _print_report("STRICT HOSDICT GATE OK", source_label, report)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
