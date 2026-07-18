#!/usr/bin/env python3
"""
build_hosdict.py — Packs assets into a HSKD container (.hosdict).

Reads from yandex_extracted + frequency data + MS models.
Output: single .hosdict container for the HOSKEY engine.
"""

import argparse
import hashlib
import json
import os
import struct
import sys
from dataclasses import dataclass

HSKD_MAGIC = 0x444B5348  # "HSKD" LE
HSKD_VERSION = 1
ALIGNMENT = 16

PROJECT_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
YANDEX_DIR = os.path.join("C:/Users/Kharki/Desktop/yandex_extracted/extracted_sections")
MS_MODELS_DIR = os.path.join("C:/Users/Kharki/Desktop/yandex_extracted/ms_models")
YNNLM_1225_MS_MODELS_DIR = os.path.join(
    "C:/Users/Kharki/Desktop/yandex_extracted/ms_models_ynnlm_1225"
)
YNNLM_1225_RAW = os.path.join(
    PROJECT_ROOT, "build/reverse/yandex1225_neural/neural_model_1225.bin"
)
TFLITE_MODELS_DIR = os.path.join("C:/Users/Kharki/clawd/yandex_extracted/yandex_extracted")
TOOLS_DIR = os.path.dirname(os.path.abspath(__file__))
DICT_OUTPUT = os.path.join(PROJECT_ROOT, "entry/src/main/resources/rawfile/dict")
PREDICTOR_BINARY = os.path.join(TOOLS_DIR, "lexicon.predictor.hpt")
WEIGHTED_WORDS = os.path.join(TOOLS_DIR, "words_with_weights.tsv")

# Files with names that encode a verified semantic variant are pinned so a
# later conversion cannot silently replace Default with an experiment.  The
# Default ranker is recovered from extracted_sections/ranker
# (SHA-256 77c74250...f6345b7f) by build_oblivious_tree_model.py, including the
# compact first tree referenced by the outer pointer vector.
PINNED_ASSET_SHA256 = {
    # Exact Default YNNLM from installed Yandex Keyboard 122.5 / dictionary
    # 67.0.  The previous 44,692,688-byte eight-model wrapper is stale and its
    # vocabulary/model indices must not be mixed with this ten-model chain.
    "neural_model_1225.bin":
        "10d8e63f951b61b6084e13374d8f8b4ab0ae030600dfe0a7a4b11f55b761e80e",
    "char_dec_exact.bin":
        "5f7d30d283b0657e1332bf52acdd7c10264706f1dcc8956585130481defea6cf",
    # Dedicated MindSpore predictor for RNNT swipe.  Do not alias this graph
    # to neural.char.dec: that key is the exact native YCD1 scoring decoder.
    "char_dec.ms":
        "108229bfbd6bd4a57c29e563a64bd9a84fb0e76e58a381f79d99bdfd5f2daeed",
    "subword_dec.ms":
        "de2e116dc897b455aad9c6c45e5fdb3402f5706d793d173dcd444186542a43fd",
    "word_dec.ms":
        "a1e714e221cf665b579298cbbd98d1fda674d877bb64087891728099c5d017f2",
    "emb_age.ms":
        "769e06fd7bdf0d4badaea4561cc52f0a3029b89fcd86627dc70ee5626317d0c6",
    "emb_app.ms":
        "3ebd8fef86f41b2a5270b9b68ba7cf73996f983bce500e8f9bab64d416d531e7",
    "emb_sex.ms":
        "5471cd5bfa9ebcc1caed3c73e8c4278ff17599eeab4d70e81417e73244539fa5",
    "word_emb.ms":
        "3215321001f8e15e5f6a5950a4fb3e2a6d116d6183d0fad7d98618f70cb044ed",
    "subword_enc_dec.ms":
        "6f32800a4fd6d63f6867d455d1792bd3ef374dbdeb8cee597d460f261a18cf2b",
    "word_enc_dec.ms":
        "355b485368a16453c98f5c7463149739280b45b07348bec634ba08e5e1db0c89",
    "enc.ms":
        "79aae185cf5afc8a73181bcb789e029060b507fe3079a7d1060f9392f8916492",
    "ranker_default_model.bin":
        "0a39e2d555bd69eb8286b12e7debef797e9612716d5f498a0dec5f02bf6d48ad",
    # Coherent Default TapModel chain extracted from the installed Yandex
    # Keyboard 122.5 main_ru asset.  The previous dict_ru_full slices used a
    # different 37/76 feature contract and must never be mixed with these
    # exact 38/78 HOBL executors.
    "tap_models_default_1225":
        "062d4ae2e38a5d9e6bb0468b7d477dea702764d60f15a16746279cdff74c972b",
    "features_tap_model_1225.json":
        "2d3458062c05950df3676186c9f0beab33a34f774322b09b3cf79896a27fd72f",
    "tap_model_ranker_1225.hobl":
        "4aeebd80a6acec0c180b13edf3e10b81763b20c8fcfe4c0491eb837b7e02e4bb",
    "tap_model_classifier_1225.hobl":
        "8daea3d952d86f7663649bd494a50f8c4c8d95992dcfe38ab682f06967156978",
    "context_emoji_wrapper_k13321.bin":
        "43d528df4c31628f1bd83216268038c0b71a1aded1261d50d7bf76804d86c255",
    "context_emoji_emb_age_k13321.ms":
        "769e06fd7bdf0d4badaea4561cc52f0a3029b89fcd86627dc70ee5626317d0c6",
    "context_emoji_emb_app_k13321.ms":
        "3ebd8fef86f41b2a5270b9b68ba7cf73996f983bce500e8f9bab64d416d531e7",
    "context_emoji_emb_sex_k13321.ms":
        "5471cd5bfa9ebcc1caed3c73e8c4278ff17599eeab4d70e81417e73244539fa5",
    "context_emoji_word_emb_k13321.ms":
        "e9a5962c89a505059076790aae8741bbe6ff2678f642debf85b9601413ed91cf",
    "context_emoji_encoder_k13321.ms":
        "79aae185cf5afc8a73181bcb789e029060b507fe3079a7d1060f9392f8916492",
    "context_emoji_decoder_k13321.ms":
        "2ac2475da37aa324df7e0eebbb91c69123c1905c0812829ddcc0bee407e1b1e2",
}


