
# Phase R4: N-gram LM Tail Analysis (Decoding Breakthrough)

**Date**: 2026-04-07

## Decompilation Results
We successfully used Ghidra Headless to extract the helper functions used by `WTrieNGramFreqs::DoFreq` (`FUN_00756eb0`).
The decompiled C++ reveals exactly how the LM data is packed in the 3.5MB tail!

## The Value Encoding (Switch Block)
The frequency/probability values are parsed using a `switch` statement with 4 distinct formats:

1. **Format 2 (LEB128 + ZigZag)**:
   - Reads a standard Little Endian Base 128 (`LEB128`) integer via `FUN_00609346`.
   - Decodes it as a signed integer using ZigZag decoding: `-(uVar3 & 1) ^ (uVar3 >> 1)`.
2. **Format 3 (Unsigned LEB128)**:
   - Reads `LEB128`.
   - Used directly as an unsigned integer.
3. **Format 4 (PrefixVarint + ZigZag)**:
   - Reads a custom Prefix Varint via `FUN_0060c9f3` (where the number of leading 1-bits in the first byte indicates the byte length, similar to UTF-8).
   - Decodes it using ZigZag decoding.
4. **Format 5 (Unsigned PrefixVarint)**:
   - Reads the custom Prefix Varint.
   - Used directly as an unsigned integer.

## The Record Structure
Before reading the value, the code calls a skip function (`FUN_00609a96` or `FUN_006092f6`). This function reads the first byte of a record, checks flags (`0x40`, `0x08`, `0x04`), and advances the pointer by a calculated amount (skipping child node arrays or keys) to reach the encoded value.

This completely explains why the first kilobytes of the LM are dominated by `0x02`, `0x01`, `0x00`. These are heavily quantized, ZigZag-encoded differences (deltas) of log-probabilities!

## Next Steps
We can now write a Python script that exactly replicates `FUN_00609a96` (record skipper), `FUN_00609346` (LEB128), and `FUN_0060c9f3` (PrefixVarint) to fully traverse the LM and dump the actual N-gram probabilities!
