import math
import struct
import unittest

import build_oblivious_tree_model as converter


def _tree(depth: int, conditions: list[int], leaf_bias: float) -> bytes:
    leaf_count = 1 << depth
    header_end = 16 + leaf_count * 8
    out = bytearray(struct.pack("<IIII", 0xABCD, header_end, 4, leaf_count))
    out.extend(struct.pack("<" + "d" * leaf_count,
                           *(leaf_bias + i * 0.5 for i in range(leaf_count))))
    out.extend(struct.pack("<II", 0, depth))
    out.extend(struct.pack("<" + "I" * depth, *conditions))
    return bytes(out)


def _fixture(depth: int = 8) -> bytes:
    # Flat border order is feature 0, feature 1, feature 2.
    conditions = [0, 1, 2] + [0] * (depth - 3)
    out = bytearray(b"PREFIX00")
    out.extend(_tree(depth, conditions, 0.0))
    out.extend(_tree(depth, conditions, 1000.0))

    # Regular feature >=2 header.
    out.extend(struct.pack("<IIIIf", 0xFFFF0000, 2, 4, 1, 0.25))
    # Feature 1 and feature 0 have recovered irregular headers.
    out.extend(struct.pack("<IIIIf", 8, 1, 4, 1, -1.0))
    out.extend(struct.pack("<IIIIf", 0x12345678, 8, 4, 1, 0.5))
    return bytes(out)


def _pointer_fixture(depth: int = 8) -> bytes:
    leaf_count = 1 << depth
    header_end = 16 + leaf_count * 8
    conditions = [0, 1, 2] + [0] * (depth - 3)

    def object_bytes(compact_footer: bool, bias: float) -> bytes:
        pointer = header_end - 4 if compact_footer else header_end
        out = bytearray(struct.pack("<IIII", 0xCAFE, pointer, 4,
                                    leaf_count))
        out.extend(struct.pack("<" + "d" * leaf_count,
                               *(bias + i for i in range(leaf_count))))
        if not compact_footer:
            out.extend(struct.pack("<I", 0))
        out.extend(struct.pack("<I", depth))
        out.extend(struct.pack("<" + "I" * depth, *conditions))
        return bytes(out)

    normal = object_bytes(False, 0.0)
    compact = object_bytes(True, 1000.0)
    normal_offset = 32
    compact_offset = normal_offset + len(normal)
    out = bytearray(struct.pack("<IIIIII", 8, 0, 0xDEAD, 0, 4, 2))
    # Preserve logical order compact -> normal, as in the recovered pointer
    # vector where physical objects are stored in reverse order.
    out.extend(struct.pack("<I", compact_offset - 24))
    out.extend(struct.pack("<I", normal_offset - 28))
    out.extend(normal)
    out.extend(compact)
    out.extend(struct.pack("<IIIIf", 0xFFFF0000, 2, 4, 1, 0.25))
    out.extend(struct.pack("<IIIIf", 8, 1, 4, 1, -1.0))
    out.extend(struct.pack("<IIIIf", 0x12345678, 8, 4, 1, 0.5))
    return bytes(out)


def _read_hobl_header(data: bytes) -> tuple[int, int, int, int]:
    magic, version, depth, reserved, trees, features = struct.unpack_from(
        "<4sHBBII", data, 0)
    if magic != b"HOBL" or reserved != 0:
        raise AssertionError("bad HOBL header")
    return version, depth, trees, features


class ConverterTest(unittest.TestCase):
    def test_converts_depth8_and_preserves_contract(self) -> None:
        output, facts = converter.convert_model(_fixture(), feature_count=4,
                                                expected_depth=8)
        self.assertEqual((2, 8, 2, 4), _read_hobl_header(output))
        self.assertEqual(2, facts.tree_count)
        self.assertEqual(3, facts.flat_border_count)
        self.assertEqual(3, facts.condition_max_plus_one)
        self.assertEqual([3], facts.zero_border_features)
        self.assertEqual("gt_proven", facts.comparator_assumption)
        self.assertEqual("lsb_proven", facts.leaf_order_assumption)

    def test_rejects_expected_depth_mismatch(self) -> None:
        with self.assertRaisesRegex(ValueError, "depth"):
            converter.convert_model(_fixture(8), feature_count=4,
                                    expected_depth=6)

    def test_pointer_vector_includes_compact_first_tree(self) -> None:
        output, facts = converter.convert_model(
            _pointer_fixture(), feature_count=4, expected_depth=8,
            expected_tree_count=2)
        self.assertEqual((2, 8, 2, 4), _read_hobl_header(output))
        self.assertEqual(2, facts.tree_count)
        # The compact object is first in logical vector order.  Its leaf 0 is
        # 1000, so this distinguishes pointer order from physical order.
        border_bytes = 2 * 4 + 3 * 4
        tree_payload = 16 + border_bytes
        first_leaf = tree_payload + 8 * 5
        self.assertEqual(1000.0, struct.unpack_from("<d", output,
                                                   first_leaf)[0])

    def test_hobl_preserves_float64_leaves(self) -> None:
        output, _ = converter.convert_model(
            _pointer_fixture(), feature_count=4, expected_depth=8,
            expected_tree_count=2)
        border_bytes = 2 * 4 + 3 * 4
        first_leaf = 16 + border_bytes + 8 * 5
        second_leaf = first_leaf + 8
        self.assertEqual(1001.0, struct.unpack_from("<d", output,
                                                   second_leaf)[0])

    def test_depth6_can_emit_existing_hrnk_runtime_format(self) -> None:
        output, facts = converter.convert_model(
            _pointer_fixture(6), feature_count=4, expected_depth=6,
            expected_tree_count=2, output_format="hrnk")
        self.assertEqual(b"HRNK", output[:4])
        self.assertEqual((2, 4), struct.unpack_from("<II", output, 4))
        self.assertEqual(6, facts.depth)
        border_bytes = 2 * 4 + 3 * 4
        first_leaf = 12 + border_bytes + 6 * 5
        self.assertEqual(1000.0, struct.unpack_from("<f", output,
                                                   first_leaf)[0])

    def test_rejects_non_contiguous_tree_run(self) -> None:
        broken = bytearray(_fixture())
        second_tree = 8 + len(_tree(8, [0] * 8, 0.0))
        broken[second_tree + 4:second_tree + 8] = struct.pack("<I", 123)
        with self.assertRaisesRegex(ValueError, "tree run"):
            converter.convert_model(bytes(broken), feature_count=4,
                                    expected_depth=8, expected_tree_count=2)

    def test_rejects_condition_outside_flat_catalog(self) -> None:
        bad = bytearray(_fixture())
        tree_start = 8
        conditions_start = tree_start + 16 + (1 << 8) * 8 + 8
        bad[conditions_start:conditions_start + 4] = struct.pack("<I", 9)
        with self.assertRaisesRegex(ValueError, "condition"):
            converter.convert_model(bytes(bad), feature_count=4,
                                    expected_depth=8)

    def test_feature_count_reads_autocorrect_schema(self) -> None:
        payload = {
            "Features": [{}, {}],
            "Stats": [{}],
            "Combos": [{}, {}, {}],
        }
        self.assertEqual(6, converter.feature_count_from_payload(payload))


if __name__ == "__main__":
    unittest.main()
