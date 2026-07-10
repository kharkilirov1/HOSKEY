/**
 * R3 Frida v4 - Find 1nc7 trie file in process memory
 * Then scan for pointers INTO that region to find trie struct
 */
"use strict";

function rp(a) { try { return a.readPointer(); } catch(e) { return ptr(0); } }
function r8(a) { try { return a.readU8(); } catch(e) { return -1; } }
function r64(a) { try { return a.readU64(); } catch(e) { return ptr(0); } }

console.log("[*] Scanning ALL process memory for '1nc7' magic...");

var trieFiles = [];
var ranges = Process.enumerateRanges("r--");
console.log("[*] Total readable ranges: " + ranges.length);

for (var i = 0; i < ranges.length; i++) {
    var range = ranges[i];
    if (range.size < 1000000) continue; // trie file is ~4.5MB

    try {
        Memory.scan(range.base, range.size, "31 6E 63 37 01 00 00 00", {
            onMatch: function(addr, size) {
                var ds0_f0 = r64(addr.add(8));
                console.log("\n[!!!] Found 1nc7 at " + addr +
                    " (range: " + range.base + " size=" + range.size + ")");
                console.log("  DS0 f0 (should be ~999284): " + ds0_f0);

                // Verify it's our trie: DS0 f0 should be 999284
                if (ds0_f0.toInt32() === 999284) {
                    console.log("  CONFIRMED: This is the Yandex trie file!");
                    trieFiles.push({addr: addr, rangeBase: range.base, rangeSize: range.size});

                    // Read key offsets from the trie file
                    var fileBase = addr;
                    console.log("\n  === TRIE FILE LAYOUT ===");
                    console.log("  File base: " + fileBase);

                    // BV headers
                    var bvOffsets = [130744, 292744, 448936, 605128];
                    var bvNames = ["BV1", "BV2", "BV3", "BV4"];
                    for (var b = 0; b < 4; b++) {
                        var nw = r64(fileBase.add(bvOffsets[b]));
                        var rs = r64(fileBase.add(bvOffsets[b] + 8));
                        var pop = r64(fileBase.add(bvOffsets[b] + 16));
                        console.log("  " + bvNames[b] + " @ +" + bvOffsets[b] +
                            ": nw=" + nw + " rs=" + rs + " pop=" + pop);
                    }

                    // Metadata at 636392
                    console.log("  Metadata @ +636392:");
                    for (var m = 0; m < 4; m++) {
                        console.log("    [" + m + "] = " + r64(fileBase.add(636392 + m*8)));
                    }

                    // Now: scan heap for pointers pointing INTO this trie file
                    var fileStart = fileBase;
                    var fileEnd = fileBase.add(range.size < 5000000 ? range.size : 4498484);
                    console.log("\n  Scanning heap for trie struct pointers into " + fileStart + ".." + fileEnd);

                    var heapRanges = Process.enumerateRanges("rw-");
                    var structFound = false;
                    for (var hi = 0; hi < heapRanges.length && !structFound; hi++) {
                        var hr = heapRanges[hi];
                        if (hr.size > 100000000 || hr.size < 400) continue;
                        try {
                            var limit = Math.min(hr.size - 200, 5000000);
                            for (var off = 0; off < limit; off += 8) {
                                var p = hr.base.add(off);
                                // Check +0x178 (labels pointer)
                                var lblP = rp(p.add(0x178));
                                if (lblP.isNull()) continue;
                                if (lblP.compare(fileStart) < 0 || lblP.compare(fileEnd) > 0) continue;

                                // Check +0x010 (LOUDS bits)
                                var loudsP = rp(p.add(0x010));
                                if (loudsP.isNull()) continue;
                                if (loudsP.compare(fileStart) < 0 || loudsP.compare(fileEnd) > 0) continue;

                                // FOUND!
                                console.log("\n  [!!!] TRIE STRUCT at " + p);
                                var lblOffset = lblP.sub(fileBase).toInt32();
                                var loudsOffset = loudsP.sub(fileBase).toInt32();
                                console.log("    LABELS ptr -> file+" + lblOffset);
                                console.log("    LOUDS bits -> file+" + loudsOffset);

                                // Dump more fields
                                var hcP = rp(p.add(0x078));
                                var termP = rp(p.add(0x120));
                                var ivP = rp(p.add(0x160));
                                var lbl1P = rp(p.add(0x180));
                                var subP = rp(p.add(0x188));

                                if (!hcP.isNull()) console.log("    HC bits    -> file+" + hcP.sub(fileBase).toInt32());
                                if (!termP.isNull()) console.log("    TERM bits  -> file+" + termP.sub(fileBase).toInt32());
                                if (!ivP.isNull()) console.log("    IV data    -> file+" + ivP.sub(fileBase).toInt32());
                                console.log("    IV bw      = " + r8(p.add(0x168)));
                                if (!lbl1P.isNull()) console.log("    LABELS_1   -> file+" + lbl1P.sub(fileBase).toInt32());
                                if (!subP.isNull()) console.log("    SUB_TRIE   = " + subP);

                                // Read first 50 label bytes
                                var bytes = [];
                                for (var j = 0; j < 50; j++) bytes.push(r8(lblP.add(j)));
                                console.log("    LABELS[0..49]: " + JSON.stringify(bytes));

                                // Check sub-trie
                                if (!subP.isNull()) {
                                    console.log("\n    --- SUB-TRIE ---");
                                    var sl = rp(subP.add(0x178));
                                    var slo = rp(subP.add(0x010));
                                    if (!sl.isNull()) console.log("      SUB LABELS -> file+" + sl.sub(fileBase).toInt32());
                                    if (!slo.isNull()) console.log("      SUB LOUDS  -> file+" + slo.sub(fileBase).toInt32());
                                    if (!sl.isNull()) {
                                        var sb = [];
                                        for (var j = 0; j < 30; j++) sb.push(r8(sl.add(j)));
                                        console.log("      SUB LABELS[0..29]: " + JSON.stringify(sb));
                                    }
                                }

                                structFound = true;
                                break;
                            }
                        } catch(e) {}
                    }

                    if (!structFound) console.log("  Trie struct not found in heap.");
                }
                return "stop";
            },
            onComplete: function() {}
        });
    } catch(e) {}
}

if (trieFiles.length === 0) {
    console.log("\n[!] No 1nc7 files found in memory!");
}
