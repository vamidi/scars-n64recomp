# S.C.A.R.S (Nintendo 64) - native static recompilation (state)

> Started 2026-11-25 (session 1). This file is the live state document.
> Append a session entry at the bottom after each work block; do not rewrite
> earlier entries when a finding is superseded - add a correction in the new
> entry, the way the PS2 project state does.
>
> Inputs (ROM, ELF, symbols, generated corpus) are local only and gitignored.
> Nothing in this file should be read as permission to redistribute them.

---

## CURRENT STATUS - updated 2026-11-25 (session 1; read this first)

**Goal:** boot the Europe release of S.C.A.R.S natively and render it in a
window, using static recompilation (N64Recomp) + N64ModernRuntime, the same
architecture as Zelda 64: Recompiled and the Diddy Kong Racing recomp.

**One-line status:** the toolchain and the AOT corpus work end to end - the
game's boot image is translated to C, compiled, and linked - but nothing boots
it yet. `scars.exe` is still a scaffold: there is no renderer, no RSP
microcode, no input/audio, and the link closure of the corpus has never been
forced, so unresolved `_recomp` symbols are still undiscovered.

**What exists now**

| Milestone | Status | Evidence |
|---|---|---|
| CMake scaffold, presets, submodules | Working | MSVC 19.51 and clang-cl 22 both build |
| Function map from the ROM | Working | Ghidra headless: **841 functions**, `0x80200400..0x80288058` |
| libultra naming | Working | n64sym + merge: **296 named**, **545** remain `FUN_*` |
| AOT translation | Working | N64Recomp: 15 `funcs_*.c`, `funcs.h`, `lookup.cpp`, `recomp_overlays.inl` (~11.7 MB) |
| Corpus compiles | Working | all 16 translation units (15 `funcs_*.c` + `lookup.cpp`) compile and archive |
| Corpus link closure | **Not tested** | the archive is linked but no generated symbol is referenced |
| Runtime boot (`recomp_entrypoint`) | **Not started** | `src/runtime/main.cpp` prints a banner and returns |
| Renderer / window / VI | **Not started** | no RT64/SDL dependency yet |
| RSP microcode (graphics/audio) | **Not started** | ucode family not identified yet |
| Audio output | **Not started** | no audio callbacks |
| Input | **Not started** | no controller backend |
| Saves / Controller Pak | **Not started** | ROM imports `osPfs*`, `osMotor*` |
| Overlay support | **Unverified** | symbols map has one `.boot` section, no relocs |
| First rendered frame | **Not started** | requires the four rows above |
| Packaging / Switch target | **Not started** | PC only |

**Immediate next action:** make the linker pull the corpus (Phase 0 below).
That single change turns the remaining runtime work from guesswork into a
finite list of missing symbols.

---

## VERIFIED FACTS (do not re-derive)

### ROM

| Field | Value |
|---|---|
| Path (local) | `data/scars.z64` |
| Size | `0x800000` (8 MiB) |
| Byte order | big-endian `.z64` (`80 37 12 40`) |
| Internal name | `SCARS` |
| Manufacturer / cart / country | `N` / `CS` / `P` (PAL Europe) |
| Version | 0 |
| Entry point | `0x80200400` |
| libultra version field | `0x00001444` |
| SHA-1 | `8b4ebad8b7f50381ba2e16d0c475332837bb5780` |
| SHA-256 | `3a11dc6bd22531527a452dab24c1d9146e4bf6228f4aace1a897ad447482df8a` |
| Runtime ROM hash | **XXH3-64** `0x4C035BA764A51971` |

The runtime validates ROMs with `XXH3_64bits(rom.data(), rom.size())`
(`librecomp/src/recomp.cpp:164`), not SHA. `tools/rom_info.py` prints all of
the above from a local dump.

### Link map / memory layout

