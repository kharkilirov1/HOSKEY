#!/usr/bin/env python3
"""
package_asset_gate.py — strict packaged-dictionary gate.

This catches regressions back to loose text dictionaries:
  - no words.txt / TSV / standalone predictor artifact in HAP;
  - .hosdict may contain binary lexicon.predictor;
  - .hosdict does not contain lexicon.words.
"""

from __future__ import annotations

import argparse
import hashlib
import struct
import sys
import zipfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_HAP = ROOT / "entry/build/default/outputs/default/entry-default-unsigned.hap"
DEFAULT_PREDICTOR = ROOT / "tools/lexicon.predictor.hpt"


def fail(message: str) -> None:
    print(f"PACKAGE ASSET GATE FAIL: {message}", file=sys.stderr)
    raise SystemExit(1)


def parse_hosdict(data: bytes) -> list[tuple[str, str, int, int]]:
    if len(data) < 16:
        fail(".hosdict too small")
    magic, version, count, manifest_size = struct.unpack_from("<IIII", data, 0)
    if magic != 0x444B5348:  # HSKD
        fail(".hosdict magic mismatch")
    if version != 1:
        fail(f".hosdict unsupported version: {version}")
    off = 16 + manifest_size
    assets: list[tuple[str, str, int, int]] = []
    for _ in range(count):
        if off + 4 > len(data):
            fail(".hosdict index truncated at key length")
        key_len = struct.unpack_from("<I", data, off)[0]
        off += 4
        key = data[off : off + key_len].decode("utf-8")
        off += key_len

        if off + 4 > len(data):
            fail(".hosdict index truncated at format length")
        fmt_len = struct.unpack_from("<I", data, off)[0]
        off += 4
        fmt = data[off : off + fmt_len].decode("utf-8")
        off += fmt_len

        if off + 16 > len(data):
            fail(".hosdict index truncated at asset span")
        asset_offset, size = struct.unpack_from("<QQ", data, off)
        off += 16
        if asset_offset + size > len(data):
            fail(f"asset span outside .hosdict: {key}")
        assets.append((key, fmt, size, asset_offset))
    return assets


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--hap", type=Path, default=DEFAULT_HAP)
    parser.add_argument(
        "--hosdict",
        type=Path,
        help="check a raw .hosdict directly instead of reading it from a HAP",
    )
    args = parser.parse_args()

    source_label = ""
    if args.hosdict:
        if not args.hosdict.exists():
            fail(f".hosdict not found: {args.hosdict}")
        source_label = f"hosdict={args.hosdict}"
        hosdict_data = args.hosdict.read_bytes()
    else:
        if not args.hap.exists():
            fail(f"HAP not found: {args.hap}")

        forbidden_suffixes = (
            "words.txt",
            "words_with_weights.tsv",
            "lexicon.predictor.hpt",
        )
        with zipfile.ZipFile(args.hap) as zf:
            names = zf.namelist()
            forbidden = [
                name
                for name in names
                if any(name.endswith(suffix) for suffix in forbidden_suffixes)
            ]
            if forbidden:
                fail("forbidden loose dictionary assets in HAP: " + ", ".join(forbidden))

            hosdict_entries = [name for name in names if name.endswith(".hosdict")]
            if len(hosdict_entries) != 1:
                fail(f"expected exactly one .hosdict in HAP, got {hosdict_entries}")
            hosdict_data = zf.read(hosdict_entries[0])
        source_label = f"hap={args.hap}"

    assets = parse_hosdict(hosdict_data)
    by_key = {key: (fmt, size, offset) for key, fmt, size, offset in assets}
    if len(by_key) != len(assets):
        fail("duplicate asset key in .hosdict index")
    if "lexicon.words" in by_key:
        fail(".hosdict still contains lexicon.words")
    if "lexicon.default" not in by_key:
        fail(".hosdict missing lexicon.default")
    required_matrixnet = {
        "ranking.matrixnet",
        "ranking.swipe.matrixnet",
        "ranking.swipe_backoff.matrixnet",
        "ranking.swipe_blocker.matrixnet",
    }
    missing_matrixnet = sorted(required_matrixnet - by_key.keys())
    if missing_matrixnet:
        fail(".hosdict missing MatrixNet assets: " + ", ".join(missing_matrixnet))
    undersized_matrixnet = sorted(
        key for key in required_matrixnet if by_key[key][1] < 100_000
    )
    if undersized_matrixnet:
        fail("unexpectedly small MatrixNet assets: " + ", ".join(undersized_matrixnet))

    required_rnnt = {
        "rnnt.sampling",
        "rnnt.pre0", "rnnt.pre1", "rnnt.pre2", "rnnt.pre3",
        "rnnt.post0", "rnnt.post1", "rnnt.post2", "rnnt.post3",
        "rnnt.joint", "rnnt.predictor.char_dec",
    }
    missing_rnnt = sorted(required_rnnt - by_key.keys())
    if missing_rnnt:
        fail(".hosdict missing RNNT components: " + ", ".join(missing_rnnt))
    undersized_rnnt = sorted(
        key for key in required_rnnt if by_key[key][1] < 10_000
    )
    if undersized_rnnt:
        fail("unexpectedly small RNNT components: " + ", ".join(undersized_rnnt))
    rnnt_bytes = sum(by_key[key][1] for key in required_rnnt)
    if rnnt_bytes < 3_700_000:
        fail(f"unexpectedly small RNNT component set: {rnnt_bytes} bytes")
    predictor_fmt, predictor_size, predictor_offset = (
        by_key["rnnt.predictor.char_dec"]
    )
    if predictor_fmt != "mindspore_model" or predictor_size != 2_392_936:
        fail(
            "RNNT predictor contract mismatch: "
            f"{predictor_fmt}/{predictor_size}, expected "
            "mindspore_model/2392936"
        )
    predictor_sha256 = hashlib.sha256(
        hosdict_data[predictor_offset : predictor_offset + predictor_size]
    ).hexdigest()
    if predictor_sha256 != (
            "108229bfbd6bd4a57c29e563a64bd9a84fb0e76e58a381f79d99bdfd5f2daeed"):
        fail(f"RNNT predictor SHA-256 mismatch: {predictor_sha256}")

    expected_ynnlm_1225 = {
        "neural.word_model.raw": (
            "ynnlm_v1_bundle", 44_770_560,
            "10d8e63f951b61b6084e13374d8f8b4ab0ae030600dfe0a7a4b11f55b761e80e",
        ),
        "neural.word.emb_age": (
            "mindspore_model", 39_272,
            "769e06fd7bdf0d4badaea4561cc52f0a3029b89fcd86627dc70ee5626317d0c6",
        ),
        "neural.word.emb_app": (
            "mindspore_model", 101_800,
            "3ebd8fef86f41b2a5270b9b68ba7cf73996f983bce500e8f9bab64d416d531e7",
        ),
        "neural.word.emb_sex": (
            "mindspore_model", 38_248,
            "5471cd5bfa9ebcc1caed3c73e8c4278ff17599eeab4d70e81417e73244539fa5",
        ),
        "neural.word.emb": (
            "mindspore_model", 1_176_528,
            "3215321001f8e15e5f6a5950a4fb3e2a6d116d6183d0fad7d98618f70cb044ed",
        ),
        "neural.word.enc": (
            "mindspore_model", 6_862_120,
            "79aae185cf5afc8a73181bcb789e029060b507fe3079a7d1060f9392f8916492",
        ),
        "neural.word.enc_dec": (
            "mindspore_model", 382_208,
            "355b485368a16453c98f5c7463149739280b45b07348bec634ba08e5e1db0c89",
        ),
        "neural.word.dec": (
            "mindspore_model", 20_543_440,
            "a1e714e221cf665b579298cbbd98d1fda674d877bb64087891728099c5d017f2",
        ),
        "neural.subword.enc_dec": (
            "mindspore_model", 261_264,
            "6f32800a4fd6d63f6867d455d1792bd3ef374dbdeb8cee597d460f261a18cf2b",
        ),
        "neural.subword.dec": (
            "mindspore_model", 9_000_192,
            "de2e116dc897b455aad9c6c45e5fdb3402f5706d793d173dcd444186542a43fd",
        ),
        "neural.char.dec": (
            "ynnlm_char_decoder_v1", 2_388_028,
            "5f7d30d283b0657e1332bf52acdd7c10264706f1dcc8956585130481defea6cf",
        ),
    }
    missing_ynnlm = sorted(expected_ynnlm_1225.keys() - by_key.keys())
    if missing_ynnlm:
        fail(".hosdict missing YNNLM 122.5 chain: " + ", ".join(missing_ynnlm))
    for key, (expected_fmt, expected_size, expected_sha256) in (
        expected_ynnlm_1225.items()
    ):
        fmt, size, offset = by_key[key]
        if fmt != expected_fmt or size != expected_size:
            fail(
                f"YNNLM 122.5 contract mismatch for {key}: "
                f"{fmt}/{size}, expected {expected_fmt}/{expected_size}"
            )
        actual_sha256 = hashlib.sha256(
            hosdict_data[offset : offset + size]
        ).hexdigest()
        if actual_sha256 != expected_sha256:
            fail(f"YNNLM 122.5 SHA-256 mismatch for {key}: {actual_sha256}")

    expected_tap_model = {
        "tap.models_default": (
            "tap_model_bundle", 135_318,
            "062d4ae2e38a5d9e6bb0468b7d477dea702764d60f15a16746279cdff74c972b",
        ),
        "features.tap_model": (
            "feature_json", 1_325,
            "2d3458062c05950df3676186c9f0beab33a34f774322b09b3cf79896a27fd72f",
        ),
        "tap.ranker.hobl": (
            "oblivious_tree_v1", 2_100_888,
            "4aeebd80a6acec0c180b13edf3e10b81763b20c8fcfe4c0491eb837b7e02e4bb",
        ),
        "tap.classifier.hobl": (
            "oblivious_tree_v1", 1_107_128,
            "8daea3d952d86f7663649bd494a50f8c4c8d95992dcfe38ab682f06967156978",
        ),
    }
    missing_tap = sorted(expected_tap_model.keys() - by_key.keys())
    if missing_tap:
        fail(".hosdict missing TapModel 122.5 chain: " + ", ".join(missing_tap))
    stale_tap_aliases = {
        "ranking.tap_model_ranker",
        "ranking.tap_model_classifier",
        "tap.model_ranker.raw",
        "tap.model_classifier.raw",
    }
    present_stale_tap = sorted(stale_tap_aliases & by_key.keys())
    if present_stale_tap:
        fail(".hosdict contains stale TapModel aliases: " +
             ", ".join(present_stale_tap))
    for key, (expected_fmt, expected_size, expected_sha256) in (
        expected_tap_model.items()
    ):
        fmt, size, offset = by_key[key]
        if fmt != expected_fmt or size != expected_size:
            fail(
                f"TapModel 122.5 contract mismatch for {key}: "
                f"{fmt}/{size}, expected {expected_fmt}/{expected_size}"
            )
        actual_sha256 = hashlib.sha256(
            hosdict_data[offset : offset + size]
        ).hexdigest()
        if actual_sha256 != expected_sha256:
            fail(f"TapModel 122.5 SHA-256 mismatch for {key}: {actual_sha256}")

    expected_autocc = {
        "features.autocc": (
            "feature_json", 4_718,
            "d30141557cb12252718860c6e635033d4ff3233e535e37c38e65a4cfe12be832",
        ),
        "autocc.tree.default.hobl": (
            "oblivious_tree_v1", 1_109_230,
            "e8cabf55aa6871616355e774f79d34ea301dc0c49f1939711114cf1d795e48de",
        ),
        "autocc.tree.k13321.hobl": (
            "oblivious_tree_v1", 1_109_046,
            "25cb613816fe209e054d234b39ac72a2e294d693928959c6172f09eded7794e9",
        ),
        "autocc.tree.k13421.hobl": (
            "oblivious_tree_v1", 1_109_230,
            "e8cabf55aa6871616355e774f79d34ea301dc0c49f1939711114cf1d795e48de",
        ),
    }
    missing_autocc = sorted(expected_autocc.keys() - by_key.keys())
    if missing_autocc:
        fail(".hosdict missing exact Autocc chain: " + ", ".join(missing_autocc))
    stale_autocc_aliases = {
        "autocc.tree_model",
        "autocc.tree_model.raw",
        "autocc.tree_model.KEYBOARD-13321",
        "autocc.tree_model.KEYBOARD-13421",
    }
    present_stale_autocc = sorted(stale_autocc_aliases & by_key.keys())
    if present_stale_autocc:
        fail(".hosdict contains stale raw Autocc aliases: " +
             ", ".join(present_stale_autocc))
    for key, (expected_fmt, expected_size, expected_sha256) in (
        expected_autocc.items()
    ):
        fmt, size, offset = by_key[key]
        if fmt != expected_fmt or size != expected_size:
            fail(
                f"Autocc contract mismatch for {key}: "
                f"{fmt}/{size}, expected {expected_fmt}/{expected_size}"
            )
        actual_sha256 = hashlib.sha256(
            hosdict_data[offset : offset + size]
        ).hexdigest()
        if actual_sha256 != expected_sha256:
            fail(f"Autocc SHA-256 mismatch for {key}: {actual_sha256}")

    expected_context_emoji = {
        "emoji.context.wrapper.k13321": (
            "ynnlm_v1_bundle", 1_330_115,
            "43d528df4c31628f1bd83216268038c0b71a1aded1261d50d7bf76804d86c255",
        ),
        "emoji.context.emb_age.k13321": (
            "mindspore_model", 39_272,
            "769e06fd7bdf0d4badaea4561cc52f0a3029b89fcd86627dc70ee5626317d0c6",
        ),
        "emoji.context.emb_app.k13321": (
            "mindspore_model", 101_800,
            "3ebd8fef86f41b2a5270b9b68ba7cf73996f983bce500e8f9bab64d416d531e7",
        ),
        "emoji.context.emb_sex.k13321": (
            "mindspore_model", 38_248,
            "5471cd5bfa9ebcc1caed3c73e8c4278ff17599eeab4d70e81417e73244539fa5",
        ),
        "emoji.context.word_emb.k13321": (
            "mindspore_model", 1_178_208,
            "e9a5962c89a505059076790aae8741bbe6ff2678f642debf85b9601413ed91cf",
        ),
        "emoji.context.encoder.k13321": (
            "mindspore_model", 6_862_120,
            "79aae185cf5afc8a73181bcb789e029060b507fe3079a7d1060f9392f8916492",
        ),
        "emoji.context.decoder.k13321": (
            "mindspore_model", 1_285_352,
            "2ac2475da37aa324df7e0eebbb91c69123c1905c0812829ddcc0bee407e1b1e2",
        ),
    }
    missing_context = sorted(expected_context_emoji.keys() - by_key.keys())
    if missing_context:
        fail(".hosdict missing context emoji chain: " + ", ".join(missing_context))
    for key, (expected_fmt, expected_size, expected_sha256) in (
        expected_context_emoji.items()
    ):
        fmt, size, offset = by_key[key]
        if fmt != expected_fmt or size != expected_size:
            fail(
                f"context emoji contract mismatch for {key}: "
                f"{fmt}/{size}, expected {expected_fmt}/{expected_size}"
            )
        actual_sha256 = hashlib.sha256(
            hosdict_data[offset : offset + size]
        ).hexdigest()
        if actual_sha256 != expected_sha256:
            fail(
                f"context emoji SHA-256 mismatch for {key}: {actual_sha256}"
            )

    lex_default = by_key["lexicon.default"]
    print("PACKAGE ASSET GATE OK")
    print(f"  {source_label}")
    print(f"  assets={len(assets)}")
    print(f"  lexicon.default=succinct_lexicon_v1, {lex_default[1]} bytes")

    # lexicon.predictor is optional now (1nc7 is primary).
    if "lexicon.predictor" in by_key:
        predictor = by_key["lexicon.predictor"]
        print(f"  lexicon.predictor={predictor[0]}, {predictor[1]} bytes (fallback)")
    else:
        print("  lexicon.predictor: not present (1nc7 primary)")
    for key in sorted(required_matrixnet):
        print(f"  {key}={by_key[key][0]}, {by_key[key][1]} bytes")
    print(
        "  ynnlm.default_1225="
        f"{sum(by_key[key][1] for key in expected_ynnlm_1225)} bytes, "
        "9 MindIR models + 1 exact native decoder, 11 pinned package assets"
    )
    print(
        "  tap_model.default_1225="
        f"{sum(by_key[key][1] for key in expected_tap_model)} bytes, "
        "4 pinned assets, features=38/78"
    )
    print(
        "  autocc.default_k13321_k13421="
        f"{sum(by_key[key][1] for key in expected_autocc)} bytes, "
        "4 pinned assets, features=79, trees=2000x3"
    )
    print(
        "  context_emoji.k13321="
        f"{sum(by_key[key][1] for key in expected_context_emoji)} bytes, "
        "7 pinned assets"
    )


if __name__ == "__main__":
    main()
