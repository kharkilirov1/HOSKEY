/**
 * R3 FIX: Frida hook to capture label pointer and access patterns
 * from Yandex keyboard's libjni_ykeyboard3.so
 *
 * Usage:
 *   frida -U -n com.yandex.keyboard -l frida_hook_labels.js
 *   (or attach to the keyboard process on device/emulator)
 *
 * What this captures:
 * 1. Labels pointer (+0x178) when set by section reader
 * 2. Label byte values when accessed during trie traversal
 * 3. LOUDS/has_child BV pointers for understanding structure
 */

"use strict";

const LIB_NAME = "libjni_ykeyboard3.so";

// Key function offsets (from Ghidra analysis)
const OFFSETS = {
    main_parser:      0x0065f2d8,  // FUN_0065f2d8 - main 1nc7 parser
    section_reader:   0x00660496,  // FUN_00660496 - reads BVs + labels
    trie_iterator:    0x00661926,  // FUN_00661926 - navigates trie
    terminal_lookup:  0x00661462,  // FUN_00661462 - label access with IntVector
    bv_reader:        0x00661806,  // FUN_00661806 - bitvector reader
    ds_reader:        0x006615a4,  // FUN_006615a4 - datasection reader
    iv_reader:        0x0066084e,  // FUN_0066084e - intvector reader
    rank1:            0x00661538,  // FUN_00661538 - rank1
    select1:          0x00661646,  // FUN_00661646 - select1
    stream_advance:   0x003a1310,  // FUN_003a1310 - sub-blob creator
    trie_init:        0x00660364,  // FUN_00660364 - trie struct init
};

let libBase = null;
let labelsPtr = null;
let labelsBaseFileOffset = null;
let trieStructPtr = null;
let callCount = 0;
const MAX_LOG = 200;

function getAddr(offset) {
    return libBase.add(offset);
}

function tryReadPtr(addr) {
    try { return addr.readPointer(); } catch(e) { return null; }
}

function tryReadU8(addr) {
    try { return addr.readU8(); } catch(e) { return -1; }
}

function tryReadU64(addr) {
    try { return addr.readU64(); } catch(e) { return -1; }
}

