/**
 * R3 Frida v5 - synchronous scan for ALL 1nc7 files, then find trie struct
 */
"use strict";

function rp(a) { try { return a.readPointer(); } catch(e) { return ptr(0); } }
function r8(a) { try { return a.readU8(); } catch(e) { return -1; } }

var ranges = Process.enumerateRanges("r--");
console.log("Readable ranges: " + ranges.length);

// Search for "1nc7" + version 1 pattern
var pattern = "31 6E 63 37 01 00 00 00";
var allMatches = [];

for (var i = 0; i < ranges.length; i++) {
    var r = ranges[i];
    if (r.size < 500000) continue;
    try {
        var matches = Memory.scanSync(r.base, r.size, pattern);
        for (var m = 0; m < matches.length; m++) {
            allMatches.push({addr: matches[m].address, range: r});
        }
    } catch(e) {}
}

console.log("\nFound " + allMatches.length + " '1nc7' instances");

for (var i = 0; i < allMatches.length; i++) {
    var addr = allMatches[i].addr;
    var range = allMatches[i].range;
    // Read DS0 f0 (u64 at offset +8)
    try {
        var f0_lo = addr.add(8).readU32();
        var f0_hi = addr.add(12).readU32();
        var count_lo = addr.add(16).readU32();

        console.log("\n[" + i + "] 1nc7 at " + addr +
            " range=" + range.base + " rangeSize=" + range.size +
            " f0=" + f0_lo + " count=" + count_lo);

        // Our Russian trie has f0=999284
        if (f0_lo === 999284 && count_lo === 15614) {
            console.log("  >>> RUSSIAN TRIE CONFIRMED! <<<");
            var fileBase = addr;

            // Verify BV headers
            var bv1_nw = fileBase.add(130744).readU32();
            var bv1_pop = fileBase.add(130744 + 16).readU32();
            console.log("  BV1: nw=" + bv1_nw + " pop=" + bv1_pop);

            // Find trie struct: scan heap for pointers into this file
            var fileEnd = fileBase.add(4498484);
            console.log("  File region: " + fileBase + " to " + fileEnd);

            var heapRanges = Process.enumerateRanges("rw-");
            console.log("  Heap ranges to scan: " + heapRanges.length);

            for (var hi = 0; hi < heapRanges.length; hi++) {
                var hr = heapRanges[hi];
                if (hr.size < 400 || hr.size > 50000000) continue;
                try {
                    for (var off = 0; off < hr.size - 400; off += 8) {
                        var p = hr.base.add(off);
                        // Quick check: +0x178 points into trie file?
                        var v178 = rp(p.add(0x178));
                        if (v178.isNull()) continue;
                        if (v178.compare(fileBase) < 0 || v178.compare(fileEnd) > 0) continue;

                        // Double check: +0x010 also in range?
                        var v010 = rp(p.add(0x010));
                        if (v010.isNull()) continue;
                        if (v010.compare(fileBase) < 0 || v010.compare(fileEnd) > 0) continue;

                        // FOUND
                        var lblOff = v178.sub(fileBase).toInt32();
                        var loudsOff = v010.sub(fileBase).toInt32();

                        console.log("\n  === TRIE STRUCT at " + p + " ===");
                        console.log("    +0x010 LOUDS bits -> file+" + loudsOff);

                        var v078 = rp(p.add(0x078));
                        if (!v078.isNull() && v078.compare(fileBase) >= 0 && v078.compare(fileEnd) <= 0)
                            console.log("    +0x078 HC bits    -> file+" + v078.sub(fileBase).toInt32());

                        var v120 = rp(p.add(0x120));
                        if (!v120.isNull() && v120.compare(fileBase) >= 0 && v120.compare(fileEnd) <= 0)
                            console.log("    +0x120 TERM bits  -> file+" + v120.sub(fileBase).toInt32());

                        var v160 = rp(p.add(0x160));
                        if (!v160.isNull() && v160.compare(fileBase) >= 0 && v160.compare(fileEnd) <= 0)
                            console.log("    +0x160 IV data    -> file+" + v160.sub(fileBase).toInt32());

                        console.log("    +0x168 IV bw      = " + r8(p.add(0x168)));

                        console.log("    +0x178 LABELS     -> file+" + lblOff);

                        var v180 = rp(p.add(0x180));
                        if (!v180.isNull() && v180.compare(fileBase) >= 0 && v180.compare(fileEnd) <= 0)
                            console.log("    +0x180 LABELS_1   -> file+" + v180.sub(fileBase).toInt32());

                        // Sub-trie pointer
                        var v188 = rp(p.add(0x188));
                        if (!v188.isNull())
                            console.log("    +0x188 SUB_TRIE   = " + v188);

                        // Labels data
                        var lbl = [];
                        for (var j = 0; j < 100; j++) lbl.push(r8(v178.add(j)));
                        console.log("    LABELS[0..99]: " + JSON.stringify(lbl));

                        // Sub-trie
                        if (!v188.isNull()) {
                            var sv178 = rp(v188.add(0x178));
                            var sv010 = rp(v188.add(0x010));
                            if (!sv178.isNull() && sv178.compare(fileBase) >= 0 && sv178.compare(fileEnd) <= 0) {
                                console.log("\n    SUB-TRIE:");
                                console.log("      LABELS -> file+" + sv178.sub(fileBase).toInt32());
                                if (!sv010.isNull() && sv010.compare(fileBase) >= 0)
                                    console.log("      LOUDS  -> file+" + sv010.sub(fileBase).toInt32());
                                var sl = [];
                                for (var j = 0; j < 50; j++) sl.push(r8(sv178.add(j)));
                                console.log("      SUB LABELS[0..49]: " + JSON.stringify(sl));
                            }
                        }

                        // Only need first match
                        break;
                    }
                } catch(e) {}
            }
            console.log("  Struct not found in heap.");
        }
    } catch(e) {
        console.log("  Error reading: " + e.message);
    }
}
