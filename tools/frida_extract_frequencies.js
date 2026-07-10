/**
 * frida_extract_frequencies.js
 *
 * Frida JS agent for extracting word frequency/weight scores from Yandex Keyboard.
 *
 * USAGE:
 *   Injected by frida_extract_frequencies.py orchestrator.
 *   Can also be used standalone:
 *     frida -U -n ru.yandex.androidkeyboard -l frida_extract_frequencies.js
 *
 * APPROACH:
 *   Strategy 1 (primary): Hook Native$DictionaryFacilitator.getSuggestions at Java level.
 *                          Send a word as input, parse protobuf response for score fields.
 *   Strategy 2 (native):  Hook the internal weight lookup function in libjni_ykeyboard3.so
 *                          at offset 0x00660dca (FUN_00660dca) which returns trie node weight.
 *   Strategy 3 (fallback): Hook getSuggestions native export in libjni_ykeyboard3.so and
 *                          parse the raw protobuf bytes for varint score fields.
 *
 * PROTOCOL (communication with Python orchestrator via send/recv):
 *   Python -> JS:  { type: "lookup", word: "слово" }
 *   JS -> Python:  { type: "result", word: "слово", weight: 12345, source: "java"|"native" }
 *   JS -> Python:  { type: "ready" }
 *   JS -> Python:  { type: "error", message: "..." }
 *   JS -> Python:  { type: "log", message: "..." }
 */

'use strict';

// ============================================================
// Configuration
// ============================================================
var NATIVE_LIB = "libjni_ykeyboard3.so";
var WEIGHT_FN_OFFSET = 0x00660dca;  // FUN_00660dca - trie weight lookup
var BATCH_MODE = false;  // set to true for passive capture mode

// State
var pendingWord = null;
var dictFacilitatorInstance = null;
var nativeWeightFn = null;
var getSuggestionsNative = null;
var hookMode = "none";  // "java", "native_weight", "native_suggest"

// ============================================================
// Utility: parse protobuf varints and extract score fields
// ============================================================
function parseVarint(buf, offset) {
    var result = 0;
    var shift = 0;
    var pos = offset;
    while (pos < buf.length) {
        var b = buf[pos] & 0xFF;
        result |= (b & 0x7F) << shift;
        pos++;
        if ((b & 0x80) === 0) break;
        shift += 7;
        if (shift > 35) break;  // safety
    }
    return { value: result, nextPos: pos };
}

/**
 * Extract scores from protobuf response bytes.
 * Yandex suggestion protobuf has Suggestion messages with score/weight fields.
 * Scores are typically varint fields with field numbers in range 3-8.
 * We look for patterns: field_tag followed by varint value.
 */
function extractScoresFromProtobuf(data) {
    var scores = [];
    var i = 0;
    while (i < data.length - 1) {
        var tag = data[i] & 0xFF;
        var fieldNum = tag >>> 3;
        var wireType = tag & 0x07;

        // Varint fields (wire type 0) with field numbers commonly used for scores
        if (wireType === 0 && fieldNum >= 1 && fieldNum <= 15) {
            var parsed = parseVarint(data, i + 1);
            if (parsed.value > 0 && parsed.value < 0x7FFFFFFF) {
                scores.push({ field: fieldNum, value: parsed.value, offset: i });
            }
            i = parsed.nextPos;
        } else if (wireType === 2) {
            // Length-delimited (string/bytes/embedded message)
            if (i + 1 < data.length) {
                var lenParsed = parseVarint(data, i + 1);
                i = lenParsed.nextPos + lenParsed.value;
            } else {
                i++;
            }
        } else if (wireType === 5) {
            i += 5;  // 32-bit
        } else if (wireType === 1) {
            i += 9;  // 64-bit
        } else {
            i++;
        }
    }
    return scores;
}

/**
 * Extract Russian words from protobuf bytes.
 */