function hookAll() {
    const mod = Process.findModuleByName(LIB_NAME);
    if (!mod) {
        console.log("[!] " + LIB_NAME + " not loaded yet, retrying in 2s...");
        setTimeout(hookAll, 2000);
        return;
    }

    libBase = mod.base;
    console.log("[*] Found " + LIB_NAME + " at " + libBase);
    console.log("[*] Module size: " + mod.size);

    // === HOOK 1: section_reader (FUN_00660496) ===
    // After it returns, the trie struct has labels at +0x178
    Interceptor.attach(getAddr(OFFSETS.section_reader), {
        onEnter: function(args) {
            // args: param_1 (output stream), param_2 (trie struct), param_3? (input stream)
            this.trieStruct = args[1]; // pvVar11 = trie struct (400 bytes)
            console.log("\n[section_reader] ENTER trie_struct=" + this.trieStruct);
        },
        onLeave: function(retval) {
            if (this.trieStruct && !this.trieStruct.isNull()) {
                trieStructPtr = this.trieStruct;
                // Read key pointers from trie struct
                var louds_bits = tryReadPtr(this.trieStruct.add(0x10));
                var hc_bits = tryReadPtr(this.trieStruct.add(0x78));
                var term_bits = tryReadPtr(this.trieStruct.add(0x120));
                var iv_data = tryReadPtr(this.trieStruct.add(0x160));
                var iv_bw = tryReadU8(this.trieStruct.add(0x168));
                var lbl_ptr = tryReadPtr(this.trieStruct.add(0x178));
                var lbl1_ptr = tryReadPtr(this.trieStruct.add(0x180));
                var sub_trie = tryReadPtr(this.trieStruct.add(0x188));

                labelsPtr = lbl_ptr;

                console.log("[section_reader] LEAVE - Trie struct pointers:");
                console.log("  +0x010 LOUDS bits:    " + louds_bits);
                console.log("  +0x078 has_child bits: " + hc_bits);
                console.log("  +0x120 terminal bits:  " + term_bits);
                console.log("  +0x160 IntVector data: " + iv_data);
                console.log("  +0x168 IV bitwidth:    " + iv_bw);
                console.log("  +0x178 LABELS ptr:     " + lbl_ptr);
                console.log("  +0x180 LABELS+1 ptr:   " + lbl1_ptr);
                console.log("  +0x188 sub-trie:       " + sub_trie);

                // Calculate file offset of labels
                if (lbl_ptr && !lbl_ptr.isNull()) {
                    var fileOffset = lbl_ptr.sub(libBase);
                    console.log("  LABELS file offset (from lib base): " + fileOffset);

                    // Read first 50 label bytes
                    console.log("  First 50 label bytes:");
                    var bytes = [];
                    for (var i = 0; i < 50; i++) {
                        bytes.push(tryReadU8(lbl_ptr.add(i)));
                    }
                    console.log("    " + JSON.stringify(bytes));
                }

                // Also read LOUDS bits - first u64
                if (louds_bits && !louds_bits.isNull()) {
                    console.log("  LOUDS first u64: 0x" + tryReadU64(louds_bits).toString(16));
                    console.log("  LOUDS file offset: " + louds_bits.sub(libBase));
                }
                if (hc_bits && !hc_bits.isNull()) {
                    console.log("  HC first u64: 0x" + tryReadU64(hc_bits).toString(16));
                    console.log("  HC file offset: " + hc_bits.sub(libBase));
                }
                if (term_bits && !term_bits.isNull()) {
                    console.log("  TERM first u64: 0x" + tryReadU64(term_bits).toString(16));
                    console.log("  TERM file offset: " + term_bits.sub(libBase));
                }
            }
        }
    });

    // === HOOK 2: terminal_rank_lookup (FUN_00661462) ===
    // This is the label access function
    Interceptor.attach(getAddr(OFFSETS.terminal_lookup), {
        onEnter: function(args) {
            this.trieBase = args[0];
            this.nodeId = args[1].toInt32();
            this.termRank = args[2].toInt32();
        },
        onLeave: function(retval) {
            if (callCount < MAX_LOG) {
                var labelByte = retval.toInt32() & 0xFF;
                var upperBits = (retval.toInt32() >> 8) & 0xFFFF;
                callCount++;
                if (callCount <= 30 || callCount % 50 === 0) {
                    console.log("[terminal_lookup] node=" + this.nodeId +
                        " termRank=" + this.termRank +
                        " => label_byte=" + labelByte +
                        " upper=" + upperBits +
                        " full=0x" + retval.toString(16));
                }
            }
        }
    });

    // === HOOK 3: trie_iterator (FUN_00661926) ===
    // Log the label read at the specific instruction
    // Line 17321: *param_2 = *(undefined1 *)(*(long *)(lVar6 + 0x178) + uVar1)
    Interceptor.attach(getAddr(OFFSETS.trie_iterator), {
        onEnter: function(args) {
            // args depend on calling convention
            this.callId = callCount;
        },
        onLeave: function(retval) {
            // Just log that iteration happened
        }
    });

    // === HOOK 4: main_parser (FUN_0065f2d8) ===
    // Capture when parsing starts and the input stream
    Interceptor.attach(getAddr(OFFSETS.main_parser), {
        onEnter: function(args) {
            console.log("\n[main_parser] ENTER");
            console.log("  param_1 (output): " + args[0]);
            console.log("  param_2 (input stream): " + args[1]);
            if (args[1] && !args[1].isNull()) {
                var streamStart = tryReadPtr(args[1]);
                var streamLen = tryReadPtr(args[1].add(8));
                console.log("  stream start addr: " + streamStart);
                console.log("  stream length: " + streamLen);
                if (streamStart && !streamStart.isNull()) {
                    // Read first 8 bytes (should be "1nc7" + version)
                    var magic = [];
                    for (var i = 0; i < 8; i++) {
                        magic.push(tryReadU8(streamStart.add(i)));
                    }
                    console.log("  first 8 bytes (magic+ver): " + JSON.stringify(magic));
                    var magicStr = String.fromCharCode(magic[0], magic[1], magic[2], magic[3]);
                    console.log("  magic string: '" + magicStr + "'");
                }
            }
        },
        onLeave: function(retval) {
            console.log("[main_parser] LEAVE");
        }
    });

    // === HOOK 5: stream_advance (FUN_003a1310) ===
    // Log stream consumption to track offsets
    var streamLogCount = 0;
    Interceptor.attach(getAddr(OFFSETS.stream_advance), {
        onEnter: function(args) {
            if (streamLogCount < 100) {
                var inputStart = tryReadPtr(args[1]);
                var offset = args[2].toInt32();
                var end = args[3].toInt32();
                streamLogCount++;
                if (streamLogCount <= 40) {
                    console.log("[stream_advance] input=" + inputStart +
                        " offset=" + offset + " end=" + end +
                        " consumed=" + offset + " remaining=" + (end - offset));
                }
            }
        }
    });

    // === HOOK 6: BV reader (FUN_00661806) ===
    Interceptor.attach(getAddr(OFFSETS.bv_reader), {
        onEnter: function(args) {
            console.log("[bv_reader] ENTER dest_struct=" + args[1]);
        },
        onLeave: function(retval) {
            console.log("[bv_reader] LEAVE");
        }
    });

    // === HOOK 7: DS reader (FUN_006615a4) ===
    Interceptor.attach(getAddr(OFFSETS.ds_reader), {
        onEnter: function(args) {
            console.log("[ds_reader] ENTER dest=" + args[1]);
        },
        onLeave: function(retval) {
            console.log("[ds_reader] LEAVE");
        }
    });

    // === HOOK 8: IntVector reader (FUN_0066084e) ===
    Interceptor.attach(getAddr(OFFSETS.iv_reader), {
        onEnter: function(args) {
            console.log("[iv_reader] ENTER dest=" + args[1]);
        },
        onLeave: function(retval) {
            console.log("[iv_reader] LEAVE");
        }
    });

    console.log("[*] All hooks installed. Type something on the keyboard to trigger trie lookup.");
    console.log("[*] Waiting for " + LIB_NAME + " function calls...");
}

// Start hooking
hookAll();
