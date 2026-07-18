import math
import unittest
from pathlib import Path

import validate_tap_bundle as validator


BUNDLE = Path(
    r"C:\Users\Kharki\Desktop\yandex_extracted\extracted_sections\tap_models_default_1225")


class TapBundleValidatorTest(unittest.TestCase):
    def test_default_bundle_exact_schema_and_goldens(self) -> None:
        parsed = validator.parse_tap_bundle(BUNDLE.read_bytes())
        self.assertEqual(135315, parsed.frame_size)
        self.assertEqual(6, len(parsed.layouts))

        android = parsed.layouts["android_ru_east_slavic_separate_comma"]
        self.assertEqual((1080, 631, 755),
                         (android.width, android.height,
                          len(android.key_params)))
        key = android.key_params["а"]
        self.assertEqual((295, 169, 99, 154),
                         (key.x, key.y, key.width, key.height))
        expected = [0.07528141140937805, 347.3800354003906,
                    245.57289123535156, 445.8924865722656,
                    505.9942626953125, 0.05216166749596596]
        actual = [key.log_prior, key.mean_x, key.mean_y,
                  key.variance_x, key.variance_y, key.correlation]
        self.assertTrue(all(math.isclose(left, right, rel_tol=0, abs_tol=1e-7)
                            for left, right in zip(actual, expected)))

        ios = parsed.layouts["ios_ru"]
        self.assertEqual((1080, 621, 579),
                         (ios.width, ios.height, len(ios.key_params)))

    def test_truncated_and_trailing_frames_are_rejected(self) -> None:
        data = BUNDLE.read_bytes()
        with self.assertRaises(ValueError):
            validator.parse_tap_bundle(data[:-1])
        with self.assertRaises(ValueError):
            validator.parse_tap_bundle(data + b"x")


if __name__ == "__main__":
    unittest.main()