function extractWordsFromBytes(data) {
    var words = [];
    var i = 0;
    while (i < data.length - 1) {
        if (data[i] === 0xD0 || data[i] === 0xD1) {
            var wordBytes = [];
            var j = i;
            while (j < data.length - 1) {
                if (data[j] === 0xD0 && data[j + 1] >= 0x80 && data[j + 1] <= 0xBF) {
                    wordBytes.push(data[j], data[j + 1]);
                    j += 2;
                } else if (data[j] === 0xD1 && data[j + 1] >= 0x80 && data[j + 1] <= 0x8F) {
                    wordBytes.push(data[j], data[j + 1]);
                    j += 2;
                } else {
                    break;
                }
            }
            if (wordBytes.length >= 4) {
                // Convert bytes to string
                var arr = new Uint8Array(wordBytes);
                var str = "";
                for (var k = 0; k < arr.length; k += 2) {
                    var code;
                    if (arr[k] === 0xD0) {
                        code = 0x400 + (arr[k + 1] - 0x80);
                    } else {
                        code = 0x440 + (arr[k + 1] - 0x80);
                    }
                    str += String.fromCharCode(code);
                }
                if (str.length >= 2) {
                    words.push(str);
                }
            }
            i = j > i ? j : i + 1;
        } else {
            i++;
        }
    }
    return words;
}

// ============================================================
// Strategy 1: Java-level hook on DictionaryFacilitator
// ============================================================
function setupJavaHook() {
    Java.perform(function () {
        try {
            var DictFacilitator = Java.use(
                "ru.yandex.androidkeyboard.nativecode.Native$DictionaryFacilitator"
            );
            send({ type: "log", message: "[+] Found Native$DictionaryFacilitator" });

            // Hook getSuggestions to capture response with scores
            DictFacilitator.getSuggestions.implementation = function (inputBytes) {
                var result = this.getSuggestions(inputBytes);

                if (result !== null && result.length > 0) {
                    // Convert Java byte[] to JS array
                    var jsBytes = new Uint8Array(result.length);
                    for (var i = 0; i < result.length; i++) {
                        jsBytes[i] = result[i] & 0xFF;
                    }

                    // Extract words and scores
                    var words = extractWordsFromBytes(jsBytes);
                    var scores = extractScoresFromProtobuf(jsBytes);

                    // Build hex for detailed Python-side parsing
                    var hexStr = "";
                    for (var i = 0; i < Math.min(jsBytes.length, 4096); i++) {
                        var b = jsBytes[i];
                        hexStr += ("0" + b.toString(16)).slice(-2);
                    }

                    // If we have a pending word lookup, pair word with best score
                    if (pendingWord !== null) {
                        var bestScore = 0;
                        for (var s = 0; s < scores.length; s++) {
                            if (scores[s].value > bestScore) {
                                bestScore = scores[s].value;
                            }
                        }
                        send({
                            type: "result",
                            word: pendingWord,
                            weight: bestScore,
                            suggestions: words,
                            scores: scores.map(function(s) { return s.value; }),
                            hex: hexStr,
                            len: jsBytes.length,
                            source: "java"
                        });
                        pendingWord = null;
                    } else {
                        // Passive mode - report all suggestions with scores
                        send({
                            type: "passive_result",
                            words: words,
                            scores: scores.map(function(s) { return { f: s.field, v: s.value }; }),
                            hex: hexStr,
                            len: jsBytes.length
                        });
                    }
                }

                return result;
            };

            hookMode = "java";
            send({ type: "log", message: "[+] Java getSuggestions hooked" });

            // Try to get the facilitator instance for active queries
            try {
                Java.choose(
                    "ru.yandex.androidkeyboard.nativecode.Native$DictionaryFacilitator",
                    {
                        onMatch: function (instance) {
                            dictFacilitatorInstance = instance;
                            send({ type: "log", message: "[+] Got DictionaryFacilitator instance" });
                        },
                        onComplete: function () {}
                    }
                );
            } catch (e) {
                send({ type: "log", message: "[-] Could not enumerate instances: " + e });
            }

        } catch (e) {
            send({ type: "log", message: "[-] Java hook failed: " + e });
        }
    });
}