| Fact | Value |
|---|---|
| ROM -> VRAM mapping for the boot image | ROM `0x1000` -> VRAM `0x80200400` |
| Boot stub | `0x80200400..0x8020043C` (`size 0x3C`) |
| Word at `0x8020043C` | **data**, not code (`0x080F0000`); function ends at the `break` at `0x80200438` |
| First call from the entry stub | `jal 0x802011A8` |
| BSS clear loop (from the boot stub) | `0x802B6050..0x803FFF20` |
| Implied RDRAM | 4 MiB (top of BSS `0x803FFF20`); no Expansion Pak |
| Recompiled code section | `rom=0x1000`, `vram=0x80200400`, `size=0x87CBC` |
| Last discovered function | `0x80288058`, ends `0x802880BC` |
| Data after the code | fills the rest of the boot image up to `_bss_start` |

### AOT corpus

| Fact | Value |
|---|---|
| Functions | 841 |
| Named by n64sym/merge | 296 |
| Still `FUN_*` | 545 |
| Generated files | 18 (15 `funcs_*.c` + `funcs.h` + `lookup.cpp` + `recomp_overlays.inl`) |
| Generated size | ~11.7 MB |
| Sections | 1 (`.boot`), no relocations, no overlays registered |
| Known generation warning | `[Info] Indirect tail call in FUN_802757b4` (not fatal) |

Functions N64Recomp replaces rather than translates are emitted as
`<name>_recomp` with `rom_size = 0`, for example:
`osInitialize_recomp`, `osPiStartDma_recomp`, `osPfsFreeBlocks_recomp`,
`osMotorInit_recomp`, `bzero_recomp`, `sprintf_recomp`, `strlen_recomp`,
`memcpy_recomp`, `ldiv_recomp`, `__divdi3_recomp`, `__umoddi3_recomp`.
Those must exist at link time once the corpus is pulled in.

### Runtime facts / known defects

- `lookup.cpp` currently returns `get_rom_name() == ".z64"` in symbols-file
  mode. N64Recomp derives the name from `elf_path`, which is empty here
  (`src/main.cpp` around line 910). Nothing validates the name yet, but ROM
  validation/UI will show it wrong.
- `osPfs*`, `osMotor*`, `leo*` functions are present, and the ROM contains
  `Controller Pak` strings. No EEPROM/SRAM/Flash strings were found. Treat
  save type as **undetermined** until the game code is inspected.
- Graphics/audio microcode is **not identified**. n64sym's built-in signatures
  did not name a `gsp*`/`aspMain` text block, so this needs the task-structure
  path described in Phase 3.

---

## BUILD / RUN (this machine)

```bat
:: configure + build tools, runtime libraries and the scaffold
cmd /c 'call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul && cmake --preset windows-x64-release && cmake --build --preset windows-x64-release'
```

```bat
:: regenerate the AOT corpus (Ghidra + n64sym + N64Recomp)
cmake --build build/windows-x64-release --target generate-aot

:: or run the pipeline directly
powershell -NoProfile -ExecutionPolicy Bypass -File tools/generate_aot.ps1
```

```bat
:: compile/link the generated corpus
cmake --preset windows-x64-release -DSCARS_LINK_GENERATED=ON
cmake --build --preset windows-x64-release

:: current executable (scaffold only)
build\windows-x64-release\bin\scars.exe
```

clang-cl alternative:

```bat
cmake -S . -B build/clang -G Ninja ^
  -DCMAKE_C_COMPILER="C:/Program Files/Microsoft Visual Studio/18/Community/VC/Tools/Llvm/x64/bin/clang-cl.exe" ^
  -DCMAKE_CXX_COMPILER="C:/Program Files/Microsoft Visual Studio/18/Community/VC/Tools/Llvm/x64/bin/clang-cl.exe" ^
  -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build/clang
```

---

## NEXT STEPS (in order)

Each step states its deliverable and how to know it worked. Do not start a
later phase until the earlier acceptance line passes; if it cannot pass, record
the measurement in a new session entry and adjust the plan.

