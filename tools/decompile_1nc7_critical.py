#!/usr/bin/env python3
"""
Decompile critical 1nc7 functions from Ghidra project using PyGhidra.
Target: FUN_00661926 (traversal), FUN_00661462 (label lookup), FUN_0066084e (level builder)
"""

import sys
import os

# Add PyGhidra to path
sys.path.insert(0, r'C:\Users\Kharki\AppData\Local\Programs\Python\Python312\Lib\site-packages')

try:
    import pyghidra
    from pyghidra import open_project
except ImportError as e:
    print(f"PyGhidra not available: {e}")
    sys.exit(1)

# Critical addresses to decompile
TARGETS = {
    0x00661926: "TrieTraversal_FUN_00661926",  # Main traversal - MOST IMPORTANT
    0x00661462: "LabelLookup_FUN_00661462",    # Label lookup at +0x160/+0x178
    0x0066084e: "LevelBuilder_FUN_0066084e",   # IntVector reader
    0x00661538: "Rank1_FUN_00661538",          # Rank operation
    0x00661646: "Select1_FUN_00661646",        # Select operation
    0x006615a4: "DataSectionReader_FUN_006615a4",  # Data section reader
    0x00661806: "BitvectorReader_FUN_00661806",    # Bitvector reader
    0x00660496: "SectionLoader_FUN_00660496",      # Section loader
}

PROJECT_DIR = r'C:\Users\Kharki\ghidra_yandex'
PROJECT_NAME = 'YKB'
OUTPUT_PATH = r'C:\Users\Kharki\DevEcoStudioProjects\HOSKEY\tools\1nc7_critical_decompiled.c'

def decompile_targets():
    """Decompile all target functions."""
    
    print(f"Opening Ghidra project: {PROJECT_DIR}/{PROJECT_NAME}")
    
    with open_project(PROJECT_DIR, PROJECT_NAME) as proj:
        print(f"Project opened: {proj}")
        
        # Get the program (libjni_ykeyboard3.so)
        root_folder = proj.getProjectRootFolder()
        files = root_folder.getFiles()
        
        program = None
        for f in files:
            print(f"Found file: {f.getName()}")
            if 'libjni_ykeyboard3' in f.getName():
                program = proj.openProgram(f, False)
                break
        
        if program is None:
            print("ERROR: libjni_ykeyboard3.so not found in project!")
            return False
        
        print(f"Opened program: {program.getName()}")
        
        # Create decompiler
        from ghidra.app.decompiler import DecompInterface
        decomp = DecompInterface()
        decomp.openProgram(program)
        
        with open(OUTPUT_PATH, 'w', encoding='utf-8') as out:
            out.write("/* 1nc7 Critical Functions Decompilation */\n")
            out.write(f"/* Project: {PROJECT_DIR}/{PROJECT_NAME} */\n")
            out.write(f"/* Binary: {program.getName()} */\n\n")
            
            for addr, name in TARGETS.items():
                print(f"\nDecompiling {name} at 0x{addr:08x}...")
                
                try:
                    func_addr = program.getAddressFactory().getDefaultAddressSpace().getAddress(addr)
                    func = program.getFunctionManager().getFunctionContaining(func_addr)
                    
                    if func is None:
                        func = program.getFunctionManager().getFunctionAt(func_addr)
                    
                    if func is None:
                        out.write(f"\n/* === 0x{addr:08x}: {name} === */\n")
                        out.write(f"// NO FUNCTION FOUND at 0x{addr:08x}\n\n")
                        print(f"  WARNING: No function at 0x{addr:08x}")
                        continue
                    
                    # Decompile
                    results = decomp.decompileFunction(func, 60, pyghidra.core.monitor)
                    
                    if results.decompileCompleted():
                        out.write(f"\n/* === 0x{addr:08x}: {name} ({func.getName()}) === */\n\n")
                        out.write(results.getDecompiledFunction().getC())
                        out.write("\n\n")
                        print(f"  SUCCESS: Decompiled {func.getName()}")
                    else:
                        out.write(f"\n/* === 0x{addr:08x}: {name} === */\n")
                        out.write(f"// DECOMPILE FAILED: {results.getErrorMessage()}\n\n")
                        print(f"  FAILED: {results.getErrorMessage()}")
                        
                except Exception as e:
                    out.write(f"\n/* === 0x{addr:08x}: {name} === */\n")
                    out.write(f"// ERROR: {str(e)}\n\n")
                    print(f"  ERROR: {e}")
                    import traceback
                    traceback.print_exc()
            
            out.write(f"\n/* End of decompilation */\n")
        
        decomp.dispose()
        proj.close(program)
    
    print(f"\n✓ Decompilation complete: {OUTPUT_PATH}")
    return True

if __name__ == '__main__':
    try:
        success = decompile_targets()
        sys.exit(0 if success else 1)
    except Exception as e:
        print(f"Fatal error: {e}")
        import traceback
        traceback.print_exc()
        sys.exit(1)
