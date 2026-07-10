import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionManager;
import ghidra.program.model.address.Address;
import ghidra.program.model.address.AddressFactory;
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import java.io.File;
import java.io.FileWriter;
import java.io.PrintWriter;

public class DumpNGramHelpers extends GhidraScript {
    @Override
    protected void run() throws Exception {
        long[] addrs = {
            0x00608f58,
            0x006096e6,
            0x006092f6,
            0x00609346,
            0x0060c9f3,
            0x00609a96,
            0x006091bc,
            0x00609a08,
            0x006098ce,
            0x007560a2, // FUN_007560a2 (caller 1)
            0x00756eb0  // FUN_00756eb0 (caller 2)
        };

        File outFile = new File("C:\\Users\\Kharki\\DevEcoStudioProjects\\HOSKEY\\reverse\\decomp_ngram_helpers_2026-04-07.txt");
        PrintWriter writer = new PrintWriter(new FileWriter(outFile));

        FunctionManager fm = currentProgram.getFunctionManager();
        AddressFactory af = currentProgram.getAddressFactory();
        
        DecompInterface decomp = new DecompInterface();
        decomp.openProgram(currentProgram);

        writer.println("Dumping " + addrs.length + " helper functions for N-Gram Frequency Parsing:");
        
        for (long addrVal : addrs) {
            Address addr = af.getDefaultAddressSpace().getAddress(addrVal);
            Function func = fm.getFunctionAt(addr);
            if (func == null) {
                func = fm.getFunctionContaining(addr);
            }
            
            writer.println("\n================================================================================");
            if (func != null) {
                writer.println("FUNCTION: " + func.getName() + " at " + func.getEntryPoint().toString());
                writer.println("================================================================================");
                DecompileResults res = decomp.decompileFunction(func, 60, monitor);
                if (res.decompileCompleted()) {
                    writer.println(res.getDecompiledFunction().getC());
                } else {
                    writer.println("Failed to decompile: " + res.getErrorMessage());
                }
            } else {
                writer.println("FUNCTION NOT FOUND at 0x" + Long.toHexString(addrVal));
                writer.println("================================================================================");
            }
        }
        
        writer.close();
        println("Dumped NGramHelpers functions to " + outFile.getAbsolutePath());
    }
}