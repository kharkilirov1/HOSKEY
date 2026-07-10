import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.program.model.symbol.Symbol;
import ghidra.program.model.symbol.SymbolIterator;
import ghidra.program.model.symbol.SymbolType;
import ghidra.program.model.listing.Function;
import java.io.File;
import java.io.FileWriter;

public class DumpNNLMFunctions extends GhidraScript {
    @Override
    protected void run() throws Exception {
        String outPath = "C:\\Users\\Kharki\\DevEcoStudioProjects\\HOSKEY\\tools\\nnlm_decompiled.c";
        File file = new File(outPath);
        FileWriter writer = new FileWriter(file);

        DecompInterface ifc = new DecompInterface();
        ifc.openProgram(currentProgram);

        SymbolIterator it = currentProgram.getSymbolTable().getAllSymbols(true);
        int dumped = 0;
        
        while (it.hasNext() && !monitor.isCancelled()) {
            Symbol sym = it.next();
            if (sym.getSymbolType() != SymbolType.FUNCTION) continue;
            
            String name = sym.getName(true); // Demangled
            if (name == null) name = sym.getName();
            String nameLower = name.toLowerCase();
            String origNameLower = sym.getName().toLowerCase();
            
            if (nameLower.contains("subword") || nameLower.contains("nnlm") || nameLower.contains("neuralword") ||
                origNameLower.contains("subword") || origNameLower.contains("nnlm") || origNameLower.contains("neuralword")) {
                 
                Function func = getFunctionAt(sym.getAddress());
                if (func == null) continue;
                
                writer.write("// ==================================================\n");
                writer.write("// Demangled: " + name + "\n");
                writer.write("// Original: " + sym.getName() + "\n");
                writer.write("// @ 0x" + Long.toHexString(sym.getAddress().getOffset()) + "\n");
                writer.write("// ==================================================\n\n");
                
                DecompileResults res = ifc.decompileFunction(func, 60, monitor);
                if (res.decompileCompleted()) {
                    writer.write(res.getDecompiledFunction().getC());
                } else {
                    writer.write("// Failed: " + res.getErrorMessage() + "\n");
                }
                writer.write("\n\n");
                dumped++;
                
                if (dumped > 200) break;
            }
        }

        writer.close();
        println("Dumped " + dumped + " functions to " + outPath);
    }
}
