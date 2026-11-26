# S.C.A.R.S (Nintendo 64) - native static recompilation (state)

> Started 2026-11-25 (session 1). This file is the live state document.
> Append a session entry at the bottom after each work block; do not rewrite
> earlier entries when a finding is superseded - add a correction in the new
> entry, the way the PS2 project state does.
>
> Inputs (ROM, ELF, symbols, generated corpus) are local only and gitignored.
> Nothing in this file should be read as permission to redistribute them.

---

## CURRENT STATUS - updated 2026-11-25 (session 2; read this first)

> **Session 2 supersedes the table below.** Phase 0 is closed, Phase 1 boots,
> and the game reaches its steady state. Read the session 2 entry at the bottom
> for the current status, the fork changes and the next action (RT64 + SDL2).

**Goal:** boot the Europe release of S.C.A.R.S natively and render it in a
window, using static recompilation (N64Recomp) + N64ModernRuntime, the same
architecture as Zelda 64: Recompiled and the Diddy Kong Racing recomp.

**One-line status (session 1, kept for history):** the toolchain and the AOT corpus work end to end - the
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

---

### Session 2 - 2026-11-25 - Phase 0 closed, Phase 1 boots (first full boot)

Status: **Phase 0 done, Phase 1 done.** `scars.exe` (Windows, MSVC, RelWithDebInfo)
validates the ROM, boots the generated entry point, and the game reaches its
steady state: four guest threads, VI retraces swapping between two framebuffers
(`origin 0x001B5188` / `0x001DA988`, `width 0x140`), F3DEX2 graphics tasks and AI
audio tasks every frame. Nothing is rendered and nothing is audible: the RSP
callbacks are still a tracing stub that reports every task as finished.

#### What Phase 0 cost, and why

