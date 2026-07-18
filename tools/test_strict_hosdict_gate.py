from __future__ import annotations

import hashlib
import json
import struct
import subprocess
import sys
import tempfile
import unittest
import warnings
import zipfile
from pathlib import Path


TOOLS = Path(__file__).resolve().parent
ROOT = TOOLS.parent
CURRENT_HOSDICT = (
    ROOT / "entry/src/main/resources/rawfile/dict/ru.hoskey.hosdict"
)
GATE_SCRIPT = TOOLS / "strict_hosdict_gate.py"

sys.path.insert(0, str(TOOLS))
import strict_hosdict_gate as gate  # noqa: E402


def _manifest(**overrides: object) -> dict[str, object]:
    value: dict[str, object] = {
        "manifest_version": "hosdict.manifest/v2",
        "engine_abi": "hoskey.engine/v2",
        "package_name": "ru.hoskey.test",
        "locale": "ru-RU",
    }
    value.update(overrides)
    return value


def _align(value: int, alignment: int = 16) -> int:
    return (value + alignment - 1) // alignment * alignment


def _build_hosdict(
    entries: list[tuple[str, str, bytes]] | None = None,
    *,
    manifest: object | None = None,
    manifest_bytes: bytes | None = None,
    magic: int = 0x444B5348,
    version: int = 1,
    relative_offsets: list[int] | None = None,
) -> tuple[bytes, list[int]]:
    if entries is None:
        entries = [("config", "feature_json", b"{}")]
    if manifest_bytes is None:
        if manifest is None:
            manifest = _manifest()
        manifest_bytes = json.dumps(manifest, ensure_ascii=False).encode("utf-8")

    encoded = [
        (key.encode("utf-8"), fmt.encode("utf-8"), payload)
        for key, fmt, payload in entries
    ]
    index_size = sum(4 + len(key) + 4 + len(fmt) + 16 for key, fmt, _ in encoded)
    data_start = _align(16 + len(manifest_bytes) + index_size)

    if relative_offsets is None:
        offsets = []
        cursor = data_start
        for _, _, payload in encoded:
            offsets.append(cursor)
            cursor = _align(cursor + len(payload))
    else:
        if len(relative_offsets) != len(encoded):
            raise AssertionError("relative_offsets must match entries")
        offsets = [data_start + delta for delta in relative_offsets]

    out = bytearray(struct.pack(
        "<IIII", magic, version, len(encoded), len(manifest_bytes)
    ))
    out.extend(manifest_bytes)
    span_positions: list[int] = []
    for (key, fmt, payload), offset in zip(encoded, offsets):
        out.extend(struct.pack("<I", len(key)))
        out.extend(key)
        out.extend(struct.pack("<I", len(fmt)))
        out.extend(fmt)
        span_positions.append(len(out))
        out.extend(struct.pack("<QQ", offset, len(payload)))

    final_size = max(
        [data_start, *[offset + len(payload)
                       for (_, _, payload), offset in zip(encoded, offsets)]]
    )
    out.extend(b"\0" * (final_size - len(out)))
    for (_, _, payload), offset in zip(encoded, offsets):
        out[offset:offset + len(payload)] = payload
    return bytes(out), span_positions


