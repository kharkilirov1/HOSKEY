import tempfile
import unittest
from pathlib import Path
from unittest import mock

import build_hosdict


class BuildHosdictPinsTest(unittest.TestCase):
    def test_default_pack_contains_production_emoji_search_model(self) -> None:
        assets = build_hosdict.build_asset_list()
        matches = [asset for asset in assets
                   if asset.key == "emoji.search_model.raw"]
        self.assertEqual(1, len(matches))
        self.assertEqual("emoji_search_comptrie_v1", matches[0].fmt)

    def test_unpinned_asset_is_unchanged(self) -> None:
        build_hosdict.validate_pinned_asset("config", b"anything")

    def test_pinned_asset_rejects_wrong_bytes(self) -> None:
        with self.assertRaisesRegex(ValueError, "SHA-256"):
            build_hosdict.validate_pinned_asset(
                "ranker_default_model.bin", b"wrong")

    def test_default_tap_chain_is_hash_pinned(self) -> None:
        for label in (
                "tap_models_default_1225",
                "features_tap_model_1225.json",
                "tap_model_ranker_1225.hobl",
                "tap_model_classifier_1225.hobl"):
            with self.subTest(label=label):
                with self.assertRaisesRegex(ValueError, "SHA-256"):
                    build_hosdict.validate_pinned_asset(label, b"wrong")

    def test_default_ynnlm_1225_chain_is_atomic_and_hash_pinned(self) -> None:
        assets = build_hosdict.build_asset_list()
        neural = {
            asset.key: asset
            for asset in assets
            if asset.key == "neural.word_model.raw"
            or asset.key.startswith("neural.word.")
            or asset.key.startswith("neural.subword.")
            or asset.key == "neural.char.dec"
        }
        self.assertEqual(
            {
                "neural.word_model.raw",
                "neural.word.emb_age",
                "neural.word.emb_app",
                "neural.word.emb_sex",
                "neural.word.emb",
                "neural.word.enc",
                "neural.word.enc_dec",
                "neural.word.dec",
                "neural.subword.enc_dec",
                "neural.subword.dec",
                "neural.char.dec",
            },
            set(neural),
        )
        self.assertEqual(
            "ynnlm_char_decoder_v1", neural["neural.char.dec"].fmt)
        for asset in neural.values():
            with self.subTest(label=asset.label):
                with self.assertRaisesRegex(ValueError, "SHA-256"):
                    build_hosdict.validate_pinned_asset(asset.label, b"wrong")

    def test_rnnt_predictor_is_separate_pinned_mindspore_asset(self) -> None:
        assets = {asset.key: asset
                  for asset in build_hosdict.build_asset_list()}
        predictor = assets["rnnt.predictor.char_dec"]
        exact_decoder = assets["neural.char.dec"]

        self.assertEqual("mindspore_model", predictor.fmt)
        self.assertEqual("char_dec.ms", predictor.label)
        self.assertNotEqual(predictor.key, exact_decoder.key)
        self.assertNotEqual(Path(predictor.source), Path(exact_decoder.source))
        with self.assertRaisesRegex(ValueError, "SHA-256"):
            build_hosdict.validate_pinned_asset(predictor.label, b"wrong")

    def test_incomplete_ynnlm_1225_chain_is_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            Path(temp_dir, "emb_app.ms").write_bytes(b"partial")
            with mock.patch.object(
                    build_hosdict, "YNNLM_1225_MS_MODELS_DIR", temp_dir):
                with self.assertRaisesRegex(
                        FileNotFoundError, "incomplete Yandex 122.5 YNNLM"):
                    build_hosdict.build_asset_list()

    def test_default_pack_contains_atomic_tap_1225_chain(self) -> None:
        assets = build_hosdict.build_asset_list()
        tap = {asset.key: asset for asset in assets if asset.key.startswith("tap.")
               or asset.key == "features.tap_model"}
        self.assertEqual(
            {
                "tap.models_default",
                "features.tap_model",
                "tap.ranker.hobl",
                "tap.classifier.hobl",
            },
            set(tap),
        )
        self.assertEqual("tap_model_bundle", tap["tap.models_default"].fmt)
        self.assertEqual("feature_json", tap["features.tap_model"].fmt)
        self.assertEqual("oblivious_tree_v1", tap["tap.ranker.hobl"].fmt)
        self.assertEqual("oblivious_tree_v1", tap["tap.classifier.hobl"].fmt)

    def test_full_pack_excludes_unproven_aliases_and_tap_experiments(self) -> None:
        keys = {
            asset.key
            for asset in build_hosdict.build_asset_list(include_full_models=True)
        }
        excluded = {
            "features.experiments.KEYBOARD-13421_tap_model",
            "tap.model_ranker.KEYBOARD-13321",
            "tap.model_classifier.KEYBOARD-13321",
            "tap.model_ranker.KEYBOARD-13421",
            "tap.model_classifier.KEYBOARD-13421",
            "tap.model_ranker.tflite",
            "tap.model_classifier.tflite",
            "tap.model_ranker.KEYBOARD-13321.tflite",
            "tap.model_classifier.KEYBOARD-13321.tflite",
            "tap.model_ranker.KEYBOARD-13421.tflite",
            "tap.model_classifier.KEYBOARD-13421.tflite",
            "autocc.tree_model.tflite",
            "autocc.tree_model.KEYBOARD-13321.tflite",
            "autocc.tree_model.KEYBOARD-13421.tflite",
            "emoji.search_model.tflite",
            "emoji.suggest_model.tflite",
            "emoji.suggest_model.KEYBOARD-13321.tflite",
        }
        self.assertTrue(excluded.isdisjoint(keys), sorted(excluded & keys))

    def test_tflite_alias_requires_tfl3_magic(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            model = Path(temp_dir) / "bad.tflite"
            model.write_bytes(b"not-a-flatbuffer")
            assets = []
            with mock.patch.object(build_hosdict, "TFLITE_MODELS_DIR", temp_dir):
                with self.assertRaisesRegex(ValueError, "TFL3"):
                    build_hosdict.append_tflite_model_if_exists(
                        assets, "audit.bad", "bad")
            self.assertEqual([], assets)


if __name__ == "__main__":
    unittest.main()