**0.1 Forced reference - 22 unresolved symbols.** Forcing `recomp_entrypoint`
produced exactly the finite list the plan predicted (all libultra internals that
`n64recomp`'s `ignored_funcs` drops and N64ModernRuntime does not implement):
`osDpGetCounters`, `osPfsIsPlug`, `__osPopThread`, `__osEnqueueThread`,
`__osPfsSelectBank`, `__osContRamRead`, `__osContRamWrite`, `__osCheckId`,
`__osPfsRWInode`, `__osSiGetAccess`, `__osSiRelAccess`, `__osCheckPackId`,
`__osPfsRequestOneChannel`, `__osSiRawStartDma`, `__osPfsGetOneChannelData`,
`__osLeoAbnormalResume`, `__osViGetCurrentContext`, `__osViSwapContext`,
`__osTimerInterrupt`, `__osPackRequestData`, `__osContGetInitData`,
`__osSiCreateAccessQueue`.

They now live in `src/runtime/ultra_stubs_scars.cpp`, split three ways:
- exact runtime equivalents: `__osPopThread`/`__osEnqueueThread` forward to
  `ultramodern::thread_queue_pop/insert`;
- state the runtime already owns: `__osContGetInitData` calls the public
  `osContInit`, so the real device table is used;
- hardware the runtime does not emulate (Controller Pak, 64DD LEO, raw SI DMA,
  DP counters): return the caller's error code (`PFS_ERR_NOPACK`) or zero, log
  once, so a trace shows if the path is reached. VI/timer/SI-access internals are
  no-ops because the guest VI manager is never started.

**0.2 also exposed a real duplicate-symbol bug**: `__divdi3`, `__udivdi3` and
`__umoddi3` were in `renamed_funcs` (translated *and* renamed), while
`librecomp/src/math_routines.cpp` defines the same `_recomp` names. Adding the
three to `reimplemented_funcs` in our fork resolves it the way upstream already
handles `__ull_div` and friends.

**0.3 `get_rom_name()`** now uses `rom_file_path` when `elf_path` is empty;
`lookup.cpp` returns `"scars.z64"`.

#### Phase 1 - five bugs between "entry point called" and "game runs"

Each was found with a stack, not by reading code. `tools/minidbg` (added this
session) is a minimal out-of-process debugger: fail-fast exceptions bypass every
in-process handler, and the in-process `src/runtime/crash_handler.cpp` cannot see
them.

1. **The section table was never registered.** `recomp_overlays.inl`
   defines `section_table` but nothing called `recomp::overlays::register_overlays`,
   so `init_overlays()` saw zero sections and `load_overlays()` dereferenced
   null. Fixed by `src/runtime/scars_overlays.cpp` (which also gives the linker
   the complete function table, the strongest pull on the corpus).
2. **`GameEntry::entrypoint_address` must be sign-extended.** Guest addresses are
   `(gpr)(int32_t)`; passing `0x80200400` unsigned makes `MEM_*`'s
   `- 0xFFFFFFFF80000000` wrap to a +2 GiB offset. The generated `lookup.cpp`
   casts through `int32_t` for exactly this reason. Symptom: `do_rom_read` wrote
   4 GB past RDRAM.
3. **The boot thread never terminates.** `osStartThread` on the entry-point
   thread returns in N64ModernRuntime (the terminating
   `throw thread_terminated` is commented out), so the guest boot stub continues
   into its terminal `break 1,2` -> `do_break` -> exit. Uncommented the throw;
   `wait_for_game_started` already catches it.
4. **`osTvType` must be 0 (PAL) for this ROM.** `librecomp` hardcodes 1 (NTSC).
   With NTSC the game's main thread takes its non-PAL branch and *returns
   immediately*, leaving no runnable threads. Fixed through
   `GameEntry::on_init_callback` (no submodule change).
5. **Eight translated functions touched hardware registers.** n64sym leaves
   libultra's MMIO functions unnamed, so N64Recomp translated them and the
   generated code dereferenced `0xA4xxxxxx` as RDRAM (offset ~0x24xxxxxx, inside
   the `PAGE_NOACCESS` guard). Identified by their register accesses and added to
   `tools/merge_symbols.py`: `osAiSetFrequency` (`0x8027FE40`),
   `osAiSetNextBuffer` (`0x8027FF70`), `__osAiDeviceBusy` (`0x80280010`),
   `osDpSetStatus` (`0x80280030`), `osCreatePiManager` (`0x80284A10`),
   `osCartRomInit` (`0x80284BB0`), `osContInit` (`0x80281450` - n64sym names every
   other function in continit.c but not this one, and the translated raw SI
   protocol deadlocks). All are names N64Recomp already replaces.

#### Remaining two runtime-level gaps (both handled)

- **Inline MMIO in game code.** The music driver (`FUN_80238144`) reads
  `AI_LEN (0xA4500004)` directly instead of calling `osAiGetLength`, so the value
  must be real. `src/runtime/guest_mmio.cpp` maps the register windows
  (`0xA4000000..0xA5000000` at RDRAM offset `0x24000000`, the KSEG1 cart-ROM alias
  `0xB0000000` at `0x30000000`, preloaded with the first 1 MiB of the ROM), and
  `src/runtime/guest_hw.cpp` models the AI FIFO after **Kyujo's**
  `src/ultra/hw_ai.c`: playback rate = sample rate the game selected (22050 Hz
  here), AI_LEN reads back bytes still to play, AI_STATUS bit 30 = DMA busy,
  refreshed from the VI callback and on every queued buffer.
- **All guest threads blocked at once is not a deadlock.** S.C.A.R.S parks main
  (VI), graphics (SP) and audio (AI) simultaneously. `run_next_thread_and_wait`
  threw `"No threads left to run!"`; it now waits for the next external event
  message, i.e. the CPU idles until an interrupt, which is what hardware does.

#### Verified facts added this session

| Fact | Value |
|---|---|
| Graphics ucode | **F3DEX2 family**; task `ucode=0x8026F5C0`, `ucode_data=0x802AD218`; display lists open with `DB060000` (`G_MOVEWORD`, F3DEX2 encoding), then `DB060004`, `DB060008` |
| Audio ucode | task `type=2`, `ucode=0x80271BB0`, `ucode_data=0x802ADA28`, ~1984 samples per buffer |
| AI sample rate | 22050 Hz set by the game |
| Framebuffers | two, `0x001B5188` and `0x001DA988` (VI origin), `width 0x140`, status `0x00003112` |
| Guest threads | ids 1 (boot/main), 3, and two 4s (audio + graphics); stacks `0x802B6590`, `0x802B8788`, `0x802B65D8`, `0x802BB420` |
| Controller | the game runs its own `osContInit`? **No** - after naming `0x80281450`, the runtime's `osContInit` is used |
| Save type | still undetermined; the boot reaches Controller Pak code only through the stubs |

#### Fork / tooling changes (all in this repo)

`thirdparty/n64recomp`: `lookup.cpp` ROM name; `__divdi3`/`__udivdi3`/`__umoddi3`
moved to `reimplemented_funcs`.
`thirdparty/N64ModernRuntime`: `osStartThread` termination throw restored;
`run_next_thread_and_wait` idles on external messages.
`tools/merge_symbols.py`: the eight manual names above.
New runtime files: `main.cpp` (boot harness), `scars_game.*`, `scars_overlays.cpp`,
`scars_log.*`, `crash_handler.*`, `guest_mmio.*`, `guest_hw.*`, `trace_backend.*`,
`ultra_stubs_scars.cpp`. New tool: `tools/minidbg`.

#### References adopted (user direction)

- **`D:\Personal\C++\dkr-n64recomp`** is the renderer reference for **PC and
  Switch**: RT64 as a static dependency with the SDL/Vulkan window
  (`RT64_STATIC`, `RT64_SDL_WINDOW_VULKAN`), SDL2 for the window/input, and on
  Switch **NXVK** (Mesa NVK ported to Switch) as the Vulkan driver, wired through
  `runtime-recomp/cmake/SwitchPlatform.cmake`, `cmake/switch/FindNXVK.cmake` and a
  devkitA64/libnx toolchain that keeps exceptions and RTTI enabled. It also keeps
  RT64 pristine: changes are checksum-pinned patches.
- **`D:\Personal\C++\Kyujo`** (GPL-3.0) is the hardware-behaviour reference:
  `src/ultra/hw_ai.c` (the AI model used here), `hw_regs.c` (VI/PI/MI register
  semantics), `hw_sp.c`, `src/render/f3dex2.c`, `src/rsp/rsp_cpu.c`,
  `src/render/rdp.c`, and `shell/sdl/main_sdl.c` (SDL2 shell for PC and Switch).
  Copying any of it makes the distributed binary GPL-3.0.

#### Next action

Phase 2: SDL2 window + RT64 on PC, following DKR's RT64 wiring (window handle is
already a `WindowHandle` in `recomp::Configuration`). Then Phase 3: RSPRecomp for
the F3DEX2 ucode at `0x8026F5C0` and the audio ucode at `0x80271BB0`; the
tracing renderer and `get_trace_rsp_microcode` are the two seams to replace.

