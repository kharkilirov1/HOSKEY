import math
import unittest

from tools.capture_ranker_features import _find_pid, validate_feature_vector


def valid_vector() -> list[float]:
    values = [0.0] * 44
    values[0] = 3.0
    values[1] = 10.0
    values[2] = 2.0
    values[6] = 1.0
    values[7] = 5.0
    values[8] = 12.0
    values[9] = 0.5
    values[11] = 0.7
    values[13] = 1.0
    values[14] = 0.0
    values[15] = 0.0
    values[16] = 4.0
    values[17] = 0.8
    values[26] = 0.0
    values[28] = values[16] + values[8]
    values[29] = values[16] + values[11]
    values[30] = values[1] - values[8]
    values[31] = values[2] - values[9]
    values[32] = values[0] - values[7]
    values[43] = 0.0
    return values


def valid_swipe_backoff_vector() -> list[float]:
    values = [0.0] * 42
    values[0] = 420.0
    values[1] = 25.0
    values[2] = 10.0
    values[3] = 0.0
    values[4] = 6.0
    values[8] = 40.0
    values[9] = 900.0
    values[10] = 1200.0
    values[11] = 200.0
    values[12] = 180.0
    values[13:18] = [100.0, 300.0, 700.0, 175.0, 60.0]
    values[18:23] = [80.0, 300.0, 900.0, 225.0, 70.0]
    values[23:27] = [0.1, -0.1, 0.2, -0.2]
    values[27] = 600.0
    values[28] = 90.0
    values[29] = 110.0
    values[30:34] = [80.0, 140.0, 600.0 / 5.0, 20.0]
    values[34] = 100.0
    values[35:38] = [120.0, 80.0, 15.0]
    values[38:42] = [20.0, 350.0, 24.0, 70.0]
    return values


class ValidateFeatureVectorTest(unittest.TestCase):
    def test_accepts_exact_44_feature_contract(self) -> None:
        ok, reason = validate_feature_vector(valid_vector())
        self.assertTrue(ok, reason)

    def test_rejects_wrong_length(self) -> None:
        ok, reason = validate_feature_vector(valid_vector()[:-1])
        self.assertFalse(ok)
        self.assertIn("44", reason)

    def test_rejects_non_finite_value(self) -> None:
        values = valid_vector()
        values[8] = math.nan
        ok, reason = validate_feature_vector(values)
        self.assertFalse(ok)
        self.assertIn("finite", reason)

    def test_rejects_combo_sum_mismatch(self) -> None:
        values = valid_vector()
        values[28] += 0.01
        ok, reason = validate_feature_vector(values)
        self.assertFalse(ok)
        self.assertIn("feature 28", reason)

    def test_rejects_combo_sub_mismatch(self) -> None:
        values = valid_vector()
        values[30] -= 0.01
        ok, reason = validate_feature_vector(values)
        self.assertFalse(ok)
        self.assertIn("feature 30", reason)

    def test_rejects_second_combo_sum_mismatch(self) -> None:
        values = valid_vector()
        values[29] += 0.01
        ok, reason = validate_feature_vector(values)
        self.assertFalse(ok)
        self.assertIn("feature 29", reason)

    def test_rejects_static_combo_sub_mismatch(self) -> None:
        values = valid_vector()
        values[31] += 0.01
        ok, reason = validate_feature_vector(values)
        self.assertFalse(ok)
        self.assertIn("feature 31", reason)

    def test_rejects_length_combo_sub_mismatch(self) -> None:
        values = valid_vector()
        values[32] += 0.01
        ok, reason = validate_feature_vector(values)
        self.assertFalse(ok)
        self.assertIn("feature 32", reason)

    def test_rejects_non_boolean_flag(self) -> None:
        values = valid_vector()
        values[43] = 0.25
        ok, reason = validate_feature_vector(values)
        self.assertFalse(ok)
        self.assertIn("boolean", reason)

    def test_accepts_swipe_backoff_contract(self) -> None:
        ok, reason = validate_feature_vector(
            valid_swipe_backoff_vector(), "swipe-backoff"
        )
        self.assertTrue(ok, reason)

    def test_rejects_swipe_aggregate_mismatch(self) -> None:
        values = valid_swipe_backoff_vector()
        values[16] = values[14] + 1.0
        ok, reason = validate_feature_vector(values, "swipe-backoff")
        self.assertFalse(ok)
        self.assertIn("min/avg/max", reason)

    def test_find_pid_falls_back_to_application_identifier(self) -> None:
        class Item:
            def __init__(self, name: str, pid: int, identifier: str = "") -> None:
                self.name = name
                self.pid = pid
                self.identifier = identifier

        class Device:
            def enumerate_processes(self):
                return [Item("Yandex Keyboard", 11968)]

            def enumerate_applications(self, scope="minimal"):
                self.scope = scope
                return [Item("Yandex Keyboard", 11968, "ru.yandex.androidkeyboard")]

        self.assertEqual(_find_pid(Device(), "ru.yandex.androidkeyboard"), 11968)


if __name__ == "__main__":
    unittest.main()