@dataclass
class AssetSpec:
    key: str
    fmt: str
    source: object
    label: str


def align(offset, alignment=ALIGNMENT):
    r = offset % alignment
    return offset + (alignment - r) if r else offset


def build_manifest():
    return json.dumps(
        {
            "manifest_version": "hosdict.manifest/v2",
            "engine_abi": "hoskey.engine/v2",
            "package_name": "ru.hoskey.default",
            "locale": "ru-RU",
        },
        ensure_ascii=False,
    ).encode("utf-8")


def find_file(*candidates):
    """Return first existing path from candidates."""
    for c in candidates:
        if os.path.exists(c):
            return c
    return None


def raw_model_format(path, fallback="raw_model_blob"):
    """Classify an extracted model blob by magic without trying to parse it."""
    try:
        with open(path, "rb") as f:
            head = f.read(16)
    except OSError:
        return fallback
    if head.startswith(b"ynnlm_v1"):
        return "ynnlm_v1_bundle"
    if len(head) >= 8 and head[4:8] == b"TFL3":
        return "tflite_model"
    if len(head) >= 8 and head[4:8] == b"MSL2":
        return "mindspore_model"
    return fallback


def validate_pinned_asset(label, data):
    expected = PINNED_ASSET_SHA256.get(label)
    if expected is None:
        return
    actual = hashlib.sha256(data).hexdigest()
    if actual != expected:
        raise ValueError(
            f"pinned asset {label} SHA-256 mismatch: "
            f"expected {expected}, got {actual}"
        )


def is_cyrillic_or_hyphen(ch):
    return ("а" <= ch <= "я") or ("А" <= ch <= "Я") or ch in ("ё", "Ё", "-")


def normalize_predictor_word(word):
    return "".join("ё" if ch == "Ё" else ch.lower() for ch in word.strip())


