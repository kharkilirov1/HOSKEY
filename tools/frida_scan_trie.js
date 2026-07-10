/**
 * R3 Frida v3 - scan memory for trie struct and extract labels pointer
 * No need to wait for keyboard input - reads existing loaded data
 */
"use strict";

const LIB = "libjni_ykeyboard3.so";

function rp(a) { try { return a.readPointer(); } catch(e) { return ptr(0); } }
function r8(a) { try { return a.readU8(); } catch(e) { return -1; } }
function r32(a) { try { return a.readU32(); } catch(e) { return -1; } }
function r64s(a) { try { return a.readS64(); } catch(e) { return -1; } }

var mod = Process.findModuleByName(LIB);
if (!mod) { console.log("Library not found!"); } else {
    var base = mod.base;
    var sz = mod.size;
    console.log("[*] " + LIB + " base=" + base + " size=" + sz);

    // Method 1: Read ELF sections to find .data/.bss where trie struct lives
    console.log("\n[*] Memory ranges for " + LIB + ":");
    var ranges = mod.enumerateRanges("r--");
    for (var i = 0; i < Math.min(ranges.length, 20); i++) {
        var r = ranges[i];
        var off = r.base.sub(base);
        console.log("  " + r.base + " size=" + r.size + " prot=" + r.protection +
            " offset=0x" + off.toString(16));
    }

    // Method 2: Find the 1nc7 trie file in memory
    // The trie file starts with "1nc7" magic (bytes 0x31, 0x6E, 0x63, 0x37)
    console.log("\n[*] Searching for '1nc7' magic in process memory...");
    var pattern = "31 6E 63 37 01 00 00 00"; // "1nc7" + version 1
    var results = Memory.scan(base, sz, pattern, {
        onMatch: function(addr, size) {
            var off = addr.sub(base);
            console.log("  Found 1nc7 at " + addr + " (lib offset 0x" + off.toString(16) + ")");

            // Read the trie file header
            console.log("  Header: " + JSON.stringify([r8(addr), r8(addr.add(1)), r8(addr.add(2)),
                r8(addr.add(3)), r32(addr.add(4))]));

            // Check what follows: DS0 at offset +8
            var ds0_f0 = r64s(addr.add(8));
            var ds0_count = r64s(addr.add(16));
            console.log("  DS0: f0=" + ds0_f0 + " count=" + ds0_count);

            // Now read the label data we think is at +636424
            var lblCandidate = addr.add(636424);
            var first30 = [];
            for (var j = 0; j < 30; j++) first30.push(r8(lblCandidate.add(j)));
            console.log("  Data at +636424: " + JSON.stringify(first30));

            // This is the trie data base address - we need to find the struct that points into it
            console.log("\n  Scanning heap for pointers INTO this trie data...");
            return "stop";
        },
        onComplete: function() {}
    });

    // Method 3: Scan ALL heap for pointers matching trie data patterns
    // Look for any pointer P where:
    // P+0x178 contains a valid pointer into the trie file region
    // AND P+0x010 contains a valid pointer into the trie file
    // These would be trie struct instances

    // First find all memory mapped sections of the .so that contain the trie data
    console.log("\n[*] Scanning all readable memory for trie structs...");

    // The trie struct has pointers at specific offsets
    // Let's search for the pattern in heap
    var heapRanges = Process.enumerateRanges("rw-");
    var found = false;

    for (var ri = 0; ri < heapRanges.length && !found; ri++) {
        var range = heapRanges[ri];
        if (range.size > 50000000) continue; // skip huge ranges
        if (range.size < 400) continue; // too small for trie struct

        try {
            // Scan for 400-byte structs where fields look like valid pointers
            var step = 8;
            var limit = Math.min(range.size - 400, 2000000);

            for (var off = 0; off < limit; off += step) {
                var p = range.base.add(off);
                // Check if +0x178 is a valid pointer into .so or mapped file
                var lblPtr = rp(p.add(0x178));
                if (lblPtr.isNull()) continue;
                var lblOff = lblPtr.sub(base);
                // Labels should be somewhere in the loaded trie file data
                // The trie is ~4.5MB, loaded within the .so's range
                if (lblOff.toInt32() < 0 || lblOff.toInt32() > sz) continue;

                // Also check LOUDS ptr at +0x010
                var loudsPtr = rp(p.add(0x010));
                if (loudsPtr.isNull()) continue;
                var loudsOff = loudsPtr.sub(base);
                if (loudsOff.toInt32() < 0 || loudsOff.toInt32() > sz) continue;

                // Check has_child at +0x078
                var hcPtr = rp(p.add(0x078));
                if (hcPtr.isNull()) continue;
                var hcOff = hcPtr.sub(base);
                if (hcOff.toInt32() < 0 || hcOff.toInt32() > sz) continue;

                // All 3 pointers valid! This is likely a trie struct
                console.log("\n[!!!] FOUND TRIE STRUCT at " + p);
                dumpTrieStruct(p, base);
                found = true;
                break;
            }
        } catch(e) {
            // Skip unreadable ranges
        }
    }

    if (!found) {
        console.log("[*] Trie struct not found in heap. Trying .bss/.data sections...");
        // Try within the library's own writable sections
        var rwRanges = mod.enumerateRanges("rw-");
        for (var ri = 0; ri < rwRanges.length && !found; ri++) {
            var range = rwRanges[ri];
            console.log("  Checking rw range: " + range.base + " size=" + range.size);
            try {
                var limit = range.size - 400;
                for (var off = 0; off < limit; off += 8) {
                    var p = range.base.add(off);
                    var lblPtr = rp(p.add(0x178));
                    if (lblPtr.isNull()) continue;
                    var lblOff = lblPtr.sub(base).toInt32();
                    if (lblOff < 100000 || lblOff > sz) continue;

                    var loudsPtr = rp(p.add(0x010));
                    if (loudsPtr.isNull()) continue;
                    var loudsOff = loudsPtr.sub(base).toInt32();
                    if (loudsOff < 0 || loudsOff > sz) continue;

                    console.log("\n[!!!] FOUND TRIE STRUCT at " + p);
                    dumpTrieStruct(p, base);
                    found = true;
                    break;
                }
            } catch(e) {}
        }
    }
}

