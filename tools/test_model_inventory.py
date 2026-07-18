import tempfile
import unittest
from pathlib import Path

import model_inventory


class ModelInventoryAliasTest(unittest.TestCase):
    def test_raw_same_basename_tflite_is_not_implicit_provenance(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            raw = root / "search_emoji_model"
            raw.write_bytes(b"raw mapping container")
            (root / "search_emoji_model.tflite").write_bytes(
                b"\x00\x00\x00\x00TFL3")
            analyzer_calls: list[Path] = []

            def analyzer(path: Path):
                analyzer_calls.append(path)
                return {"inputs": [{"shape": [1, 1]}],
                        "outputs": [{"shape": [1, 1, 525]}]}

            record = model_inventory.analyze_record(
                raw.name, "raw", raw, analyzer, root)
            self.assertIsNone(record.alias_tflite)
            self.assertEqual([], record.inputs)
            self.assertEqual([], record.outputs)
            self.assertEqual([], analyzer_calls)
            self.assertIn("implicit alias rejected", record.note)

    def test_direct_tflite_is_still_analyzed(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "direct.tflite"
            path.write_bytes(b"\x00\x00\x00\x00TFL3")

            def analyzer(_: Path):
                return {"inputs": [{"shape": [1]}],
                        "outputs": [{"shape": [1]}],
                        "operators": ["ADD"]}

            record = model_inventory.analyze_record(
                path.name, "tflite", path, analyzer, path.parent)
            self.assertEqual([[1]], [item["shape"] for item in record.inputs])
            self.assertEqual(["ADD"], record.operators)


if __name__ == "__main__":
    unittest.main()