def is_accepted_predictor_word(word):
    if not word:
        return False
    byte_len = len(word.encode("utf-8"))
    if byte_len < 2 or byte_len > 64:
        return False
    if len(word) < 2 or len(word) > 32:
        return False
    return all(is_cyrillic_or_hyphen(ch) for ch in word)


def read_weighted_words(path):
    """Match the runtime weighted_wordlist_v1 acceptance/dedup semantics."""
    words = []
    seen = set()
    with open(path, "r", encoding="utf-8", errors="ignore") as f:
        for raw in f:
            line = raw.strip()
            if not line:
                continue
            if "\t" in line:
                word, weight_s = line.split("\t", 1)
            else:
                word, weight_s = line, "0"
            word = normalize_predictor_word(word)
            if not is_accepted_predictor_word(word):
                continue
            if word in seen:
                continue
            seen.add(word)
            weight = 0
            for ch in weight_s.strip():
                if "0" <= ch <= "9":
                    weight = weight * 10 + (ord(ch) - ord("0"))
                else:
                    break
            words.append((word, weight))
    return words


def build_predictor_trie_blob(weighted_path):
    """
    Build runtime predictor as a compact binary trie instead of shipping a text
    word list.  Format predictor_trie_v1:

      magic "HPT1", version=1, node_count, word_count, edge_count, reserved
      repeated node_count: first_edge(u32), edge_count(u32), weight(i32),
                           max_subtree_weight(i32)
      repeated edge_count: label_unicode_codepoint(u32), child_node(u32)
    """
    weighted_words = read_weighted_words(weighted_path)

    nodes = [{"children": {}, "weight": -1, "max": -1}]
    word_count = 0
    for word, weight in weighted_words:
        node = 0
        for ch in word:
            children = nodes[node]["children"]
            child = children.get(ch)
            if child is None:
                child = len(nodes)
                children[ch] = child
                nodes.append({"children": {}, "weight": -1, "max": -1})
            node = child
        if nodes[node]["weight"] < 0:
            word_count += 1
            nodes[node]["weight"] = weight
        else:
            nodes[node]["weight"] = max(nodes[node]["weight"], weight)

    # Compute max_subtree_weight iteratively to avoid Python recursion limits.
    order = [0]
    for node in order:
        order.extend(nodes[node]["children"].values())
    for node in reversed(order):
        best = nodes[node]["weight"]
        for child in nodes[node]["children"].values():
            best = max(best, nodes[child]["max"])
        nodes[node]["max"] = best

    node_records = []
    edge_records = []
    for node in nodes:
        children = sorted(node["children"].items(), key=lambda item: ord(item[0]))
        first_edge = len(edge_records)
        for ch, child in children:
            edge_records.append((ord(ch), child))
        node_records.append((first_edge, len(children), node["weight"], node["max"]))

    blob = bytearray()
    blob += struct.pack(
        "<IIIIII", 0x31545048, 1, len(nodes), word_count, len(edge_records), 0
    )  # "HPT1" LE
    for first_edge, edge_count, weight, max_weight in node_records:
        blob += struct.pack("<IIii", first_edge, edge_count, weight, max_weight)
    for label, child in edge_records:
        blob += struct.pack("<II", label, child)
    return bytes(blob)


def env_flag(name):
    return os.environ.get(name, "").strip().lower() in ("1", "true", "yes", "on")


def append_asset_if_exists(assets, key, fmt, *candidates):
    path = find_file(*candidates)
    if path:
        assets.append(AssetSpec(key, fmt, path, os.path.basename(path)))
        return True
    return False


def append_raw_model_if_exists(assets, key, name, fmt=None):
    path = find_file(os.path.join(YANDEX_DIR, name))
    if path:
        assets.append(AssetSpec(key, fmt or raw_model_format(path), path, os.path.basename(path)))
        return True
    return False