class StrictHosdictGateTest(unittest.TestCase):
    def test_valid_in_memory_fixture_is_structurally_valid_but_variant_blocked(
        self,
    ) -> None:
        data, _ = _build_hosdict()

        report = gate.validate_hosdict(data)

        self.assertEqual("ru.hoskey.test", report.manifest["package_name"])
        self.assertEqual(["config"], [entry.key for entry in report.entries])
        self.assertRegex("\n".join(report.blockers), r"variant manifest.*absent")

    def test_strict_mode_rejects_missing_variant_manifest(self) -> None:
        data, _ = _build_hosdict()

        with self.assertRaisesRegex(gate.ProvenanceBlocked,
                                    r"variant manifest.*absent"):
            gate.validate_hosdict(data, require_variant_manifest=True)

    def test_unversioned_variant_field_is_not_mistaken_for_valid_metadata(self) -> None:
        data, _ = _build_hosdict(manifest=_manifest(variant="default"))

        report = gate.validate_hosdict(data)

        self.assertRegex("\n".join(report.blockers),
                         r"variant metadata.*no defined schema")

    def test_rejects_bad_magic(self) -> None:
        data, _ = _build_hosdict(magic=0xDEADBEEF)

        with self.assertRaisesRegex(gate.ValidationError, "magic"):
            gate.validate_hosdict(data)

    def test_rejects_bad_container_version(self) -> None:
        data, _ = _build_hosdict(version=2)

        with self.assertRaisesRegex(gate.ValidationError, "version"):
            gate.validate_hosdict(data)

    def test_rejects_manifest_out_of_bounds(self) -> None:
        data, _ = _build_hosdict()
        broken = bytearray(data)
        struct.pack_into("<I", broken, 12, len(data) + 1)

        with self.assertRaisesRegex(gate.ValidationError, "manifest.*bounds"):
            gate.validate_hosdict(bytes(broken))

    def test_rejects_invalid_manifest_json(self) -> None:
        data, _ = _build_hosdict(manifest_bytes=b"{")

        with self.assertRaisesRegex(gate.ValidationError, "manifest JSON"):
            gate.validate_hosdict(data)

    def test_rejects_non_standard_manifest_json_constants(self) -> None:
        raw = json.dumps(_manifest()).encode("utf-8")[:-1] + b', "score": NaN}'
        data, _ = _build_hosdict(manifest_bytes=raw)

        with self.assertRaisesRegex(gate.ValidationError,
                                    r"manifest JSON.*NaN"):
            gate.validate_hosdict(data)

    def test_rejects_duplicate_manifest_keys(self) -> None:
        raw = (
            b'{"manifest_version":"hosdict.manifest/v2",'
            b'"engine_abi":"hoskey.engine/v2",'
            b'"package_name":"first","package_name":"last",'
            b'"locale":"ru-RU"}'
        )
        data, _ = _build_hosdict(manifest_bytes=raw)

        with self.assertRaisesRegex(gate.ValidationError,
                                    "duplicate manifest key.*package_name"):
            gate.validate_hosdict(data)

    def test_rejects_invalid_required_manifest_metadata(self) -> None:
        for field, value in (
            ("manifest_version", "hosdict.manifest/v1"),
            ("engine_abi", 2),
            ("package_name", ""),
            ("locale", None),
        ):
            with self.subTest(field=field):
                data, _ = _build_hosdict(manifest=_manifest(**{field: value}))
                with self.assertRaisesRegex(gate.ValidationError, field):
                    gate.validate_hosdict(data)

    def test_rejects_malformed_embedded_sha256_metadata(self) -> None:
        data, _ = _build_hosdict(manifest=_manifest(sha256="not-a-sha256"))

        with self.assertRaisesRegex(gate.ValidationError, "sha256"):
            gate.validate_hosdict(data)

    def test_validates_explicit_package_sha256(self) -> None:
        data, _ = _build_hosdict()
        expected = hashlib.sha256(data).hexdigest().upper()

        report = gate.validate_hosdict(data, expected_sha256=expected)

        self.assertEqual(expected.lower(), report.sha256)
        with self.assertRaisesRegex(gate.ValidationError, "SHA-256 mismatch"):
            gate.validate_hosdict(data, expected_sha256="0" * 64)

    def test_rejects_malformed_expected_sha256(self) -> None:
        data, _ = _build_hosdict()

        with self.assertRaisesRegex(gate.ValidationError,
                                    "expected SHA-256"):
            gate.validate_hosdict(data, expected_sha256="xyz")

    def test_rejects_asset_out_of_bounds(self) -> None:
        data, spans = _build_hosdict()
        broken = bytearray(data)
        struct.pack_into("<Q", broken, spans[0], len(data) + 16)

        with self.assertRaisesRegex(gate.ValidationError,
                                    r"config.*outside file bounds"):
            gate.validate_hosdict(bytes(broken))

    def test_rejects_asset_overlap(self) -> None:
        data, _ = _build_hosdict(
            [("first", "raw", b"AAAA"), ("second", "raw", b"BBBB")],
            relative_offsets=[0, 0],
        )

        with self.assertRaisesRegex(gate.ValidationError,
                                    r"overlap.*first.*second"):
            gate.validate_hosdict(data)

    def test_rejects_asset_overlap_with_index(self) -> None:
        data, spans = _build_hosdict()
        broken = bytearray(data)
        struct.pack_into("<Q", broken, spans[0], 16)

        with self.assertRaisesRegex(gate.ValidationError,
                                    r"config.*overlaps.*index"):
            gate.validate_hosdict(bytes(broken))

    def test_rejects_alignment_violation(self) -> None:
        data, _ = _build_hosdict(relative_offsets=[1])

        with self.assertRaisesRegex(gate.ValidationError,
                                    r"config.*16-byte aligned"):
            gate.validate_hosdict(data)

    def test_duplicate_name_cannot_hide_first_wins_runtime_from_last_wins_map(
        self,
    ) -> None:
        entries = [
            ("same", "runtime_first", b"FIRST"),
            ("same", "validator_last", b"LAST"),
        ]
        self.assertEqual("validator_last",
                         {key: fmt for key, fmt, _ in entries}["same"])
        data, _ = _build_hosdict(entries)

        with self.assertRaisesRegex(
            gate.ValidationError,
            r"duplicate asset key.*same.*runtime is first-wins.*last-wins",
        ):
            gate.validate_hosdict(data)

    def test_rejects_truncated_asset_index(self) -> None:
        data, spans = _build_hosdict()
        truncated = data[:spans[0] + 7]

        with self.assertRaisesRegex(gate.ValidationError,
                                    r"asset index.*offset/size"):
            gate.validate_hosdict(truncated)

    def test_loads_exactly_one_hosdict_from_temp_hap(self) -> None:
        data, _ = _build_hosdict()
        with tempfile.TemporaryDirectory() as tmp:
            hap = Path(tmp) / "fixture.hap"
            with zipfile.ZipFile(hap, "w") as archive:
                archive.writestr("resources/rawfile/dict/test.hosdict", data)

            loaded, label = gate.load_hosdict_source(hap_path=hap)

        self.assertEqual(data, loaded)
        self.assertIn("test.hosdict", label)

    def test_rejects_duplicate_zip_member_names(self) -> None:
        data, _ = _build_hosdict()
        with tempfile.TemporaryDirectory() as tmp:
            hap = Path(tmp) / "duplicate.hap"
            with warnings.catch_warnings():
                warnings.simplefilter("ignore", UserWarning)
                with zipfile.ZipFile(hap, "w") as archive:
                    archive.writestr("dict/test.hosdict", data)
                    archive.writestr("dict/test.hosdict", data)

            with self.assertRaisesRegex(gate.ValidationError,
                                        "duplicate ZIP member"):
                gate.load_hosdict_source(hap_path=hap)

    def test_cli_requires_explicit_override_for_variant_blocker(self) -> None:
        data, _ = _build_hosdict()
        with tempfile.TemporaryDirectory() as tmp:
            fixture = Path(tmp) / "fixture.hosdict"
            fixture.write_bytes(data)
            command = [sys.executable, str(GATE_SCRIPT), "--hosdict", str(fixture)]

            strict = subprocess.run(command, capture_output=True, text=True)
            allowed = subprocess.run(
                [*command, "--allow-missing-variant-manifest"],
                capture_output=True,
                text=True,
            )

        self.assertEqual(2, strict.returncode, strict.stdout + strict.stderr)
        self.assertIn("STRICT HOSDICT GATE BLOCKED", strict.stderr)
        self.assertEqual(0, allowed.returncode, allowed.stdout + allowed.stderr)
        self.assertIn("STRUCTURE OK", allowed.stdout)
        self.assertIn("BLOCKER (allowed)", allowed.stdout)

    def test_current_ru_hosdict_is_structurally_checked_not_false_passed(
        self,
    ) -> None:
        self.assertTrue(CURRENT_HOSDICT.is_file(), CURRENT_HOSDICT)

        report = gate.validate_hosdict_file(CURRENT_HOSDICT)

        self.assertEqual("ru.hoskey.default", report.manifest["package_name"])
        self.assertGreater(len(report.entries), 0)
        self.assertEqual(64, len(report.sha256))
        self.assertRegex("\n".join(report.blockers), r"variant manifest.*absent")


if __name__ == "__main__":
    unittest.main()