### Phase 0 - Close the link and inventory the runtime (highest priority)

**Why:** linking a static archive does not pull any object unless a symbol is
referenced. Today the corpus can hide unresolved `_recomp`/`os*` symbols, so
the true size of the runtime gap is unknown.

- [ ] **0.1 Force one reference to `recomp_entrypoint`.**
  In `src/runtime/main.cpp`, add a declaration and a reference that the
  optimizer cannot remove, for example:

  ```cpp
  extern "C" void recomp_entrypoint(uint8_t* rdram, recomp_context* context);
  // keeps the archive member (and its call graph) linked in
  static void (*volatile const scars_entry_ref)(uint8_t*, recomp_context*) = &recomp_entrypoint;
  // reference it from main so the compiler cannot drop it
  (void)scars_entry_ref;
  ```

  Build with `-DSCARS_LINK_GENERATED=ON`.
  *Accept:* the build either links, or fails with a finite list of unresolved
  symbols.

- [ ] **0.2 Record and resolve every unresolved symbol.**
  Append the missing list to this file (session entry), then:
  - if N64ModernRuntime already implements it, link the owning library;
  - if it is a libultra function the runtime should provide, add a stub in a
    new `src/runtime/ultra_stubs_scars.cpp` (`extern "C" void name_recomp(...)`)
    that logs once and returns a safe value;
  - if it is game code, the symbols map is missing a function: fix the map.
  *Accept:* `scars.exe` links with the corpus pulled and no unresolved symbols.

- [ ] **0.3 Fix the ROM name in symbols mode (your fork).**
  In `thirdparty/n64recomp/src/main.cpp`, the `lookup.cpp` generator uses
  `config.elf_path.filename()`; use `config.rom_file_path.filename()` when
  `elf_path` is empty. Commit upstream in your fork, then update the submodule.
  *Accept:* `build/generated/lookup.cpp` returns `"scars.z64"`.

### Phase 1 - Boot harness

**Why:** the generated `recomp_entrypoint` must be called by a properly
initialised runtime, with a game entry that matches this ROM hash.

- [ ] **1.1 Add `src/runtime/scars_game.cpp/.h`.**
  Define and register:

  ```cpp
  recomp::GameEntry{
      .rom_hash = 0x4C035BA764A51971,
      .internal_name = "SCARS",
      .display_name = "S.C.A.R.S",
      .game_id = u8"SCARS",
      .save_type = recomp::SaveType::AllowAll, // placeholder; see 5.4
      .is_enabled = true,
      .entrypoint_address = 0x80200400,
      .entrypoint = recomp_entrypoint, // becomes the scars_entry wrapper in 1.3
  };
  ```

  Keep the hash in one named constant next to the tool that prints it.

- [ ] **1.2 Replace the scaffold `main.cpp`.**
  Call `recomp::register_config_path`, `recomp::start(cfg)` (callbacks),
  `recomp::register_game`, `recomp::select_rom` with the default
  `data/scars.z64`, then `recomp::start_game`. This is the order
  `librecomp/src/recomp.cpp` and the Zelda 64 port use; do not reorder
  `recomp::start` after `ultramodern::preinit`.
  Fill every callback in `recomp::Configuration`. For the first pass:
  - renderer/RSP callbacks: log and return safe values, abort with a clear
    message only when a real task arrives;
  - audio/input/events/error/thread callbacks: log/no-op.
  *Accept:* running `scars.exe` logs the entry point being executed, or
  reaches the first graphics call and stops with a precise message.

- [ ] **1.3 Add a trace/wrapper entry.**
  Keep the generated `recomp_entrypoint` untouched. Register
  `scars_entry` instead: it logs, then calls `recomp_entrypoint`. This gives a
  stable place for boot-step logging without regenerating the corpus.
  *Accept:* the log shows `recomp_entrypoint` start, stack setup, BSS clear,
  and the `jal 0x802011A8` target being reached.

