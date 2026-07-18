#!/usr/bin/env python3

from __future__ import annotations

import hashlib
import json
import tempfile
import unittest
from pathlib import Path

from build_autocc_models import (
    EXPECTED_OUTPUTS,
    FEATURE_SCHEMA_SHA256,
    FEATURE_SCHEMA_SIZE,
    build_models,
)


class BuildAutoccModelsTest(unittest.TestCase):
    def test_reproducible_exact_three_variant_bundle(self) -> None:
        with tempfile.TemporaryDirectory(prefix="hoskey-autocc-") as temp:
            output_dir = Path(temp)
            metadata = build_models(output_dir)

            self.assertEqual(set(metadata["models"]), set(EXPECTED_OUTPUTS))
            for name, expected in EXPECTED_OUTPUTS.items():
                path = output_dir / name
                self.assertTrue(path.is_file(), name)
                data = path.read_bytes()
                self.assertEqual(len(data), expected["size"], name)
                self.assertEqual(hashlib.sha256(data).hexdigest(),
                                 expected["sha256"], name)

            self.assertEqual(
                (output_dir / "autocc_default.hobl").read_bytes(),
                (output_dir / "autocc_KEYBOARD-13421.hobl").read_bytes(),
            )

            provenance_path = output_dir / "autocc_models.provenance.json"
            self.assertTrue(provenance_path.is_file())
            on_disk = json.loads(provenance_path.read_text(encoding="utf-8"))
            self.assertEqual(on_disk, metadata)
            self.assertEqual(metadata["feature_count"], 79)
            self.assertEqual(metadata["feature_schema"]["size"],
                             FEATURE_SCHEMA_SIZE)
            self.assertEqual(metadata["feature_schema"]["sha256"],
                             FEATURE_SCHEMA_SHA256)
            self.assertEqual(metadata["tree_count"], 2000)
            self.assertEqual(metadata["depth"], 6)
            self.assertEqual(metadata["threshold"], 0.65)
            self.assertEqual(
                metadata["models"]["autocc_default.hobl"]["promoted_from"],
                "KEYBOARD-13421",
            )
            self.assertEqual(
                metadata["live_default_identity"]["dictionary_version"],
                "67.0",
            )
            self.assertEqual(
                metadata["live_default_identity"]["source_sha256"],
                "b0fedf03cc32a264e27d96f5867a82f75a7f4e60bdb71553618090073d593224",
            )


if __name__ == "__main__":
    unittest.main()
