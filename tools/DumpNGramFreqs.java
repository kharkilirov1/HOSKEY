import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionManager;
import ghidra.program.model.listing.Listing;
import ghidra.program.model.address.Address;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.ReferenceIterator;
import ghidra.program.model.symbol.ReferenceManager;
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import java.io.File;
import java.io.FileWriter;
import java.io.PrintWriter;
import java.util.HashSet;
import java.util.Set;

public class DumpNGramFreqs extends GhidraScript {
    @Override
    protected void run() throws Exception {
        String[] targetStrings = {
            "Invalid TotalFreq in NGramFreqs",
            "Trace queries to NGramFreqs model",
            "ngram_freqs.cpp",
            "TrieNGramFreqs"
        };

        File outFile = new File("C:\\Users\\Kharki\\DevEcoStudioProjects\\HOSKEY\\reverse\\decomp_ngram_freqs_2026-04-07.txt");
        PrintWriter writer = new PrintWriter(new FileWriter(outFile));

        FunctionManager fm = currentProgram.getFunctionManager();
        ReferenceManager rm = currentProgram.getReferenceManager();
        
        Set<Function> foundFunctions = new HashSet<>();

        // Find strings by looking at defined data
        ghidra.program.model.listing.DataIterator dataIter = currentProgram.getListing().getDefinedData(true);
        while (dataIter.hasNext()) {
            ghidra.program.model.listing.Data data = dataIter.next();
            Object val = data.getValue();
            if (val instanceof String) {
                String strVal = (String) val;
                for (String target : targetStrings) {
                    if (strVal.contains(target)) {
                        ReferenceIterator refs = rm.getReferencesTo(data.getMinAddress());
                        while (refs.hasNext()) {
                            Reference ref = refs.next();
                            Function func = fm.getFunctionContaining(ref.getFromAddress());
                            if (func != null) {
                                foundFunctions.add(func);
                            }
                        }
                    }
                }
            }
        }

        writer.println("Found " + foundFunctions.size() + " functions related to NGramFreqs");
        
        DecompInterface decomp = new DecompInterface();
        decomp.openProgram(currentProgram);

        for (Function func : foundFunctions) {
            writer.println("\n================================================================================");
            writer.println("FUNCTION: " + func.getName() + " at " + func.getEntryPoint().toString());
            writer.println("================================================================================");
            DecompileResults res = decomp.decompileFunction(func, 60, monitor);
            if (res.decompileCompleted()) {
                writer.println(res.getDecompiledFunction().getC());
            } else {
                writer.println("Failed to decompile");
            }
        }
        
        writer.close();
        println("Dumped NGramFreqs functions to " + outFile.getAbsolutePath());
    }
}
