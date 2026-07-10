# Ghidra Python Script to dump decompiled functions
from ghidra.app.decompiler import DecompInterface
from ghidra.util.task import ConsoleTaskMonitor
import os

function_addresses = [
    0x0065f2d8, # 1nc7 parser
    0x00660496, # section_loader
    0x00661806, # bitvector_reader
    0x006615a4, # data_section_reader
    0x00660364, # trie_struct_init
    0x0066084e, # level_builder
    0x00663c34, # trie_init
    0x00661b66, # state_builder
    0x00661926, # traversal / child navigation
    0x00661538, # rank1
    0x00661646  # select1
]

out_path = "C:\\Users\\Kharki\\DevEcoStudioProjects\\HOSKEY\\tools\\1nc7_parser_decompiled.c"

def run():
    monitor = ConsoleTaskMonitor()
    ifc = DecompInterface()
    ifc.openProgram(currentProgram)

    with open(out_path, "w") as f:
        for addr_val in function_addresses:
            addr = currentProgram.getAddressFactory().getDefaultAddressSpace().getAddress(addr_val)
            func = getFunctionAt(addr)
            if func is None:
                print("Function not found at: 0x%x" % addr_val)
                f.write("// Function not found at 0x%x\n\n" % addr_val)
                continue
            
            f.write("// ==================================================\n")
            f.write("// Decompiled: %s at 0x%x\n" % (func.getName(), addr_val))
            f.write("// ==================================================\n\n")
            
            res = ifc.decompileFunction(func, 60, monitor)
            if res.decompileCompleted():
                f.write(res.getDecompiledFunction().getC())
            else:
                f.write("// Decompilation failed: %s\n" % res.getErrorMessage())
            f.write("\n\n")
    print("Done dumping to " + out_path)

run()