def append_tflite_model_if_exists(assets, key, name):
    path = find_file(
        os.path.join(TFLITE_MODELS_DIR, f"{name}.tflite"),
        os.path.join(TFLITE_MODELS_DIR, name),
    )
    if not path:
        return False
    with open(path, "rb") as model_file:
        head = model_file.read(8)
    if len(head) < 8 or head[4:8] != b"TFL3":
        raise ValueError(f"TFLite alias {path} has no TFL3 identifier")
    assets.append(AssetSpec(key, "tflite_model", path, os.path.basename(path)))
    return True


def append_full_model_assets(assets):
    """
    Add reverse-engineered Yandex neural/ranker/tap/autocc model assets.

    This is intentionally opt-in: the exact neural wrappers are large and the
    lean debug package must stay small and deterministic.  Use either
    --full-models or HOSKEY_PACK_FULL_MODELS=1.
    """
    # Extra feature configs used by ranker/swipe policy modules.
    extra_features = [
        ("features.ranker_swipe", "features_ranker_swipe.json"),
        ("features.ranker_swipe_backoff", "features_ranker_swipe_backoff.json"),
        ("features.swipe_blocker", "features_swipe_blocker.json"),
    ]
    for key, name in extra_features:
        append_asset_if_exists(
            assets,
            key,
            "feature_json",
            os.path.join(YANDEX_DIR, name),
            os.path.join(DICT_OUTPUT, name),
        )

    # Exact raw config-referenced Yandex wrappers/blobs.
    raw_models = [
        ("neural.word_model", "neural_model", None),
        ("neural.subword_nnlm.KEYBOARD-13321", "KEYBOARD-13321_nnlm_model", None),
        ("rnnt.model", "rnnt_model", None),
        ("ranking.catboost", "ranker", "catboost_model"),
        ("ranking.swipe", "ranker_swipe", "catboost_model"),
        ("ranking.swipe_backoff", "ranker_swipe_backoff", "catboost_model"),
        ("ranking.swipe_blocker", "swipe_blocker", "catboost_model"),
        ("tap.model_ranker.raw", "tap_model_ranker", None),
        ("tap.model_classifier.raw", "tap_model_classifier", None),
        ("tap.models_default", "tap_models_default", "tap_model_bundle"),
        ("emoji.suggest_model.raw", "emoji_model", None),
        ("emoji.suggest_model.KEYBOARD-13321", "KEYBOARD-13321_emoji_suggest", None),
        ("ranking.catboost.KEYBOARD-13321", "KEYBOARD-13321_ranker", "catboost_model"),
        ("ranking.swipe.KEYBOARD-13321", "KEYBOARD-13321_ranker_swipe", "catboost_model"),
        ("ranking.swipe_backoff.KEYBOARD-13321", "KEYBOARD-13321_ranker_swipe_backoff", "catboost_model"),
        ("rnnt.model.KEYBOARD-13321", "KEYBOARD-13321_rnnt_model", None),
        ("ranking.catboost.KEYBOARD-13421", "KEYBOARD-13421_ranker", "catboost_model"),
        ("ranking.swipe.KEYBOARD-13421", "KEYBOARD-13421_ranker_swipe", "catboost_model"),
        ("ranking.swipe_backoff.KEYBOARD-13421", "KEYBOARD-13421_ranker_swipe_backoff", "catboost_model"),
    ]
    for key, name, fmt in raw_models:
        append_raw_model_if_exists(assets, key, name, fmt)

    # Direct TFLite aliases retained only for branches whose graph role was
    # already used by the conversion/runtime work.  Emoji search/context,
    # TapModel and autocorrect aliases are deliberately excluded: their role
    # cannot be inferred from TFL3 magic alone, and the exact raw wrappers are
    # already available for continued reverse engineering.
    tflite_models = [
        ("neural.word_model.tflite", "neural_model"),
        ("neural.subword_nnlm.KEYBOARD-13321.tflite", "KEYBOARD-13321_nnlm_model"),
        ("neural.char_model.tflite", "char_model"),
        ("rnnt.model.tflite", "rnnt_model"),
        ("ranking.catboost.tflite", "ranker"),
        ("ranking.swipe.tflite", "ranker_swipe"),
        ("ranking.swipe_blocker.tflite", "swipe_blocker"),
        ("ranking.catboost.KEYBOARD-13321.tflite", "KEYBOARD-13321_ranker"),
        ("ranking.swipe.KEYBOARD-13321.tflite", "KEYBOARD-13321_ranker_swipe"),
        ("rnnt.model.KEYBOARD-13321.tflite", "KEYBOARD-13321_rnnt_model"),
        ("ranking.catboost.KEYBOARD-13421.tflite", "KEYBOARD-13421_ranker"),
        ("ranking.swipe.KEYBOARD-13421.tflite", "KEYBOARD-13421_ranker_swipe"),
    ]
    for key, name in tflite_models:
        append_tflite_model_if_exists(assets, key, name)