function dumpTrieStruct(ptr, base) {
    var fields = [
        [0x000, "vtable      "],
        [0x010, "LOUDS_bits  "],
        [0x078, "HC_bits     "],
        [0x0A0, "HC_struct   "],
        [0x120, "TERM_bits   "],
        [0x160, "IV_data     "],
        [0x178, "LABELS      "],
        [0x180, "LABELS_1    "],
        [0x188, "SUB_TRIE    "],
    ];

    console.log("=== TRIE STRUCT at " + ptr + " ===");
    for (var i = 0; i < fields.length; i++) {
        var off = fields[i][0];
        var name = fields[i][1];
        var val = rp(ptr.add(off));
        var foff = val.isNull() ? "NULL" : "0x" + val.sub(base).toString(16);
        console.log("  +" + ("000" + off.toString(16)).slice(-3) + " " + name + " = " + val + " (lib+" + foff + ")");
    }

    // IV bitwidth byte at +0x168
    console.log("  +168 IV_bitwidth   = " + r8(ptr.add(0x168)));
    // IV mask at +0x170
    console.log("  +170 IV_mask       = 0x" + r64s(ptr.add(0x170)).toString(16));

    // Read first 50 label bytes
    var lblPtr = rp(ptr.add(0x178));
    if (!lblPtr.isNull()) {
        var bytes = [];
        for (var j = 0; j < 50; j++) bytes.push(r8(lblPtr.add(j)));
        console.log("\n  LABELS[0..49]: " + JSON.stringify(bytes));
        console.log("  LABELS lib offset: 0x" + lblPtr.sub(base).toString(16));

        // Check: are siblings unique?
        // LOUDS bits: read first u64
        var loudsPtr = rp(ptr.add(0x010));
        if (!loudsPtr.isNull()) {
            var w0 = r64s(loudsPtr);
            console.log("\n  LOUDS first u64: 0x" + (w0 >>> 0).toString(16));
        }
    }

    // Check sub-trie
    var subPtr = rp(ptr.add(0x188));
    if (!subPtr.isNull()) {
        console.log("\n  --- SUB-TRIE ---");
        dumpTrieStruct(subPtr, base);
    }
}