### Phase 2 - Renderer (RT64) and window

**Why:** N64ModernRuntime requires renderer callbacks; the game cannot run
past its first VI/RDP activity without a backend.

- [ ] **2.1 Add the renderer dependency.**
  Add `thirdparty/rt64` as a submodule and wire it in CMake with
  `RT64_STATIC=TRUE`, `HLSL_CPU`, SDL window support. On Windows, use the SDL2
  prebuilt package the way `D:\Personal\C++\DK64Recomp-NX\CMakeLists.txt`
  does; on Linux/macOS use the system SDL2. Ship `SDL2.dll` (and RT64's DXC
  DLLs) next to `scars.exe` as a POST_BUILD copy.
  *Accept:* `cmake --build` produces `rt64` and `scars.exe` links it.

- [ ] **2.2 Implement `gfx_callbacks_t` and `renderer::callbacks_t`.**
  Create the RT64 application/window, map resolution scale, target framerate,
  and present. This is the analogue of ToL's `src/gfx/backends/sdlgpu`.
  *Accept:* a window opens and a (black) frame is presented; closing it exits
  cleanly.

- [ ] **2.3 Map VI behaviour.**
  Register the VI/retrace callbacks and confirm `recomp::start` reaches the
  first `osViSwapBuffer` in generated code.
  *Accept:* the first VI swap is logged with a non-zero framebuffer address.

### Phase 3 - RSP microcode (graphics and audio)

**Why:** display lists and audio are RSP tasks. Without recompiled microcode
the renderer gets no RDP work and audio produces no samples.

- [ ] **3.1 Identify the graphics ucode.**
  With Ghidra, find the first `osSpTaskLoad`/`osSpTaskStartGo` call site and
  dump the `OSTask.ucode` pointer plus the PI DMA that populates it. Resolve
  the ROM offset of the ucode text/data. Record the family (F3DEX2,
  F3DZEX2, S2DEX2, custom) and its `ucode_data`.
  *Accept:* a table entry in this file: ucode text ROM offset, RDRAM address,
  size, family.

- [ ] **3.2 Recompile the graphics ucode.**
  Add an RSPRecomp config under `recomp/rsp/`, generate the C, compile it into
  the runtime, and wire `recomp::rsp::callbacks_t` and the `ultramodern` RSP
  dispatch. Route task submission through the generated ucode, not a stub.
  *Accept:* the first display-list task completes and RT64 receives RDP
  commands (log the first command word).

- [ ] **3.3 Identify and recompile the audio ucode.**
  Repeat 3.1/3.2 for the audio task and connect `audio_callbacks_t` to an SDL
  audio queue (48 kHz stereo is the usual N64 default; confirm the AI
  frequency the game sets).
  *Accept:* the audio callback receives non-zero sample counts.

### Phase 4 - Boot to the first frame

- [ ] **4.1 Capture a reference boot from an N64 emulator.**
  Use a debugger-capable emulator (mupen64plus with debugger, ares, or
  Project64 + debugger) and record, with timestamps:
  entry `0x80200400`, first PI DMA, `osInitialize`, first SP task, first VI
  swap, first controller read, first display list pointer, first RDP command.
  *Accept:* a milestone table in this file, from the emulator, not inferred.

- [ ] **4.2 Diff the port against the reference, one milestone at a time.**
  Add a `SCARS_TRACE=1` log line for each reference milestone. Fix the first
  divergence only; do not chase later ones. Use
  `src/runtime/scars_overrides.cpp` for per-address corrections, following the
  pattern of ToL's `tol_overrides.cpp`.
  *Accept:* the port reaches the same first divergence-free milestone chain.

- [ ] **4.3 Reach the first rendered frame.**
  Define "first frame" as a non-black image read from the presented buffer,
  not merely a VI swap. Save it under `logs/` for evidence.
  *Accept:* a screenshot with non-black pixels and a documented input path
  (attract mode, logo, or menu).