def build_asset_list(include_full_models=False, include_predictor_fallback=False):
    """Build list of (key, format, filepath) for all available assets."""
    assets = []

    # Core assets from yandex_extracted.
    core = [
        ("config", "feature_json", "config"),
        ("lexicon.default", "succinct_lexicon_v1", "trie"),
        (
            "lexicon.experiments.KEYBOARD-13421_v2",
            "succinct_lexicon_k13421",
            "KEYBOARD-13421_trie",
        ),
        ("rules.transfeme", "tfmtrie_v1", "rules.bin"),
        (
            "rules.transfeme_experiments.KEYBOARD-13421_v2",
            "tfmtrie_k13421",
            "KEYBOARD-13421_rules.bin",
        ),
        ("normdict.default", "normdict_v2", "normdict"),
        # Raw Yandex ynnlm_v1 bundle.  This is the original neural model
        # wrapper, not the debug predictor fallback; the engine parses its
        # embedded UTF-8 vocabulary as an exact/vocab bridge when MindSpore
        # Lite inference is unavailable locally.
        ("neural.word_model.raw", "ynnlm_v1_bundle", "neural_model"),
        # The exact Yandex 3GRM character LM is only 289 KB and is required by
        # the 44-feature MatrixNet ranker, so it belongs in the lean core pack.
        ("neural.char_model", "char_trigram_v1", "char_model"),
        # The recovered emoji search asset is a 1.08 MB FlatBuffer catalog plus
        # UTF-16BE compact trie. It now has a production C++ evaluator and is
        # part of the default package rather than the audit-only full pack.
        (
            "emoji.search_model.raw",
            "emoji_search_comptrie_v1",
            "search_emoji_model",
        ),
        ("lemma.hash", "chd_hash", "lemmer_mhash"),
        ("lemma.rules", "lemma_rules_program", "lemmer_rules"),
        ("policy.blacklist", "policy_blob", "blacklist"),
        ("policy.blacklist_suggest", "policy_blob", "blacklist_suggest"),
        ("policy.autocorrect_blocker", "policy_blob", "autocorrect_blocker"),
        ("policy.abbr", "policy_blob", "abbr"),
        ("features.autocc", "feature_json", "features_autocc.json"),
        # Exact recovered Autocc forests. Yandex 122.5 dictionary 67.0 promotes
        # the recovered K13421 forest to default; both keys intentionally carry
        # the same pinned HOBL bytes. The old raw aliases are not packaged.
        (
            "autocc.tree.default.hobl",
            "oblivious_tree_v1",
            "autocc_default.hobl",
        ),
        (
            "autocc.tree.k13321.hobl",
            "oblivious_tree_v1",
            "autocc_KEYBOARD-13321.hobl",
        ),
        (
            "autocc.tree.k13421.hobl",
            "oblivious_tree_v1",
            "autocc_KEYBOARD-13421.hobl",
        ),
        ("features.ranker", "feature_json", "features_ranker.json"),
        ("features.swipe", "feature_json", "features_ranker_swipe.json"),
        (
            "features.swipe_backoff",
            "feature_json",
            "features_ranker_swipe_backoff.json",
        ),
        ("features.swipe_blocker", "feature_json", "features_swipe_blocker.json"),
        # Atomic Default TapModel 122.5 chain. All four assets are small enough
        # for the production package and are consumed only as one validated
        # unit by TapModelRunner.
        (
            "tap.models_default",
            "tap_model_bundle",
            "tap_models_default_1225",
        ),
        (
            "features.tap_model",
            "feature_json",
            "features_tap_model_1225.json",
        ),
        (
            "tap.ranker.hobl",
            "oblivious_tree_v1",
            "tap_model_ranker_1225.hobl",
        ),
        (
            "tap.classifier.hobl",
            "oblivious_tree_v1",
            "tap_model_classifier_1225.hobl",
        ),
    ]
    for key, fmt, name in core:
        if key == "neural.word_model.raw":
            path = find_file(YNNLM_1225_RAW)
        else:
            path = find_file(
                os.path.join(YANDEX_DIR, name),
                os.path.join(DICT_OUTPUT, name),
            )
        if path:
            assets.append(AssetSpec(key, fmt, path, os.path.basename(path)))

    # Optional local/debug fallback. Disabled by default because the target
    # parity path is Yandex-like: 1nc7 states + neural/ranker models, not a
    # separate word-level predictor trie.
    if include_predictor_fallback:
        if os.path.exists(WEIGHTED_WORDS):
            assets.append(
                AssetSpec(
                    "lexicon.predictor",
                    "predictor_trie_v1",
                    build_predictor_trie_blob(WEIGHTED_WORDS),
                    os.path.basename(WEIGHTED_WORDS),
                )
            )
        else:
            append_asset_if_exists(
                assets,
                "lexicon.predictor",
                "predictor_trie_v1",
                PREDICTOR_BINARY,
            )

    # MindSpore models used by active runtime paths.
    # TapModel .ms aliases are deliberately absent: they were generated from
    # stale 37/76 wrappers and are not contract-compatible with the installed
    # 122.5 ranker/classifier. The active path is the exact HOBL chain above.
    ms_models = [
        ("autocc.tree_model", "mindspore_model", "tree_autocorrect_model.ms"),
    ]
    if include_full_models:
        # Legacy fallbacks: MatrixNet and neural.word_model.raw take priority in
        # production, so shipping these two by default only bloats the HAP.
        ms_models.extend([
            ("ranking.ranker", "mindspore_model", "ranker.ms"),
            ("ranking.neural_model", "mindspore_model", "neural_model.ms"),
        ])
    for key, fmt, name in ms_models:
        path = find_file(os.path.join(MS_MODELS_DIR, name))
        if path:
            assets.append(AssetSpec(key, fmt, path, os.path.basename(path)))

    # Coherent K13321 context-emoji chain. Never package a partial mixture:
    # the runner validates and activates these seven assets atomically.
    context_emoji_models = [
        (
            "emoji.context.wrapper.k13321",
            "ynnlm_v1_bundle",
            "context_emoji_wrapper_k13321.bin",
        ),
        (
            "emoji.context.emb_age.k13321",
            "mindspore_model",
            "context_emoji_emb_age_k13321.ms",
        ),
        (
            "emoji.context.emb_app.k13321",
            "mindspore_model",
            "context_emoji_emb_app_k13321.ms",
        ),
        (
            "emoji.context.emb_sex.k13321",
            "mindspore_model",
            "context_emoji_emb_sex_k13321.ms",
        ),
        (
            "emoji.context.word_emb.k13321",
            "mindspore_model",
            "context_emoji_word_emb_k13321.ms",
        ),
        (
            "emoji.context.encoder.k13321",
            "mindspore_model",
            "context_emoji_encoder_k13321.ms",
        ),
        (
            "emoji.context.decoder.k13321",
            "mindspore_model",
            "context_emoji_decoder_k13321.ms",
        ),
    ]
    context_paths = [
        find_file(os.path.join(MS_MODELS_DIR, name))
        for _, _, name in context_emoji_models
    ]
    if any(context_paths) and not all(context_paths):
        missing = [
            name
            for (_, _, name), path in zip(context_emoji_models, context_paths)
            if not path
        ]
        raise FileNotFoundError(
            "incomplete K13321 context emoji chain: " + ", ".join(missing)
        )
    if all(context_paths):
        for (key, fmt, name), path in zip(context_emoji_models, context_paths):
            assets.append(AssetSpec(key, fmt, path, name))

    # Neural word-suggestion chain from the same pinned 122.5 wrapper as the
    # vocabulary above. Package it atomically: a partial chain or a stale model
    # mixed with the live vocabulary silently corrupts rank indices and scores.
    ynnlm_1225_models = [
        ("neural.word.emb_age", "mindspore_model", "emb_age.ms"),
        ("neural.word.emb_app", "mindspore_model", "emb_app.ms"),
        ("neural.word.emb_sex", "mindspore_model", "emb_sex.ms"),
        ("neural.word.emb", "mindspore_model", "word_emb.ms"),
        ("neural.word.enc", "mindspore_model", "enc.ms"),
        ("neural.word.enc_dec", "mindspore_model", "word_enc_dec.ms"),
        ("neural.word.dec", "mindspore_model", "word_dec.ms"),
        ("neural.subword.enc_dec", "mindspore_model", "subword_enc_dec.ms"),
        ("neural.subword.dec", "mindspore_model", "subword_dec.ms"),
        ("neural.char.dec", "ynnlm_char_decoder_v1", "char_dec_exact.bin"),
    ]
    ynnlm_1225_paths = [
        find_file(os.path.join(YNNLM_1225_MS_MODELS_DIR, name))
        for _, _, name in ynnlm_1225_models
    ]
    if not all(ynnlm_1225_paths):
        missing = [
            name
            for (_, _, name), path in zip(ynnlm_1225_models, ynnlm_1225_paths)
            if not path
        ]
        raise FileNotFoundError(
            "incomplete Yandex 122.5 YNNLM chain: " + ", ".join(missing)
        )
    for (key, fmt, name), path in zip(ynnlm_1225_models, ynnlm_1225_paths):
        assets.append(AssetSpec(key, fmt, path, name))

    # Other learned runtime assets remain in the shared model directory.
    neural_word = [
        # Coherent Default MatrixNet ranker.  Keep K13321/K13421 under separate
        # filenames/routes; mixing the K13421 forest with the Default
        # NNLM/swipe chain is not a valid variant.
        ("ranking.matrixnet", "raw_model_blob", "ranker_default_model.bin"),
        # Same recovered 500-tree MatrixNet format for gesture ranking.  The
        # backoff catalog is the active HOSKEY path until RNNT swipe scores are
        # available; primary and blocker stay packaged for one coherent bundle.
        ("ranking.swipe.matrixnet", "raw_model_blob", "ranker_swipe_model.bin"),
        (
            "ranking.swipe_backoff.matrixnet",
            "raw_model_blob",
            "ranker_swipe_backoff_model.bin",
        ),
        (
            "ranking.swipe_blocker.matrixnet",
            "raw_model_blob",
            "swipe_blocker_model.bin",
        ),
        # Learned RNNT swipe encoder/predictor/joint components.  The original
        # positional graph is an untrained sinusoidal transform and is
        # reproduced exactly in C++ because MindSpore Lite 2.6 hangs in that
        # graph's SIN/COS path.
        ("rnnt.sampling", "mindspore_model", "rnnt_sampling.ms"),
        ("rnnt.pre0", "mindspore_model", "rnnt_pre0.ms"),
        ("rnnt.pre1", "mindspore_model", "rnnt_pre1.ms"),
        ("rnnt.pre2", "mindspore_model", "rnnt_pre2.ms"),
        ("rnnt.pre3", "mindspore_model", "rnnt_pre3.ms"),
        ("rnnt.post0", "mindspore_model", "rnnt_post0.ms"),
        ("rnnt.post1", "mindspore_model", "rnnt_post1.ms"),
        ("rnnt.post2", "mindspore_model", "rnnt_post2.ms"),
        ("rnnt.post3", "mindspore_model", "rnnt_post3.ms"),
        ("rnnt.joint", "mindspore_model", "rnnt_joint.ms"),
        ("rnnt.predictor.char_dec", "mindspore_model", "char_dec.ms"),
    ]
    for key, fmt, name in neural_word:
        path = find_file(os.path.join(MS_MODELS_DIR, name))
        if path:
            assets.append(AssetSpec(key, fmt, path, os.path.basename(path)))

    if include_full_models:
        append_asset_if_exists(
            assets,
            "neural.word.encoder",
            "raw_model_blob",
            os.path.join(MS_MODELS_DIR, "word_encoder.bin"),
        )

    if include_full_models:
        append_full_model_assets(assets)

    return assets


