import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionManager;
import ghidra.program.model.address.Address;
import ghidra.program.model.address.AddressFactory;
import java.io.File;
import java.io.FileWriter;
import java.io.PrintWriter;

public class DecompileReverseFocus extends GhidraScript {
    @Override
    protected void run() throws Exception {
        long[] addrs = {
            0x0065f2d8L,
            0x0065f266L,
            0x00660badL,
            0x006614c9L,
            0x00660b5cL,
            0x00642388L,
            0x00660a4aL,
            0x00661b66L,
            0x00661926L
        };

        File outFile = new File("C:\\Users\\Kharki\\DevEcoStudioProjects\\HOSKEY\\reverse\\R3_reverse_focus_2026-04-08.txt");
        PrintWriter writer = new PrintWriter(new FileWriter(outFile));

        FunctionManager fm = currentProgram.getFunctionManager();
        AddressFactory af = currentProgram.getAddressFactory();
        DecompInterface decomp = new DecompInterface();
        decomp.openProgram(currentProgram);

        for (long addrVal : addrs) {
            Address addr = af.getDefaultAddressSpace().getAddress(addrVal);
            Function func = fm.getFunctionAt(addr);
            if (func == null) {
                func = fm.getFunctionContaining(addr);
            }

            writer.println("================================================================================");
            if (func != null) {
                writer.println("FUNCTION: " + func.getName() + " at " + func.getEntryPoint().toString());
                writer.println("SIZE: " + func.getBody().getNumAddresses() + " bytes");
                writer.println("================================================================================");
                DecompileResults res = decomp.decompileFunction(func, 180, monitor);
                if (res.decompileCompleted()) {
                    writer.println(res.getDecompiledFunction().getC());
                } else {
                    writer.println("Failed to decompile: " + res.getErrorMessage());
                }
            } else {
                writer.println("FUNCTION NOT FOUND at 0x" + Long.toHexString(addrVal));
                writer.println("================================================================================");
            }
            writer.println();
        }

        writer.close();
        println("Dumped reverse focus functions to " + outFile.getAbsolutePath());
    }
}