### Phase 5 - Correctness and completeness (required for "fully working")

- [ ] **5.1 Overlay audit.**
  Search every `osPiStartDma`/`__osPiRawStartDma` call site for destinations in
  executable ranges, and every `LOOKUP_FUNC` in the corpus. If the game loads
  code overlays, add the sections and relocations to the symbols TOML and
  register them (`recomp_overlays.inl` already has the table shape). If it
  does not, record that as a verified fact.
  *Accept:* a written answer ("no overlays" or the list of sections) backed by
  call sites, plus a successful overlay load if they exist.

- [ ] **5.2 Function-boundary audit.**
  The map is Ghidra-only. Add `tools/verify_symbols.py` that, for every
  function:
  - disassembles the last word and requires a terminator (`jr ra`, `j`, `b`,
    `break`) or a known fallthrough target;
  - checks that every `jal`/`j` target inside the section is a function start;
  - reports functions whose body overlaps another or whose start is not
    word-aligned/interior to another.
  *Accept:* a report with zero unexplained violations, or an allow-list of
  known interior entries documented here.

- [ ] **5.3 Interior-entry handling.**
  Any `jal` to an address that is not a function start is either a tail call
  or an interior entry. N64Recomp will emit the wrong dispatch row for the
  latter (the exact class that cost the ToL project many sessions). Add the
  address to the symbols map as its own function, or add an override in
  `scars_overrides.cpp`.
  *Accept:* every `jal` target resolves to a function start or a documented
  override.

- [ ] **5.4 Determine and implement the save type.**
  Inspect calls to `osEeprom*`, `osSram*`, `osFlash*`, and `osPfs*`. The ROM
  imports Controller Pak (`osPfs*`) and rumble (`osMotor*`) functions and
  contains `Controller Pak` strings; cartridge save type is still unproven.
  Set `GameEntry.save_type` accordingly and implement the memory-card path /
  rumble if required.
  *Accept:* saving and loading a game does not hang, and the save file format
  matches what the game expects.

- [ ] **5.5 Data symbols (mod/hook support).**
  n64sym found more than 400 data/interior symbols (`osTvType`,
  `__OSGlobalIntMask`, `gu*` tables, etc.). Export the data symbols into a
  side table so future hooks and mods can name globals. This is not needed to
  boot but is needed for the modding layer.
  *Accept:* a `build/symbols/scars.datasyms.toml` consumed by the hook layer.

- [ ] **5.6 Input, audio mix, and timing.**
  Controller mapping, deadzone, rumble, audio buffering, frame pacing. Use the
  emulator reference for VI cadence.
  *Accept:* the game is playable at the intended 50 Hz PAL cadence with
  working controls and audio.

- [ ] **5.7 Packaging and Switch target.**
  Version stamping, license notices, runtime DLL staging, and the libnx build
  (the repo README already promises PC + Switch).
  *Accept:* a clean release folder runs on a machine with no toolchain.

---

## DEFINITION OF DONE

"Fully working" means all of the following, in this order:

- the game boots through the logos to the title/menu;
- a race starts and renders at the PAL cadence;
- controls, audio, and saves/Controller Pak work;
- the link has no unresolved `_recomp`/`os*` symbols;
- the overlay and function-boundary audits are clean or their exceptions are
  documented here;
- a release build runs from a clean folder on a machine with no toolchain.

---

## OPEN QUESTIONS

1. Does S.C.A.R.S load code overlays at runtime? (Phase 5.1)
2. Which graphics microcode does it use, and where is its text in ROM?
   (Phase 3.1)
3. Which audio microcode, and what AI frequency does the game select?
   (Phase 3.3)
4. Cartridge save type: EEPROM, SRAM, FlashRAM, Controller Pak, or none?
   (Phase 5.4)
5. Does the game use an Expansion Pak? BSS tops out at `0x803FFF20`, which
   implies 4 MiB; confirm by looking for `osGetMemSize` users and any 8 MiB
   allocations.
