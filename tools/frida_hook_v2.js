/**
 * R3 Frida hook v2 - robust, with try-catch per hook
 * Focus: capture labels pointer + first label bytes
 */
"use strict";

const LIB = "libjni_ykeyboard3.so";

// Offsets from Ghidra (these are file offsets, need to verify they match runtime)
const FNS = {
    main_parser:    0x0065f2d8,
    section_reader: 0x00660496,
    trie_iterator:  0x00661926,
    terminal_lookup:0x00661462,
    bv_reader:      0x00661806,
    ds_reader:      0x006615a4,
    iv_reader:      0x0066084e,
    rank1_fn:       0x00661538,
    select1_fn:     0x00661646,
    stream_adv:     0x003a1310,
    trie_init:      0x00660364,
};

function tryHook(name, offset, callbacks) {
    try {
        var addr = libBase.add(offset);
        Interceptor.attach(addr, callbacks);
        console.log("  [OK] hooked " + name + " at " + addr);
        return true;
    } catch(e) {
        console.log("  [FAIL] " + name + " at offset 0x" + offset.toString(16) + ": " + e.message);
        // Try offset +/- 1 for Thumb mode
        try {
            var addr2 = libBase.add(offset & ~1);
            Interceptor.attach(addr2, callbacks);
            console.log("  [OK] hooked " + name + " (aligned) at " + addr2);
            return true;
        } catch(e2) {
            return false;
        }
    }
}

function rp(addr) { try { return addr.readPointer(); } catch(e) { return ptr(0); } }
function r8(addr) { try { return addr.readU8(); } catch(e) { return -1; } }
function r64(addr) { try { return addr.readU64(); } catch(e) { return -1; } }

var libBase = null;
var hookCount = 0;

function doHook() {
    var mod = Process.findModuleByName(LIB);
    if (!mod) { console.log("[!] lib not loaded, retry..."); setTimeout(doHook, 2000); return; }
    libBase = mod.base;
    console.log("[*] " + LIB + " base=" + libBase + " size=" + mod.size);

    // First: just scan memory for existing trie structs
    // The trie_init sets *param = &PTR_FUN_00a112b0
    // So look for pointers to libBase + 0x00a112b0 in writable memory
    var vtableAddr = libBase.add(0x00a112b0);
    console.log("[*] Scanning for trie structs with vtable " + vtableAddr + "...");

    // Read the vtable to understand it
    console.log("[*] Vtable at 0xa112b0:");
    for (var i = 0; i <= 0x20; i += 8) {
        try {
            var fptr = vtableAddr.add(i).readPointer();
            var off = fptr.sub(libBase);
            console.log("  +" + i.toString(16) + ": " + fptr + " (offset 0x" + off.toString(16) + ")");
        } catch(e) {
            console.log("  +" + i.toString(16) + ": unreadable");
        }
    }

    // Hook trie_init to catch when trie structs are created
    tryHook("trie_init", FNS.trie_init, {
        onEnter: function(args) {
            this.structPtr = args[0];
            console.log("\n[trie_init] struct=" + this.structPtr);
        },
        onLeave: function(retval) {
            // After init, struct has vtable but no data yet
        }
    });

    // Hook main_parser - this is the entry point for trie loading
    tryHook("main_parser", FNS.main_parser, {
        onEnter: function(args) {
            this.out = args[0];
            this.stream = args[1];
            console.log("\n[main_parser] ENTER out=" + this.out + " stream=" + this.stream);
            if (!this.stream.isNull()) {
                var start = rp(this.stream);
                var len = rp(this.stream.add(8));
                console.log("  stream: start=" + start + " len=" + len);
                if (!start.isNull()) {
                    var bytes = [];
                    for (var i = 0; i < 8; i++) bytes.push(r8(start.add(i)));
                    console.log("  first 8 bytes: " + JSON.stringify(bytes));
                }
            }
        },
        onLeave: function(retval) {
            console.log("[main_parser] LEAVE");
            // After parsing, out[0] should point to the trie struct
            if (!this.out.isNull()) {
                var triePtr = rp(this.out);
                console.log("  trie struct at: " + triePtr);
                if (!triePtr.isNull()) {
                    dumpTrieStruct(triePtr);
                }
            }
        }
    });

    // Hook terminal_lookup - called during suggestion lookup
    var termCount = 0;
    tryHook("terminal_lookup", FNS.terminal_lookup, {
        onEnter: function(args) {
            this.base = args[0];
            this.nodeId = args[1].toInt32();
            this.rank = args[2].toInt32();
        },
        onLeave: function(retval) {
            termCount++;
            if (termCount <= 50 || termCount % 200 === 0) {
                console.log("[term_lookup] #" + termCount +
                    " node=" + this.nodeId + " rank=" + this.rank +
                    " => 0x" + retval.toString(16) +
                    " (byte=" + (retval.toInt32() & 0xFF) +
                    " upper=" + ((retval.toInt32() >> 8) & 0xFFFF) + ")");
            }
        }
    });

    // Hook trie_iterator
    tryHook("trie_iterator", FNS.trie_iterator, {
        onEnter: function(args) {
            // Not much to log on enter
        }
    });

    // Hook rank1 to see what BVs are used
    var rankCount = 0;
    tryHook("rank1", FNS.rank1_fn, {
        onEnter: function(args) {
            this.structOff = args[0];
            this.bitsPtr = args[1];
            this.pos = args[2].toInt32();
        },
        onLeave: function(retval) {
            rankCount++;
            if (rankCount <= 20) {
                var bitsFileOff = this.bitsPtr.sub(libBase);
                console.log("[rank1] #" + rankCount +
                    " bits_offset=0x" + bitsFileOff.toString(16) +
                    " pos=" + this.pos +
                    " => " + retval.toInt32());
            }
        }
    });

    // Hook select1
    var selCount = 0;
    tryHook("select1", FNS.select1_fn, {
        onEnter: function(args) {
            this.bitsPtr = args[1];
            this.k = args[2].toInt32();
        },
        onLeave: function(retval) {
            selCount++;
            if (selCount <= 20) {
                var bitsFileOff = this.bitsPtr.sub(libBase);
                console.log("[select1] #" + selCount +
                    " bits_offset=0x" + bitsFileOff.toString(16) +
                    " k=" + this.k +
                    " => " + retval.toInt32());
            }
        }
    });

    console.log("\n[*] Hooks installed. Now type on the keyboard!");
}

