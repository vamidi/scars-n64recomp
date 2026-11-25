Put your own lawfully obtained S.C.A.R.S (Nintendo 64) inputs here.

Expected files (matching recomp/scars.toml):

  scars.z64   - the ROM dump (big-endian .z64)
  scars.elf   - the ELF with function/section/relocation metadata

N64Recomp cannot work from a ROM alone: it needs an ELF (or a symbols TOML
with [section] / functions) describing where every function starts and ends.
The ELF normally comes from a matching disassembly/decompilation project.

Neither file is tracked or redistributed by this repository.