// ============================================================
// Strategy 2: Native weight lookup function hook
// ============================================================
function setupNativeWeightHook() {
    var mod = Process.findModuleByName(NATIVE_LIB);
    if (!mod) {
        send({ type: "log", message: "[-] " + NATIVE_LIB + " not loaded" });
        return false;
    }
    send({ type: "log", message: "[+] " + NATIVE_LIB + " at " + mod.base });

    // Hook the weight/frequency lookup function
    try {
        var weightFnAddr = mod.base.add(WEIGHT_FN_OFFSET);
        send({ type: "log", message: "[+] Weight function at " + weightFnAddr });

        Interceptor.attach(weightFnAddr, {
            onEnter: function (args) {
                // args[0] = this/context pointer
                // args[1] = node pointer or key
                this.ctx = args[0];
                this.node = args[1];
            },
            onLeave: function (retval) {
                var weight = retval.toInt32();
                if (weight !== 0) {
                    send({
                        type: "native_weight",
                        weight: weight,
                        node: this.node.toString()
                    });
                }
            }
        });

        nativeWeightFn = weightFnAddr;
        send({ type: "log", message: "[+] Native weight function hooked at offset 0x" + WEIGHT_FN_OFFSET.toString(16) });
        return true;
    } catch (e) {
        send({ type: "log", message: "[-] Native weight hook failed: " + e });
        return false;
    }
}

// ============================================================
// Strategy 3: Native getSuggestions export hook
// ============================================================
function setupNativeSuggestHook() {
    var mod = Process.findModuleByName(NATIVE_LIB);
    if (!mod) return false;

    var exports = mod.enumerateExports();
    var suggestExport = null;

    // Find getSuggestions or similar export
    for (var i = 0; i < exports.length; i++) {
        var name = exports[i].name;
        if (name.indexOf("getSuggestions") !== -1 ||
            name.indexOf("suggest") !== -1 ||
            name.indexOf("Suggest") !== -1) {
            suggestExport = exports[i];
            send({ type: "log", message: "[+] Found native export: " + name + " at " + exports[i].address });
            break;
        }
    }

    // List all exports for debugging
    send({ type: "log", message: "[*] Total exports: " + exports.length });
    var jniExports = exports.filter(function(e) {
        return e.name.indexOf("Java_") === 0;
    });
    for (var j = 0; j < jniExports.length; j++) {
        send({ type: "log", message: "  JNI: " + jniExports[j].name });
    }

    if (suggestExport) {
        try {
            Interceptor.attach(suggestExport.address, {
                onEnter: function (args) {
                    this.callArgs = [];
                    for (var a = 0; a < 6; a++) {
                        this.callArgs.push(args[a]);
                    }
                },
                onLeave: function (retval) {
                    if (!retval.isNull()) {
                        send({
                            type: "native_suggest_call",
                            retval: retval.toString()
                        });
                    }
                }
            });
            getSuggestionsNative = suggestExport.address;
            send({ type: "log", message: "[+] Native suggest export hooked" });
            return true;
        } catch (e) {
            send({ type: "log", message: "[-] Native suggest hook failed: " + e });
        }
    }

    return false;
}

// ============================================================
// Active word lookup: simulate input via InputConnection
// ============================================================
function simulateWordInput(word) {
    Java.perform(function () {
        try {
            // Find the active InputMethodService
            Java.choose("android.inputmethodservice.InputMethodService", {
                onMatch: function (ims) {
                    var ic = ims.getCurrentInputConnection();
                    if (ic !== null) {
                        // Clear current input
                        ic.deleteSurroundingText(100, 100);
                        // Commit the word to trigger suggestions
                        ic.commitText(Java.use("java.lang.String").$new(word), 1);
                        send({ type: "log", message: "[*] Simulated input: " + word });
                    } else {
                        send({ type: "log", message: "[-] No active InputConnection" });
                    }
                },
                onComplete: function () {}
            });
        } catch (e) {
            send({ type: "log", message: "[-] simulateWordInput error: " + e });
        }
    });
}

/**
 * Alternative: use setComposingText to trigger suggestions without committing.
 * This is more likely to trigger the suggestion pipeline.
 */
function setComposingWord(word) {
    Java.perform(function () {
        try {
            Java.choose("android.inputmethodservice.InputMethodService", {
                onMatch: function (ims) {
                    var ic = ims.getCurrentInputConnection();
                    if (ic !== null) {
                        ic.beginBatchEdit();
                        ic.deleteSurroundingText(100, 100);
                        ic.setComposingText(
                            Java.use("java.lang.String").$new(word), 1
                        );
                        ic.endBatchEdit();
                    }
                },
                onComplete: function () {}
            });
        } catch (e) {
            send({ type: "log", message: "[-] setComposingWord error: " + e });
        }
    });
}