6. Are there merged/split function boundaries in the game code (not just
   libultra) that will cause interior-entry bugs? (Phase 5.2/5.3)

---

## TRAPS LEARNED (do not repeat)

- **Ghidra project paths cannot contain `+`.** The repo lives under
  `D:\Personal\C++\...`, so `generate_aot.ps1` puts the Ghidra project in
  `%TEMP%\scars-ghidra`. Do not "simplify" that path into the repo.
- **Ghidra needs a seed before analysis.** `ScarsSeed.java` runs as a
  `-preScript`; the post script exports. Without the seed, function discovery
  starts from nothing.
- **The entry function is not 0x40 bytes.** `0x8020043C` is data. The merge
  step clamps it to `0x3C`; if you hand-edit the TOML and lose that, N64Recomp
  fails with `Unhandled branch ... to 0x803C0000`.
- **n64sym v0.9a does not accept `-f splat`.** Use `-f default` and parse
  `ADDRESS NAME` lines (`tools/merge_symbols.py` does).
- **Never use a raw Ghidra export directly.** Without the n64sym merge,
  N64Recomp hits `cache` (0x80274FB0/0x80275110) and `mfc0`/`mtc0` in the
  libultra cache/interrupt/TLB functions. Those functions must be named so
  N64Recomp replaces them. `tools/merge_symbols.py` also carries two manual
  names n64sym misses: `osUnmapTLBAll` (0x802750C0) and
  `osWritebackDCacheAll` (0x80275110).
- **The symbols TOML is generated output.** Fix naming/sizes in
  `tools/merge_symbols.py` (or the Ghidra exporter), not by editing it.
  Regeneration overwrites it.
- **MSVC rejects upstream's `-Wno-unused-parameter`** in `ultramodern`.
  `cmake/ScarsRuntime.cmake` strips it for cl.exe only.
- **`recompile` != `generate-aot`.** `recompile` runs N64Recomp against the
  existing symbols TOML. `generate-aot` runs Ghidra + n64sym + N64Recomp.
- **The runtime ROM hash is XXH3-64 over the whole file.** Do not use SHA for
  `GameEntry.rom_hash`.
- **The corpus is not pulled by the linker until referenced.** A green build
  today does not mean the runtime symbols exist. See Phase 0.1.
- **Licensing:** N64ModernRuntime carries GPL-3.0. A distributed binary that
  links it must comply with that license; the original code in this
  repository is MIT.

---

## SESSION LOG

### Session 1 - 2026-11-25 - scaffold, AOT pipeline, first corpus

Done:

- Mirrored the ToL layout: `thirdparty/n64recomp` (fork) and
  `thirdparty/N64ModernRuntime`; CMake presets; `scars` runtime scaffold.
- Implemented the AOT pipeline (`tools/generate_aot.ps1`,
  `tools/ghidra/ScarsSeed.java`, `tools/ghidra/ScarsExportSymbols.java`,
  `tools/merge_symbols.py`).
- Ran it on the local Europe dump: 841 functions, 296 named, 15 `funcs_*.c`.
- Fixed the pipeline failures: entry size 0x3C, cache functions must be named,
  `osUnmapTLBAll`/`osWritebackDCacheAll` manual names, MSVC flag stripping.
- Verified the corpus compiles and links into `scars.exe` with MSVC and
  clang-cl.
- Added `tools/rom_info.py`; recorded the ROM hashes and memory layout above.

Measured / decided:

- ROM hash for `GameEntry` is XXH3-64 `0x4C035BA764A51971`.
- Boot image maps ROM `0x1000` to `0x80200400`; BSS starts `0x802B6050`.
- `lookup.cpp` `get_rom_name()` is wrong in symbols mode (`.z64`).
- The corpus is not yet link-closed; no runtime boot attempted.

Next session starts at Phase 0.1.