function dumpTrieStruct(ptr) {
    console.log("\n=== TRIE STRUCT DUMP ===");
    var fields = [
        [0x000, "vtable"],
        [0x010, "LOUDS_bits"],
        [0x018, "LOUDS_struct"],
        [0x078, "HC_bits"],
        [0x080, "HC_struct"],
        [0x120, "TERM_bits"],
        [0x128, "TERM_struct"],
        [0x160, "IV_data"],
        [0x168, "IV_bitwidth"],
        [0x170, "IV_mask"],
        [0x178, "LABELS"],
        [0x180, "LABELS_1"],
        [0x188, "SUB_TRIE"],
    ];
    for (var i = 0; i < fields.length; i++) {
        var off = fields[i][0];
        var name = fields[i][1];
        var val = rp(ptr.add(off));
        var fileOff = val.sub(libBase);
        console.log("  +" + off.toString(16).padStart(3, '0') + " " + name.padEnd(14) +
            " = " + val + " (file_off=0x" + fileOff.toString(16) + ")");
    }

    // Read first 30 label bytes
    var lblPtr = rp(ptr.add(0x178));
    if (!lblPtr.isNull()) {
        var bytes = [];
        for (var i = 0; i < 50; i++) bytes.push(r8(lblPtr.add(i)));
        console.log("  LABELS first 50 bytes: " + JSON.stringify(bytes));

        // Also compute file offset relative to known trie file sections
        var lblOff = lblPtr.sub(libBase);
        console.log("  LABELS ptr offset from lib base: 0x" + lblOff.toString(16) + " (" + lblOff.toInt32() + ")");
    }

    // Check sub-trie
    var subPtr = rp(ptr.add(0x188));
    if (!subPtr.isNull()) {
        console.log("\n  --- SUB-TRIE ---");
        dumpTrieStruct(subPtr);
    }
}

doHook();
