# Merges n64sym libultra symbol names into the Ghidra-generated symbols TOML.
#
# n64sym identifies libultra functions by code signature; Ghidra gives the
# function boundaries. N64Recomp already knows how to replace/ignore those
# libultra names, so naming them correctly removes the CP0/cache/TLB bodies
# it cannot translate.

import re, sys

symbols_path = sys.argv[1] if len(sys.argv) > 1 else r'D:\Personal\C++\scars-n64recomp\build\symbols\scars.symbols.toml'
n64sym_path = sys.argv[2] if len(sys.argv) > 2 else r'D:\Personal\C++\scars-n64recomp\build\symbols\n64sym.txt'

# Functions n64sym's built-in signatures miss in this build. Verified against
# ultralib's unmaptlball.s (EntryHi save, Index 0..N, tlbwi, EntryHi restore).
manual_names = {
    0x802750C0: 'osUnmapTLBAll',
    0x80275110: 'osWritebackDCacheAll',
    # n64sym leaves the libultra functions that touch hardware registers
    # unnamed, so N64Recomp translates them and the generated code reads MMIO
    # addresses as if they were RDRAM (a wild pointer, since 0xA4500014 and
    # friends are outside the 4 GiB guest space). Each of these was identified
    # from its register accesses and call shape, and every one of them is a name
    # N64Recomp already replaces with a runtime implementation:
    #   osAiSetFrequency  - writes AI_DACRATE(AI_BITRATE)/AI_CONTROL
    #   osAiSetNextBuffer - osVirtualToPhysical + writes AI_DRAM_ADDR/AI_LEN
    #   __osAiDeviceBusy  - reads AI_STATUS, returns status < 0
    #   osDpSetStatus     - writes DP_STATUS
    #   osCreatePiManager - builds the PI message queue and PI thread
    #   osCartRomInit     - reads PI_BSD_DOM1/2_LAT|PWD|PGS|RLS into OSPiHandle
    0x8027FE40: 'osAiSetFrequency',
    0x8027FF70: 'osAiSetNextBuffer',
    0x80280010: '__osAiDeviceBusy',
    0x80280030: 'osDpSetStatus',
    0x80284A10: 'osCreatePiManager',
    0x80284BB0: 'osCartRomInit',
    # n64sym names every other function in libultra's continit.c but skips
    # osContInit itself. Leaving it unnamed translates the raw SI controller
    # protocol (pack request -> SI DMA -> wait for the completion message), which
    # the runtime cannot answer. 0x80281450 calls __osSiCreateAccessQueue,
    # __osContGetInitData and __osSiRawStartDma, which is osContInit's shape.
    0x80281450: 'osContInit',
}

names = {}
for line in open(n64sym_path, encoding='utf-8'):
    parts = line.split()
    if len(parts) >= 2 and re.fullmatch(r'[0-9A-Fa-f]{8}', parts[0]):
        names[int(parts[0], 16)] = parts[1]

lines = open(symbols_path, encoding='utf-8').read().splitlines()
renamed = 0
unmatched = set(names)
i = 0
while i < len(lines):
    if lines[i].strip() == '[[section.functions]]':
        j = i
        while j < len(lines) and not lines[j].startswith('size = '):
            j += 1
        name_i = vram_i = None
        for k in range(i, min(j + 1, len(lines))):
            if lines[k].startswith('name = '):
                name_i = k
            elif lines[k].startswith('vram = '):
                vram_i = k
        if vram_i is not None:
            vram = int(lines[vram_i].split('0x')[1], 16)
            if vram in manual_names:
                lines[name_i] = 'name = "%s"' % manual_names[vram]
                renamed += 1
            elif vram in names:
                lines[name_i] = 'name = "%s"' % names[vram]
                renamed += 1
                unmatched.discard(vram)
        i = j
    i += 1

# The boot entry ends at the `break` instruction; the word after it is data.
for i, line in enumerate(lines):
    if line == 'name = "recomp_entrypoint"' or line == 'name = "entry"':
        for j in range(i, min(i + 4, len(lines))):
            if lines[j].startswith('size = '):
                lines[j] = 'size = 0x3C'
                break

with open(symbols_path, 'w', encoding='utf-8', newline='\n') as f:
    f.write('\n'.join(lines) + '\n')

print('renamed %d functions from n64sym' % renamed)
print('n64sym symbols with no matching Ghidra function: %d' % len(unmatched))
for a in sorted(unmatched)[:20]:
    print('  0x%08X %s' % (a, names[a]))


