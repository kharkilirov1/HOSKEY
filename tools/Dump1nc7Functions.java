import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import java.io.File;
import java.io.FileWriter;

public class Dump1nc7Functions extends GhidraScript {
    @Override
    protected void run() throws Exception {
        long[] addrs = {
            0x0065f2d8L, // 1nc7 parser
            0x00660496L, // section_loader
            0x00661806L, // bitvector_reader
            0x006615a4L, // data_section_reader
            0x00660364L, // trie_struct_init
            0x0066084eL, // level_builder
            0x00663c34L, // trie_init
            0x00661b66L, // state_builder
            0x00661926L, // traversal / child navigation
            0x00661538L, // rank1
            0x00661646L  // select1
        };

        String outPath = "C:\\Users\\Kharki\\DevEcoStudioProjects\\HOSKEY\\tools\\1nc7_parser_decompiled.c";
        File file = new File(outPath);
        FileWriter writer = new FileWriter(file);

        DecompInterface ifc = new DecompInterface();
        ifc.openProgram(currentProgram);

        for (long addrVal : addrs) {
            Address addr = currentProgram.getAddressFactory().getDefaultAddressSpace().getAddress(addrVal);
            Function func = getFunctionAt(addr);
            if (func == null) {
                println("Function not found at: 0x" + Long.toHexString(addrVal));
                writer.write("// Function not found at 0x" + Long.toHexString(addrVal) + "\n\n");
                continue;
            }

            writer.write("// ==================================================\n");
            writer.write("// Decompiled: " + func.getName() + " at 0x" + Long.toHexString(addrVal) + "\n");
            writer.write("// ==================================================\n\n");

            DecompileResults res = ifc.decompileFunction(func, 60, monitor);
            if (res.decompileCompleted()) {
                writer.write(res.getDecompiledFunction().getC());
            } else {
                writer.write("// Decompilation failed: " + res.getErrorMessage() + "\n");
            }
            writer.write("\n\n");
        }

        writer.close();
        println("Done dumping to " + outPath);
    }
}