---

### Session 3 - 2026-11-25 - Phase 2: PC window + RT64 renders the game

Status: **the game renders on PC.** A 960x720 SDL2 window opens, RT64 initialises
(D3D12 here, graphics API 1), and the first frames show the Ubi Soft
Entertainment boot logo. Evidence: `%TEMP%\scars-window2.png` captured with
`PrintWindow(PW_RENDERFULLCONTENT)` from the live process.

#### What was wrong with "scars.exe disappears immediately"

Double-clicking ran the exe with its own directory as the working directory, so
`data/scars.z64` was not found, the ROM error printed, and the console closed
with the process. Fixed in `main.cpp`:
- the ROM is searched next to the exe and up to five parent directories
  (`build/windows-x64-release/bin` finds the repo's `data/`), after `argv[1]` and
  `SCARS_ROM`;
- the default user directory is `<exe>/user` (Explorer's cwd is not a place to
  write saves);
- every log line is mirrored to `<exe>/scars-log.txt`, and a fatal startup error
  waits for Enter when stdin is a console.

#### Rendering stack (reference: dkr-n64recomp, as directed)

| Piece | Choice |
|---|---|
| Renderer | RT64, unmodified (MIT), `RT64_STATIC=TRUE`; wires through `cmake/ScarsRt64.cmake` |
| Window | SDL2 2.26.3 bundled with RT64's mupen64plus deps; `SDL_MAIN_HANDLED`, console subsystem, plain `main()` |
| Window owner | the `recomp::start` thread; `WindowHandle{ HWND, GetCurrentThreadId() }` is passed to RT64's `setup(thread_id)` |
| Graphics ucode | **RT64's own HLE F3DEX2 decoder** - `GBIUCode` family 7, recognised by hashing the ucode text/data in RDRAM |
| Input | SDL game controllers (ports 0-3) plus a keyboard fallback on port 1 |
| RT64 includes | the consumer must repeat RT64's directory-scope defines/includes: `HLSL_CPU`, `FFX_GCC`, `NOMINMAX`, `src/contrib/hlslpp/include`, `Unknwn.h`/`oaidl.h` before the headers |

RT64 is gitignored and fetched with `tools/fetch_rt64.ps1` (copies a local
checkout or clones upstream). `-DSCARS_RT64_DIR=` points at any checkout.

#### The two mistakes that cost the first frames

- **RT64 indexes RDRAM by physical offset.** `processDisplayLists(memory, start,
  end, isHLE)` wants `0x003314F0`, not the guest's kseg0 `0x803314F0`; passing the
  virtual address walked the decoder off into unmapped memory. Mask with
  `0x1FFFFFFF`, for `ucode`/`ucode_data` too.
- **`0x406D1388` is `MS_VC_EXCEPTION`**, raised by the display driver purely to
  name a thread. The crash handler now ignores it (as it already ignored MSVC's
  `0xE06D7363`), otherwise every run "crashed" a few seconds in.

#### Consequence for Phase 3

Graphics microcode needs **no RSP translation unit**: RT64's GBI database already
knows this game's F3DEX2, and `send_dl` passes the display list to
`Application::processDisplayLists(..., isHLE=true)`. Only the **audio** ucode
(`0x80271BB0`, task type 2) still needs RSPRecomp - or an HLE audio implementation.
The `get_trace_rsp_microcode` seam in `trace_backend.cpp` is now graphics-free but
still returns `Broke` for audio tasks, which is why the game runs without sound.

#### Next action

Switch preset: devkitA64 + libnx, RT64 with `RT64_SDL_WINDOW_VULKAN` and the NXVK
Vulkan portlib, following dkr-n64recomp's `SwitchPlatform.cmake`,
`cmake/switch/FindSDL2.cmake`, `FindNXVK.cmake` and its NRO packaging helpers.
`ultramodern`'s `renderer_context.hpp` needs a `NintendoSwitch` branch for
`WindowHandle` (SDL_Window*), since it currently only defines Windows/Linux/
Android/Apple.

---

### Session 3 (cont.) - Nintendo Switch preset

Added, using dkr-n64recomp's Switch stack as the reference:

| File | Purpose |
|---|---|
| `cmake/switch/SwitchToolchain.cmake` | devkitA64/libnx, devkitPro's `Switch.cmake`, `SCARS_SWITCH` marker, NACP fields |
| `cmake/SwitchPlatform.cmake` | exceptions + RTTI compile checks, `_GNU_SOURCE`, Switch module path, `find_package(SDL2/NXVK)` |
| `cmake/switch/FindSDL2.cmake` | switch-sdl2 from portlibs (no pkg-config execution: it is a bash wrapper on Windows) |
| `cmake/switch/FindNXVK.cmake` | parses `nxvk.pc` into an imported `nxvk::nxvk` target |
| `cmake/ScarsRt64.cmake` | Switch also defines `PLUME_SDL_VULKAN_ENABLED`/`RT64_SDL_WINDOW_VULKAN`, because RT64's own CMake only does that for `CMAKE_SYSTEM_NAME == Linux` |
| `CMakePresets.json` | `switch` configure/build presets |
| `cmake/switch/app.nacp.template` + root `CMakeLists.txt` | POST_BUILD `nacptool` + `elf2nro` produce `build/switch/scars.nro` |
| `build-switch.sh`, `docs/SWITCH.md` | build entry point and prerequisites |
| `ultramodern/renderer_context.hpp` | `__SWITCH__` added to the SDL_Window* `WindowHandle` branch (fork change) |

Verified from Windows: `cmake --preset switch` reaches the devkitA64 compiler
check, finds switch-sdl2, and stops exactly at the missing **NXVK** portlib with
the intended message. Two host facts to remember:

- **NXVK must be installed** (`${DEVKITPRO}/portlibs/switch`, `nxvk.pc` +
  headers + libvulkan). RT64 has no Vulkan driver of its own.
- **Build the NRO from Linux/WSL.** RT64 compiles shaders with DXC at build time
  and picks the DXC binary by *target* platform, so a non-Windows target wants
  `dxc-linux`; a Windows host cannot run it. dkr-n64recomp builds from WSL for
  the same reason.

---

### Session 3 (cont.) - the game goes past the boot logo

Reported: the window opened and showed the boot logo, then nothing moved.
Investigated with `minidbg --dump-every` (all-thread stacks) plus a new
`SCARS_QUEUES=<hex,...>` queue inspector on the status line.

**Diagnosis.** Every guest thread was parked on a message queue at the same
time, and the messages that would wake them were sitting in the runtime's host
queue undelivered:

- guest thread 3 (game main loop, `FUN_802118f0`) waits on the VI queue
  `0x803FF970` (`osViSetEvent`), inside `do_recv`;
- one thread waits on the SP queue `0x803C93C8` (`osSetEventMesg(4)`), which
  *already held* its message (valid=1, no waiter);
- the SI queue `0x803E3300` likewise held a message nobody consumed.

`enqueue_external_message_src` only queues host-side; the guest transfers
messages in `osRecvMesg`/`osSendMesg`. With every guest thread blocked, nothing
ran, so nothing ever transferred them - a livelock, not a deadlock in the guest
logic.

**What fixed it** (all in `ultramodern`, defensive and behaviour-preserving):

1. `thread_queue_pop` returns NULLPTR on an empty queue instead of
   dereferencing null (the pop could race with another parked thread).
2. `run_next_thread` returns when nothing is runnable instead of throwing
   `"No threads left to run!"` (which terminated the process).
3. `thread_queue_insert` is idempotent: `do_recv` re-registers on every wake-up,
   and a duplicate entry made a *running* thread get popped again later.
4. `check_running_queue`, `do_send` and `do_recv` guard their pops.

`run_next_thread_and_wait` keeps the "if nothing is runnable, wait for an event"
form (upstream threw there). A more thorough rewrite - giving the runtime an
explicit "the emulated CPU is idle" state so delivery can wake a parked thread
directly - was prototyped and reverted: it needs the guest execution model
reworked, and the defensive set above already unblocks this game.

**Verified.** 60-second run: the Ubi Soft splash gives way to the S.C.A.R.S
copyright screen, then the **LANGUAGE SELECT** menu (English/Francais/Deutsch,
"A ACCEPT") with its animated wireframe background. ~1060 audio tasks and ~1060
VI retraces in 20 s; `ai_len` reads back 2152-2200 bytes, i.e. the AI FIFO model
is pacing the music driver. Screenshots: `%TEMP%\scars-t30.png`,
`%TEMP%\scars-t55.png`, `%TEMP%\scars-now.png`.

**Still open**

- The menu waits for A. Keyboard Z is mapped to A (`sdl_frontend.cpp`), but a
  key posted to an unfocused window did not advance the menu, so input needs a
  focus check (Phase 5.6). No input is a correct reason to sit on this screen.
- Audio is silent: the audio ucode (`0x80271BB0`) is still the tracing stub, so
  the buffers are queued with whatever the CPU left in them.
- PAL pacing: VI still runs at 60 Hz for a 50 Hz title.

**New diagnostics worth keeping**

- `SCARS_QUEUES=803C93C8,803FF970,...` (hex) makes the status line dump
  `OSMesgQueue` state: valid/msgCount, waiter count and thread ids.
- `minidbg --dump-every <seconds> <exe>` prints every thread's symbolized stack
  on that interval while the process runs.

---

### Session 3 (cont.) - save type determined: Controller Pak

User input carried the game past LANGUAGE SELECT (input path verified end to end:
SDL keyboard/gamepad -> `osContGetReadData` -> game). The next screen is the save
probe:

```
NO CONTROLLER PAK IN CONTROLLER 1
  CONTINUE WITHOUT SAVE
  RETRY
(A) ACCEPT
```

**Answer to open question 4 (save type): Controller Pak.** The cartridge has no
EEPROM/SRAM/Flash path in use; the game saves to a Controller Pak in port 1 and
degrades cleanly when none is present ("CONTINUE WITHOUT SAVE").

**Why our stubs are the cause.** The game does *not* call the replaced
`osPfsInitPak` API - the corpus calls only `osPfsFindFile`, `osPfsAllocateFile`,
`osPfsDeleteFile`, `osPfsFreeBlocks`, `osPfsChecker`, `osPfsRepairId`,
`osPfsIsPlug` and `osMotorInit`. Its PFS implementation is its own translated
libultra code (`contpfs_text_0078`, `FUN_80282e10`, `FUN_802843e0`, ...), which
calls the low-level primitives this port currently stubs as "no pak":
`__osPfsSelectBank`, `__osPfsRWInode`, `__osCheckId`, `__osContRamRead/Write`,
`__osPfsRequestOneChannel`, `__osPfsGetOneChannelData`, `__osPackRequestData`,
`__osSiRawStartDma`.

**Implementation plan** (Phase 5.4): emulate the pak at the SI/Joybus layer so the
game's own PFS code works unchanged. `__osSiRawStartDma` is the single choke
point: on the CPU->PIF block copy record the 64-byte request, on the PIF->CPU copy
execute it and fill the answers, then post the SI completion message. Commands
needed (n64brew Joybus, as implemented in Kyujo's `src/ultra/hw_pif.c`):
- `0x00` info: answer `05 00 01` (standard controller, pak present);
- `0x02` read pak: 32 bytes at a 32-byte aligned address + CRC-8/0x85;
- `0x03` write pak: 32 bytes + data CRC, answered CRC;
- address checksum: XOR table over bits 15..5, 5 bits in the low byte.
The pak is 32 KiB of host-backed storage (`<user>/SCARS.pak`), which the game
formats itself with `osPfsInitPak`.
Reference: `D:\Personal\C++\Kyujo\src\ultra\hw_pif.c` (protocol, checksums, the
"commands run on the read DMA" subtlety, and the ares-compatible CRC rejection
behaviour). The game exposes `__osContAddressCrc`/`__osContDataCrc` in its own
translated code, so those CRCs must match libultra exactly.

**Interim:** "CONTINUE WITHOUT SAVE" reaches the title/menus, which is what the
next phase needs; saving stays unimplemented until the SI pak layer lands.

---

### Session 3 (cont.) - Controller Pak storage layer (caveat resolved)

**The CRC caveat is void.** `__osContAddressCrc` and `__osContDataCrc` are named in
the symbols file but the corpus has **zero call sites** for either (checked across
all 15 `funcs_*.c`). The game never computes a Joybus CRC itself: its translated
contpfs code calls the replaced primitives, so the checksum layer lives on our
side of the boundary and there is nothing to match byte-for-byte.

**What was added** (`src/runtime/guest_pak.cpp`, `guest_pak.h`):

| Symbol | Behaviour |
|---|---|
| `__osContRamRead` / `__osContRamWrite` | 32-byte block copy to/from a 32 KiB image at `<exe>/user/SCARS.pak` |
| `__osPfsSelectBank`, `__osCheckId` | succeed; the game's own code validates the header it reads |
| `__osPfsRWInode` | one 256-byte inode page; writes mirror into `minode_table` |
| `__osPackRequestData` | zero-fills the unused SI request block |
| `__osSiRawStartDma` | posts the SI completion (the storage layer answers directly) |
| `osPfsIsPlug` | reports a pak in port 1 when enabled |
| `__osPfsRequestOneChannel` / `__osPfsGetOneChannelData` | status answer `05 00 01` (pak) / `05 00 02` (none) for the probed port |

Gated by `SCARS_CONTROLLER_PAK=1` and **off by default**: with the layer enabled
the game still refuses the pak, because its next calls go to the *public* API
(`osPfsFindFile`, `osPfsAllocateFile`, `osPfsChecker`, `osPfsRepairId`), which is
still librecomp's silent `PFS_ERR_NOPACK` stub. Reporting a pak therefore turns a
clean "CONTINUE WITHOUT SAVE" into a save error, so the default keeps the previous
behaviour.

**Also fixed: the guest-scheduler stall.** Runs now survive indefinitely instead of
parking after a few seconds. `do_recv`/`do_send` drain host event messages on
every iteration of their blocking loop rather than only when a guest thread
happens to run, which was the remaining lost-wake-up window. Three consecutive
22 s runs: ~700 VI retraces and ~700 audio tasks each, no stall.

**Next for saving** (Phase 5.4): implement the public PFS API over the same image,
matching libultra's on-disk layout so the game's own contpfs code and our API agree:

- header page 0: `id`/`label`/`version`/`dir_size`/`inode_table`/`minode_table`/
  `dir_table`/`inode_start_page`, with `__osIdCheckSum`/`__osSumcalc` checksums
  (both translated in the corpus, so the game can verify what we write);
- `__OSInodePage` chain (128 two-byte entries per page) and `__OSDirEntry`
  (32 bytes: file type/number, company code, game code, ext name, game name,
  ext length, blocks used, timestamp);
- then `osPfsAllocateFile`/`FindFile`/`ReadWriteFile`/`DeleteFile`/`FileState`/
  `FreeBlocks` on top.

Until that lands, the game must be driven with "CONTINUE WITHOUT SAVE".

#### pak=1 hangs: black screen, all threads parked

Reported by the user: `SCARS_CONTROLLER_PAK=1` gives a black screen. Reproduced under
`minidbg --dump-every 8` with `SCARS_AUTO_CONFIRM=1`: the pak image is created
(`osPfsIsPlug` ran), then every guest thread parks in `do_recv` and the game stops
submitting graphics/audio work - the VI thread keeps retracing, the display shows
the cleared framebuffer.

Stacks at the hang: thread 1 in `pause_self` -> `wait_for_external_message`;
threads 3/4 in `do_recv` -> `wait_for_resumed`. All four waiting for messages that
are sitting in the host queue.

So the pak storage layer is not the failure: reporting a pak merely moves the game
onto a path that parks every thread at once, which exposes the runtime's remaining
scheduler gap (a parked thread cannot be woken by a host delivery; see the session 3
note above). With the pak reported as absent the game always keeps a thread that
polls, so it does not hit this.

**Conclusion:** keep `SCARS_CONTROLLER_PAK` off (the default) and use "CONTINUE
WITHOUT SAVE". Saving needs both, in this order:

1. the delivery fix: when `enqueue_external_message_src` targets a queue with a
   blocked thread and no guest thread is executing, wake that thread (or let the
   parked thread pump messages without breaking the one-thread-at-a-time model);
2. the public PFS API over the same image (layout notes above).

---

### Session 4 - 2026-11-26 - audio ucode: identified, recompiled, wired; ABI input still hangs

Goal was to finish the audio microcode. Progress is real but **the ucode does not
run a task yet**; it is gated behind `SCARS_RECOMP_AUDIO=1` and the default build
still uses the tracing stub, so the game behaves as before (video, no sound).

#### Identified and recompiled

The runtime task says everything needed: `type=M_AUDTASK`, `ucode=0x80271BB0`
(size `0x1000`), `ucode_data=0x802ADA28` (size `0x2C0`). With ROM 0x1000 -> VRAM
0x80200400 that is ROM `0x727B0` (text) and `0xAE628` (data). The text opens with
`addi $24,$zero,0x360` / `addi $23,$zero,0xF90` (aspMain's prologue) and the data
holds the 16-entry ABI handler table at offset 0x10:

```
0x1118 0x1470 0x11DC 0x1B38 0x1214 0x187C 0x1254 0x12D0
0x12EC 0x1328 0x140C 0x1294 0x1E24 0x138C 0x170C 0x144C
```

`recomp/rsp/aspMain.toml` + the `generate-rsp` target produce
`build/generated_rsp/aspMain.cpp` (`scarsAspMain`, 99 labels). The CMake side
compiles it when present (`SCARS_HAVE_RECOMP_ASPMAIN`), and
`get_trace_rsp_microcode` returns it for `M_AUDTASK`.

#### Three real bugs found and fixed on the way

1. **RSPRecomp address space.** Instruction addresses are link-space (0x1000+)
   while `jal/beq` immediates in this ucode are physical (0x000-0xFFF), so direct
   branch labels came out undefined. The tool already normalises *indirect*
   targets with `| 0x1000`; the same is now applied to direct targets
   (`get_branch_targets` and the per-instruction target). Plus `text_address =
   0x1000` so the 4 KiB blob covers the whole IMEM image.
2. **`osSpTaskLoad` was a no-op.** libultra converts every OSTask address to
   physical (the RSP's DMA engine is physical) on a *private* copy, and
   `osSpTaskStartGo` starts that copy (which is why it takes no argument). Doing
   it in place on the caller's struct broke the audio driver (it stopped after
   8 tasks); a private buffer at guest 0x80880000 fixes both.
3. **`recomp::rsp::run_task` copied 0xF80 bytes of ucode data** regardless of
   `ucode_data_size` (0x2C0 here), spilling into the ABI's I/O buffers at 0x360.
   It now uses the task's size, and preloads the ACMD list into DMEM 0x380
   (capped at 0xC00 so the 4 KiB ring cannot wrap over the ucode data).

After those, DMEM holds the correct handler table and the real ACMD stream
(`07000000 00000000 02000440 ...`).

#### Where it still fails

With `SCARS_RECOMP_AUDIO=1` the first audio task never returns; the SP Task
Thread sits inside `scarsAspMain` and no further audio task is submitted.

Measured facts for the next attempt:
- the dispatcher reads commands from DMEM 0x380 upwards, 8 bytes per step, with
  `r30 = data_size` (`0x1790`) as the remaining-size counter;
- the ABI input ring is therefore at most 0x380..0xF8F (`0xC10`), smaller than
  the list, so aspMain must be intended to consume it in chunks (the first
  handler, 0x1118, is the DMA-read helper) - our single 0xC00 preload is not the
  chunking the microcode expects;
- entry registers set `r24 = 0x360` (output buffer) and `r23 = 0xF90` (input end),
  which is the pair that should bound the refill logic.

Next steps, in order: decode the disarm/refill path of the 0x1064 dispatcher
against `r23`/`r24`, and check whether the ABI expects the *caller* to write the
first chunk length somewhere (e.g. DMEM 0x370 or the task yield buffer) instead
of a full preload.

The rest of the session's audio plumbing is sound and independent of that: the
ucode is compiled and selected, the task addresses are physical, and DMEM setup
matches libultra's.

#### aspMain dispatcher decoded - and why recompiling it is the wrong tool

Disassembly of the ucode text (base 0x1000, addresses are link-space) settled the
control flow:

```
0x1064  lw   r26,0x0(r29)      ; command word
0x1068  lw   r25,0x4(r29)
0x106C  srl  r1,r26,23
0x1070  andi r1,r1,0xFE       ; index = opcode*2
0x1074  addi r28,8            ; data_ptr += 8
0x1078  addi r27,-8           ; remaining -= 8
0x107C  addi r29,8            ; DMEM cursor += 8
0x1080  addi r30,-8           ; chunk remaining -= 8
0x1084  add  r2,r0,r1
0x1088  lh   r2,0x10(r2)      ; handler from the table in aspMainData
0x108C  jr   r2               ; NOTE: jump, not call - ra is not set
0x1090  nop
0x1094  break
0x1098  bgtz r30,0x1064       ; more in this chunk
0x10A0  blez r27,0x10B8       ; nothing left -> signal done
0x10A8  jal  0x1150           ; flush the output buffer
0x10B0  j    0x10D4           ; refill
0x10B8  ori  r1,0x4000 ; mtc0 SP_STATUS ; break   (Broke)
0x10D4  r2 = r28 ; r3 = r27 ; r4 = r3-0x140
0x10E0  blez r4 -> 0x10EC else r1=0x380, r3=0x140
0x10EC  r30 = r3              ; chunk size
0x10F0  jal  0x1184           ; DMA r3-1 bytes from r2 to DMEM 0x380
0x10F8  r29 = 0x380
0x10FC  jr   a1               ; returns to the dispatcher via a1 (r5)
```

Two conventions break the recompiled path, and neither is a bug in our wiring:

1. **`jr r2` without setting `ra`.** Handlers such as ACMD 0x07 (`0x12D0: nop;
   j 0x1118`) tail-jump into the DMA helper at 0x1118, which ends in `jr ra`.
   For that to resume at the dispatcher, `ra` must already be 0x1098, but nothing
   in the reachable entry path sets it (the entry sets `r5` from `mfc0 DPC_STATUS`
   and never touches `ra`). Executing the entry the way the recompiler lays it out
   therefore loops: helper -> `jr ra` -> 0x1040 -> init -> dispatch -> helper...
2. **`jr a1` in the refill.** `a1` is only set at 0x10D0 (`addi a1, ra, 0`), which
   sits *between* the `beq r0,r0` trap loop and the refill, and `0x10B0` jumps to
   0x10D4, skipping it. So the refill also depends on a register the boot
   establishes, not on anything the recompiled body computes.

In other words the microcode is written for the real RSP boot handshake (the boot
leaves `ra`/`a1` pointing at the dispatcher), and a static recompilation of the
*body* cannot reproduce it without also modelling that handshake. Feeding DMEM
0x380 directly (as tried) only dispatches an uninitialised handler; leaving it
zero dispatches handler 0, which is the DMA helper, and loops.

**Recommended way forward** (a decision, not more guessing):
- replace the recompiled aspMain with an **RSP interpreter** for the audio task -
  `D:\Personal\C++\Kyujo\src\rsp\rsp_cpu.c` is a complete RSP CPU used against real
  ucode (GPL-3.0, see the licensing note above), and it sidesteps the boot
  handshake because it executes the ROM's own instructions from the true entry;
- or write the boot shim by hand: set `ra = 0x1098` for handlers and `a1 = 0x1064`
  for the refill before entering, which means patching the generated function's
  prologue rather than the ucode;
- an HLE audio engine (decode ACMD in C) is the third option and the most work.

State: `scarsAspMain` is compiled, wired and gated behind `SCARS_RECOMP_AUDIO=1`
(default off, game unchanged). The DMEM address fixes, the private task buffer and
the RSPRecomp branch-space fix all stay - they are correct independent of this.

## Session 5 - the audio stall is a completion-message deadlock

### Reproduction
Run `scars.exe` with `SCARS_AUTO_CONFIRM=1` and no other env vars. The game boots
to the menus and then freezes at a random point inside the first second
(`rsp` sticks at 7-8 audio tasks, `ai_len=0`, display lists stop). Any
perturbation (stderr tracing, a 1-line ring buffer per event) hides it, so it is
a race, but a deterministic one once it lands.

### Evidence (in-memory ring + stall dump, see below)
Stall dump at the moment the RSP counter stopped for 120 VIs:

```
running queue:                       (empty - nothing scheduled)
SP queue      803C93C8 valid=0/1 recv_waiters=2
SI queue      803E3300 valid=1/1 recv_waiters=0
AI queue      8038D9C0 valid=1/1 recv_waiters=0
VI queue      803FF970 valid=2/2 recv_waiters=0
DP queue      803BF7F8 valid=1/1 recv_waiters=0
PRENMI        803DDBD8 valid=0/1 recv_waiters=1
```

So every completion queue is **full of messages nobody consumed**, while both
game threads wait on the SP queue, which is empty. The game cannot submit the
next task (both its threads are waiting for an SP completion), so no further
completion is produced: `rsp` sticks at 8 forever.

The same run records ~37 million failed delivery attempts (the idler retrying
undeliverable messages), i.e. 100% of a core is burned while the guest is dead.

### Root cause
Completion messages are the only thing that can wake a thread blocked in
`do_recv`. `do_send` refuses to insert into a full queue, and the runtime either
drops the message (`requeue_if_blocked == false`, the default for AI/VI/PI) or
re-queues it and retries later (default for SP/DP/timer). With queue capacity 1
and several completion sources, a completion can therefore be lost or deferred
while the thread that needs it is already blocked; the guest then waits for a
completion that will never be produced again.

Two aggravating factors:
- `pause_self` retries undeliverable messages in a tight loop with no yield, so
  a stuck guest also pegs a CPU (37M attempts in ~6 s).
- `schedule_running_thread` marks a thread QUEUED but never signals it; only
  `run_next_thread`/`resume_thread` signal. That makes the wake-up path depend
  on a third thread reaching a dispatch point.

### Fix direction (not implemented yet)
A completion is a count, not a payload: when the target queue is full, a
completion must *replace* an outstanding entry rather than be refused, so a
waiter is always woken and a queue can never wedge with unconsumed completions.
That is a semantic change to `do_send` in
`ultramodern/src/mesgqueue.cpp` (shared runtime), so it needs a decision before
implementation.

### Diagnostics now in the tree (all behind `SCARS_SCHED_TRACE` except the dump)
- `sched_record()` keeps the last 512 scheduling events in memory and prints
  them only on a stall; `SCARS_SCHED_TRACE=1` also streams them live.
- Stall detector in `trace_backend.cpp::on_vi` dumps the ring, the running
  queue, and every registered event queue (valid/msgCount/waiters) once, when
  the RSP task counter stops advancing.
- `scars_dump_running_queue()`, `scars_dump_event_queues()`,
  `scars_note_event_queue()` live in `ultramodern/src/scheduling.cpp`.

### Also fixed this session (real, committed)
- `thread_queue_pop` dereferenced null on an empty queue (crash).
- `pause_self`/`yield_self` only swapped to higher-priority threads; the idle
  loop now runs anything runnable.