def build_container(output_path, include_full_models=False,
                    include_predictor_fallback=False):
    assets = build_asset_list(
        include_full_models=include_full_models,
        include_predictor_fallback=include_predictor_fallback,
    )

    # Read all asset data.
    entries = []
    for asset in assets:
        key, fmt = asset.key, asset.fmt
        if isinstance(asset.source, (bytes, bytearray)):
            data = bytes(asset.source)
        else:
            with open(asset.source, "rb") as f:
                data = f.read()
        validate_pinned_asset(asset.label, data)
        entries.append((key, fmt, data))
        size_mb = len(data) / 1024 / 1024
        label = f"{size_mb:.1f}MB" if size_mb > 1 else f"{len(data):,}B"
        print(f"  PACK {key}: {asset.label} ({label})")

    if not entries:
        print("ERROR: no assets found")
        sys.exit(1)

    manifest_bytes = build_manifest()
    asset_count = len(entries)

    # Calculate index size.
    index_size = 0
    for key, fmt, data in entries:
        index_size += 4 + len(key.encode()) + 4 + len(fmt.encode()) + 16

    header_size = 16
    data_start = align(header_size + len(manifest_bytes) + index_size)

    # Calculate offsets.
    cur = data_start
    offsets = []
    for _, _, data in entries:
        offsets.append(cur)
        cur = align(cur + len(data))

    # Build binary.
    buf = bytearray()
    buf += struct.pack("<I", HSKD_MAGIC)
    buf += struct.pack("<I", HSKD_VERSION)
    buf += struct.pack("<I", asset_count)
    buf += struct.pack("<I", len(manifest_bytes))
    buf += manifest_bytes

    for i, (key, fmt, data) in enumerate(entries):
        kb = key.encode()
        fb = fmt.encode()
        buf += struct.pack("<I", len(kb)) + kb
        buf += struct.pack("<I", len(fb)) + fb
        buf += struct.pack("<Q", offsets[i])
        buf += struct.pack("<Q", len(data))

    while len(buf) < data_start:
        buf += b"\x00"

    for i, (key, fmt, data) in enumerate(entries):
        assert len(buf) == offsets[i]
        buf += data
        padded = align(len(buf))
        while len(buf) < padded:
            buf += b"\x00"

    os.makedirs(os.path.dirname(output_path) or ".", exist_ok=True)
    with open(output_path, "wb") as f:
        f.write(buf)

    size_mb = len(buf) / 1024 / 1024
    print(f"\nContainer: {output_path}")
    print(f"  Assets: {asset_count}")
    print(f"  Size: {len(buf):,} bytes ({size_mb:.1f} MB)")


def main():
    default_output = os.path.join(DICT_OUTPUT, "ru.hoskey.hosdict")
    parser = argparse.ArgumentParser(description="Build HSKD container")
    parser.add_argument("--output", default=default_output)
    parser.add_argument(
        "--full-models",
        action="store_true",
        default=env_flag("HOSKEY_PACK_FULL_MODELS"),
        help="pack all reverse-engineered Yandex neural/ranker/tap/autocc model blobs",
    )
    parser.add_argument(
        "--predictor-fallback",
        dest="predictor_fallback",
        action="store_true",
        default=env_flag("HOSKEY_PACK_PREDICTOR_FALLBACK"),
        help="pack local binary word predictor fallback (debug only, not Yandex parity path)",
    )
    args = parser.parse_args()

    print("Building HOSKEY container v2...")
    if args.full_models:
        print("  full model pack: enabled")
    if args.predictor_fallback:
        print("  predictor fallback: enabled (debug)")
    build_container(
        args.output,
        include_full_models=args.full_models,
        include_predictor_fallback=args.predictor_fallback,
    )
    print("Done.")


if __name__ == "__main__":
    main()
