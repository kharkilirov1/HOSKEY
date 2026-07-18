import hashlib
import struct
import tempfile
import unittest
from pathlib import Path

import build_ynnlm_char_decoder as builder


ROOT = Path(__file__).resolve().parents[1]
LIVE_TFLITE = (
    ROOT
    / "build"
    / "context_emoji_exact"
    / "k13321_nnlm_tflite"
    / "ynnlm_m0_char_dec.tflite"
)


class BuildYnnlmCharDecoderTest(unittest.TestCase):
    def test_live_1225_decoder_build_is_deterministic_and_validated(self) -> None:
        if not LIVE_TFLITE.is_file():
            self.skipTest(f"live fixture is absent: {LIVE_TFLITE}")
        with tempfile.TemporaryDirectory() as temp_dir:
            output = Path(temp_dir) / "char_dec_exact.bin"
            report = builder.build_decoder_file(LIVE_TFLITE, output)
            blob = output.read_bytes()

        self.assertEqual(b"YCD1", blob[:4])
        self.assertEqual(1, struct.unpack_from("<I", blob, 4)[0])
        self.assertEqual(160, struct.unpack_from("<I", blob, 8)[0])
        self.assertEqual(len(blob), struct.unpack_from("<I", blob, 12)[0])
        self.assertEqual((38, 32, 750, 3000, 1500),
                         struct.unpack_from("<IIIII", blob, 16))
        self.assertEqual(
            "c3b8d59cf1dee140336b03cdb0d053b2ab2dfa2d126ff741923590a4a1323b34",
            report.source_sha256,
        )
        self.assertEqual(2_388_028, len(blob))
        self.assertEqual(
            "5f7d30d283b0657e1332bf52acdd7c10264706f1dcc8956585130481defea6cf",
            hashlib.sha256(blob).hexdigest(),
        )

    def test_non_tflite_input_is_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            source = Path(temp_dir) / "bad.tflite"
            output = Path(temp_dir) / "out.bin"
            source.write_bytes(b"not a tflite model")
            with self.assertRaisesRegex(ValueError, "TFL3"):
                builder.build_decoder_file(source, output)
            self.assertFalse(output.exists())


if __name__ == "__main__":
    unittest.main()
