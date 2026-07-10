# Ghidra Python Script to find and dump functions related to NGramFreqs
from ghidra.app.decompiler import DecompInterface
from ghidra.util.task import ConsoleTaskMonitor
import os

def run():
    target_strings = [
        "Invalid TotalFreq in NGramFreqs",
        "Trace queries to NGramFreqs model",
        "ngram_freqs.cpp",
        "TrieNGramFreqs"
    ]
    
    fm = currentProgram.getFunctionManager()
    listing = currentProgram.getListing()
    decomp = DecompInterface()
    decomp.openProgram(currentProgram)
    monitor = ConsoleTaskMonitor()
    
    out_path = r"C:\Users\Kharki\DevEcoStudioProjects\HOSKEY\reverse\decomp_ngram_freqs_2026-04-07.txt"
    out_lines = []
    
    for s in target_strings:
        # Search memory for strings (simplified logic, usually we search memory blocks)
        # Here we just iterate all strings found by Ghidra's string analyzer if possible,
        # but a simpler way is to find functions containing references to these strings.
        pass # To properly search strings in a short script, we use defined strings.
        
    strings = currentProgram.getListing().getDefinedData(True)
    found_addrs = set()
    
    for d in strings:
        val = d.getValue()
        if isinstance(val, str) or isinstance(val, unicode):
            for ts in target_strings:
                if ts in val:
                    # found string! get refs
                    refs = getReferencesTo(d.getMinAddress())
                    for ref in refs:
                        func = fm.getFunctionContaining(ref.getFromAddress())
                        if func:
                            found_addrs.add(func.getEntryPoint().getOffset())
    
    out_lines.append("Found {} functions related to NGramFreqs".format(len(found_addrs)))
    for addr in found_addrs:
        func = fm.getFunctionAt(toAddr(addr))
        out_lines.append("\n" + "="*80)
        out_lines.append("FUNCTION: {} at {}".format(func.getName(), hex(addr)))
        out_lines.append("="*80)
        res = decomp.decompileFunction(func, 60, monitor)
        if res.decompileCompleted():
            out_lines.append(res.getDecompiledFunction().getC())
        else:
            out_lines.append("Failed to decompile")
            
    with open(out_path, "w") as f:
        f.write("\n".join(out_lines).encode("utf-8"))
        
    print("Dumped NGramFreqs functions to {}".format(out_path))

run()
