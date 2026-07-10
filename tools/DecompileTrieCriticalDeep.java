import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionManager;
import ghidra.program.model.address.Address;
import java.io.File;
import java.io.FileWriter;
import java.io.PrintWriter;

public class DecompileTrieCriticalDeep extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] addrs = new String[] {
            "0x00660496", // recursive trie section loader
            "0x0066084e", // intvector/tail reader
            "0x00661462", // label lookup
            "0x00661538", // rank1
            "0x006615a4", // data section reader
            "0x00661646", // select1
            "0x00661806", // bitvector reader
            "0x00661926", // traversal
            "0x00661b66", // state builder leaf
            "0x00661baa"  // state builder terminal
        };

        File outFile = new File("C:\\Users\\Kharki\\DevEcoStudioProjects\\HOSKEY\\reverse\\R3_trie_critical_deep_2026-04-08.txt");
        PrintWriter pw = new PrintWriter(new FileWriter(outFile));

        DecompInterface decomp = new DecompInterface();
        decomp.openProgram(currentProgram);

        FunctionManager fm = currentProgram.getFunctionManager();

        for (String addrStr : addrs) {
            Address addr = currentProgram.getAddressFactory().getAddress(addrStr);
            Function f = fm.getFunctionAt(addr);
            if (f == null) {
                f = fm.getFunctionContaining(addr);
            }
            pw.println("================================================================================");
            pw.println("ADDRESS: " + addrStr);
            if (f == null) {
                pw.println("Function not found");
                pw.println("================================================================================");
                pw.println();
                continue;
            }
            pw.println("FUNCTION: " + f.getName() + " @ " + f.getEntryPoint());
            pw.println("SIZE: " + f.getBody().getNumAddresses() + " bytes");
            pw.println("================================================================================");
            DecompileResults res = decomp.decompileFunction(f, 180, monitor);
            if (res.decompileCompleted()) {
                pw.println(res.getDecompiledFunction().getC());
            } else {
                pw.println("Decompile failed: " + res.getErrorMessage());
            }
            pw.println();
        }

        decomp.dispose();
        pw.close();
        println("Decompiled to " + outFile.getAbsolutePath());
    }
}
