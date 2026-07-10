import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionManager;
import ghidra.program.model.address.Address;
import ghidra.program.model.address.AddressFactory;
import ghidra.program.model.mem.Memory;
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import java.io.File;
import java.io.FileWriter;
import java.io.PrintWriter;
import java.util.LinkedHashSet;
import java.util.Set;

/**
 * R3 FIX: Decompile vtable functions used by FUN_00660496 (section reader)
 * to determine how much stream data each step consumes.
 *
 * We need:
 * 1. The vtable at PTR_FUN_00a112b0 - read function pointers at offsets +0x10, +0x18
 * 2. FUN_00661806 (BV reader) - how much it consumes
 * 3. FUN_006615a4 (DataSection reader) - how much it consumes
 * 4. Any other vtable pointers set during BV/DS reading
 * 5. FUN_003a1310 (stream_advance/sub-blob creator) - confirm consumption formula
 */
public class DumpVtableFunctions extends GhidraScript {
    @Override
    protected void run() throws Exception {
        File outFile = new File("C:\\Users\\Kharki\\DevEcoStudioProjects\\HOSKEY\\reverse\\R3_vtable_functions_2026-04-08.txt");
        PrintWriter writer = new PrintWriter(new FileWriter(outFile));

        FunctionManager fm = currentProgram.getFunctionManager();
        AddressFactory af = currentProgram.getAddressFactory();
        Memory mem = currentProgram.getMemory();

        DecompInterface decomp = new DecompInterface();
        decomp.openProgram(currentProgram);

        Set<Long> toDecompile = new LinkedHashSet<>();

        // 1. Known function addresses
        toDecompile.add(0x00661806L); // BV reader
        toDecompile.add(0x006615a4L); // DataSection reader
        toDecompile.add(0x003a1310L); // stream_advance / sub-blob
        toDecompile.add(0x00660496L); // section reader (for reference)
        toDecompile.add(0x0066084eL); // IntVector reader
        toDecompile.add(0x00660364L); // trie_struct_init (to find vtable addresses)

        // 2. Read vtable pointers from PTR_FUN_00a112b0
        // The vtable is at address 0x00a112b0 in the binary
        long vtableAddr = 0x00a112b0L;
        writer.println("=== VTABLE AT 0x00a112b0 ===");
        try {
            Address vtAddr = af.getDefaultAddressSpace().getAddress(vtableAddr);
            for (int offset = 0; offset <= 0x20; offset += 8) {
                Address ptrAddr = vtAddr.add(offset);
                long funcAddr = mem.getLong(ptrAddr);
                writer.println(String.format("  vtable+0x%02X: 0x%08X", offset, funcAddr));
                if (funcAddr > 0x1000 && funcAddr < 0x00FFFFFFL) {
                    toDecompile.add(funcAddr);
                }
            }
        } catch (Exception e) {
            writer.println("  Error reading vtable: " + e.getMessage());
        }

        // 3. Also check vtable at PTR_FUN_00a11260 (used for param_1+1 in main parser)
        long vtableAddr2 = 0x00a11260L;
        writer.println("\n=== VTABLE AT 0x00a11260 ===");
        try {
            Address vtAddr = af.getDefaultAddressSpace().getAddress(vtableAddr2);
            for (int offset = 0; offset <= 0x20; offset += 8) {
                Address ptrAddr = vtAddr.add(offset);
                long funcAddr = mem.getLong(ptrAddr);
                writer.println(String.format("  vtable+0x%02X: 0x%08X", offset, funcAddr));
                if (funcAddr > 0x1000 && funcAddr < 0x00FFFFFFL) {
                    toDecompile.add(funcAddr);
                }
            }
        } catch (Exception e) {
            writer.println("  Error reading vtable: " + e.getMessage());
        }

        // 4. Check vtable at PTR_FUN_00a11308 (used in main parser line 526)
        long vtableAddr3 = 0x00a11308L;
        writer.println("\n=== VTABLE AT 0x00a11308 ===");
        try {
            Address vtAddr = af.getDefaultAddressSpace().getAddress(vtableAddr3);
            for (int offset = 0; offset <= 0x20; offset += 8) {
                Address ptrAddr = vtAddr.add(offset);
                long funcAddr = mem.getLong(ptrAddr);
                writer.println(String.format("  vtable+0x%02X: 0x%08X", offset, funcAddr));
                if (funcAddr > 0x1000 && funcAddr < 0x00FFFFFFL) {
                    toDecompile.add(funcAddr);
                }
            }
        } catch (Exception e) {
            writer.println("  Error reading vtable: " + e.getMessage());
        }

        // 5. Search for other vtable assignments in FUN_00661806 (BV reader may set new vtable)
        // Decompile FUN_00661806 and look for vtable writes

        writer.println("\n\n=== DECOMPILING " + toDecompile.size() + " FUNCTIONS ===\n");

        for (long addrVal : toDecompile) {
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
                DecompileResults res = decomp.decompileFunction(func, 120, monitor);
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
        decomp.dispose();
        println("Dumped vtable functions to " + outFile.getAbsolutePath());
    }
}
