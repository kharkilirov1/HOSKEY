import hashlib
import unittest
from pathlib import Path

import validate_emoji_assets as validator


ASSETS = Path(r"C:\Users\Kharki\Desktop\yandex_extracted\extracted_sections")


class EmojiAssetValidatorTest(unittest.TestCase):
    def test_context_wrapper_catalog_and_decoder_boundary(self) -> None:
        data = (ASSETS / "emoji_model").read_bytes()
        parsed = validator.parse_context_wrapper(data)
        self.assertEqual(10, parsed.order)
        self.assertEqual(352, parsed.catalog_offset)
        self.assertEqual(44480, parsed.tflite_offset)
        self.assertEqual(1752, len(parsed.catalog))
        self.assertEqual(1752, len(set(parsed.catalog)))
        self.assertEqual(list(range(1752)), sorted(parsed.rank_to_lex))
        self.assertEqual(list(range(1752)), sorted(parsed.lex_to_rank))
        self.assertTrue(all(parsed.lex_to_rank[parsed.rank_to_lex[index]] == index
                            for index in range(1752)))
        self.assertEqual(
            ["<unk>", "👋", "👍", "👌", "❤️", "👏", "🙏", "😘", "🤣", "🎉"],
            parsed.ranked_emoji()[:10])
        self.assertEqual(b"TFL3", data[parsed.tflite_offset + 4:
                                      parsed.tflite_offset + 8])

    def test_search_mapping_catalog_is_not_525_classes(self) -> None:
        data = (ASSETS / "search_emoji_model").read_bytes()
        parsed = validator.parse_search_mapping(data)
        self.assertEqual(256949, parsed.opaque_payload_size)
        self.assertEqual(6450, len(parsed.groups))
        self.assertEqual(44165, sum(len(group) for group in parsed.groups))
        self.assertEqual(1898, len({item for group in parsed.groups
                                   for item in group}))
        self.assertEqual(21804, len(parsed.entries))
        self.assertEqual(21804, len({key for key, _ in parsed.entries}))
        self.assertEqual(0, min(group for _, group in parsed.entries))
        self.assertEqual(6449, max(group for _, group in parsed.entries))
        self.assertEqual(["🛡️", "🫢", "🛟", "🛡️"], parsed.groups[0])
        self.assertEqual(511, len(parsed.groups[5216]))
        self.assertEqual("👗", parsed.groups[5216][0])
        self.assertEqual("🪪", parsed.groups[5216][-1])
        self.assertEqual(["➖"], parsed.groups[6449])
        self.assertEqual(
            "aa81eef43483fa310d73ee22b58442c57a363f71eecb582fa7bc6abf2922aea9",
            parsed.canonical_sha256())

    def test_search_comptrie_matches_native_oracle_prefixes(self) -> None:
        data = (ASSETS / "search_emoji_model").read_bytes()
        parsed = validator.parse_search_mapping(data)

        def without_variation(items: list[str]) -> list[str]:
            return [item.replace("\ufe0f", "") for item in items]

        native_top10 = {
            "кот": ["😻", "😹", "😽", "🐈", "😿", "😼", "🙀", "😸", "😾", "🐱"],
            "улыбка": ["😅", "☺", "🙂", "😬", "🤣", "😃", "🙃", "😊", "😁", "😼"],
            "сердце": ["🤍", "💛", "💚", "🤎", "❤", "💜", "🧡", "💔", "💖", "♥"],
            "машина": ["🚘", "🚗", "🚖", "🚕", "🚚", "🚌", "🏎", "🚒", "🚓", "🚛"],
            "огонь": ["🔥", "📛", "🚒", "🧯", "🎇", "🚨", "🎆", "🕯", "♨"],
        }
        for query, expected in native_top10.items():
            actual = parsed.search(query, max_results=10)
            self.assertEqual(expected, without_variation(actual), query)
        self.assertEqual(parsed.search("кот", 10), parsed.search("  КОТ  ", 10))
        self.assertEqual([], parsed.search(""))

    def test_truncated_assets_are_rejected(self) -> None:
        context = (ASSETS / "emoji_model").read_bytes()
        search = (ASSETS / "search_emoji_model").read_bytes()
        with self.assertRaises(ValueError):
            validator.parse_context_wrapper(context[:1000])
        with self.assertRaises(ValueError):
            validator.parse_search_mapping(search[:1000])


if __name__ == "__main__":
    unittest.main()
