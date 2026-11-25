// Seeds Ghidra's MIPS analysis at the S.C.A.R.S entry point.
//
// The ROM is imported as a raw binary at base 0x801FF400, so ROM offset
// 0x1000 lands at the linked address 0x80200400 (the header entry point).
// This script marks the block executable and disassembles the boot stub so
// the normal analyzers can follow calls into the game code.

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.mem.MemoryBlock;

public class ScarsSeed extends GhidraScript {
    @Override
    public void run() throws Exception {
        long entry = 0x80200400L;
        Address addr = toAddr(entry);
        MemoryBlock block = currentProgram.getMemory().getBlock(addr);
        if (block == null) {
            println("ScarsSeed: no memory block contains " + addr);
            return;
        }
        if (!block.isExecute()) {
            block.setExecute(true);
        }
        println("ScarsSeed: seeding analysis at " + addr + " (block " + block.getName() + ")");
        disassemble(addr);
        if (getFunctionAt(addr) == null) {
            createFunction(addr, "entry");
        }
        println("ScarsSeed: " + getFunctionAt(addr));
    }
}