/**
 * Direct call to getSuggestions if we have the instance.
 * Builds a minimal protobuf request with the word.
 */
function directGetSuggestions(word) {
    if (dictFacilitatorInstance === null) {
        send({ type: "log", message: "[-] No DictionaryFacilitator instance for direct call" });
        return;
    }

    Java.perform(function () {
        try {
            // Encode word to UTF-8 bytes and wrap in minimal protobuf
            var wordStr = Java.use("java.lang.String").$new(word);
            var wordBytes = wordStr.getBytes(Java.use("java.lang.String").$new("UTF-8"));

            // Try calling getSuggestions directly
            pendingWord = word;
            var result = dictFacilitatorInstance.getSuggestions(wordBytes);

            if (result === null || result.length === 0) {
                send({
                    type: "result",
                    word: word,
                    weight: 0,
                    source: "java_direct",
                    suggestions: [],
                    scores: []
                });
                pendingWord = null;
            }
            // If result is non-null, the hooked implementation will process it
        } catch (e) {
            send({ type: "error", message: "directGetSuggestions failed: " + e });
            pendingWord = null;
        }
    });
}

// ============================================================
// Message handler: receive commands from Python orchestrator
// ============================================================
function handleMessage(message) {
    if (message === null || message === undefined) return;

    var msgType = message.type || "";

    switch (msgType) {
        case "lookup":
            // Active word lookup
            var word = message.word;
            if (!word) {
                send({ type: "error", message: "No word in lookup message" });
                break;
            }
            pendingWord = word;

            if (dictFacilitatorInstance !== null) {
                directGetSuggestions(word);
            } else {
                // Trigger via InputConnection
                setComposingWord(word);
                // Timeout: if no result in 500ms, report weight=0
                setTimeout(function () {
                    if (pendingWord === word) {
                        send({
                            type: "result",
                            word: word,
                            weight: 0,
                            source: "timeout",
                            suggestions: [],
                            scores: []
                        });
                        pendingWord = null;
                    }
                }, 500);
            }
            break;

        case "batch_lookup":
            // Batch of words - process sequentially
            var words = message.words || [];
            processBatch(words, 0);
            break;

        case "ping":
            send({ type: "pong" });
            break;

        case "status":
            send({
                type: "status",
                hookMode: hookMode,
                hasFacilitator: dictFacilitatorInstance !== null,
                hasNativeWeight: nativeWeightFn !== null,
                hasNativeSuggest: getSuggestionsNative !== null
            });
            break;

        default:
            send({ type: "log", message: "Unknown message type: " + msgType });
    }
}

function processBatch(words, index) {
    if (index >= words.length) {
        send({ type: "batch_done", count: words.length });
        return;
    }

    var word = words[index];
    pendingWord = word;

    if (dictFacilitatorInstance !== null) {
        directGetSuggestions(word);
    } else {
        setComposingWord(word);
    }

    // Move to next word after delay
    setTimeout(function () {
        if (pendingWord === word) {
            // Timed out - report zero weight
            send({
                type: "result",
                word: word,
                weight: 0,
                source: "timeout",
                suggestions: [],
                scores: []
            });
            pendingWord = null;
        }
        processBatch(words, index + 1);
    }, 200);
}

// ============================================================
// Setup recv handler for Python -> JS communication
// ============================================================
function setupRecvHandler() {
    recv("command", function (message) {
        handleMessage(message);
        // Re-register handler for next message
        setupRecvHandler();
    });
}

// ============================================================
// Main initialization
// ============================================================
(function main() {
    send({ type: "log", message: "=== Yandex Keyboard Frequency Extractor ===" });
    send({ type: "log", message: "[*] PID: " + Process.id });
    send({ type: "log", message: "[*] Arch: " + Process.arch });

    // Strategy 1: Java hook (primary)
    setupJavaHook();

    // Strategy 2: Native weight function
    setupNativeWeightHook();

    // Strategy 3: Native getSuggestions export
    setupNativeSuggestHook();

    // Setup message handler
    setupRecvHandler();

    // Signal readiness
    setTimeout(function () {
        send({
            type: "ready",
            hookMode: hookMode,
            hasFacilitator: dictFacilitatorInstance !== null,
            hasNativeWeight: nativeWeightFn !== null,
            hasNativeSuggest: getSuggestionsNative !== null
        });
    }, 1000);
})();
