# scars-n64recomp

Static recompilation of **S.C.A.R.S** (Nintendo 64) to native PC, in the same
layout as the Tales of Legendia PS2 recomp: engine checkouts live under
`thirdparty/`, the game/runtime code lives under `src/`, and CMake drives both.

> No game code, ROM, or generated output is tracked or distributed. You supply
> your own lawfully obtained ROM and the ELF/symbols produced locally from it.

Live status and the ordered roadmap to a playable build:
[`n64_project_state.md`](n64_project_state.md).

## Layout

| Path | Purpose |
|------|---------|
| `thirdparty/n64recomp` | Your N64Recomp fork (`vamidi/n64recomp`): the recompiler, RSPRecomp and shared headers |
| `thirdparty/N64ModernRuntime` | N64 runtime: `ultramodern` (libultra replacement) and `librecomp` (recomp runtime) |
| `cmake/ScarsRuntime.cmake` | Wires N64ModernRuntime into the build against your fork |
| `cmake/ScarsGeneratedPayload.cmake` | Compiles and links the N64Recomp output |
| `recomp/scars.toml` | N64Recomp configuration (ROM + symbols paths, output directory) |
| `tools/generate_aot.ps1` | One-shot AOT pipeline: Ghidra → n64sym → N64Recomp |
| `tools/ghidra` | Headless scripts that seed analysis and export the symbols TOML |
| `tools/merge_symbols.py` | Merges n64sym libultra names into the Ghidra symbol map |
| `src/runtime` | The `scars` executable |
| `build/generated` | N64Recomp output (gitignored) |

## Submodules

The checkouts were added with:

```sh
git submodule add https://github.com/vamidi/n64recomp thirdparty/n64recomp
git submodule add https://github.com/N64Recomp/N64ModernRuntime thirdparty/N64ModernRuntime
git submodule update --init --recursive
```

On a fresh clone:

```sh
git clone --recurse-submodules git@github.com:vamidi/scars-n64recomp.git
# or, after a normal clone:
git submodule update --init --recursive
```

To pull newer commits of your fork later:

```sh
git submodule update --remote thirdparty/n64recomp
git add thirdparty/n64recomp
git commit -m "Update N64Recomp"
```

## Requirements

- CMake 3.24+
- Ninja
- A C++20 compiler: Clang 16+, clang-cl, or MSVC 2022+
- Git (submodules)
- For AOT generation: Ghidra 12 (`SCARS_GHIDRA_DIR`), Python 3, and n64sym
  (auto-downloaded on first run)

No SDL/renderer dependency yet; the runtime executable is still a scaffold.

## Build

### Windows

Run from an **x64 Native Tools Command Prompt for Visual Studio** (or after
`vcvars64.bat`), so the compiler is on `PATH`:

```bat
cmake --preset windows-x64-release
cmake --build --preset windows-x64-release
```

With clang-cl explicitly (paths from the Visual Studio installer are used by
the ToL build too):

```bat
cmake -S . -B build/clang -G Ninja ^
  -DCMAKE_C_COMPILER="C:/Program Files/Microsoft Visual Studio/18/Community/VC/Tools/Llvm/x64/bin/clang-cl.exe" ^
  -DCMAKE_CXX_COMPILER="C:/Program Files/Microsoft Visual Studio/18/Community/VC/Tools/Llvm/x64/bin/clang-cl.exe" ^
  -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build/clang
```

### Linux / macOS

```sh
cmake --preset linux-x86_64-release   # or macos-arm64-release
cmake --build --preset linux-x86_64-release
```

Outputs land in `build/<preset>/bin`:

- `N64Recomp` — the recompiler
- `RSPRecomp` — the RSP microcode recompiler
- `scars` — the game runtime (scaffold until a renderer is wired up)

## AOT generation (static recompilation)

N64Recomp translates code from a function map: `section → functions (name,
vram, size)`. That map is metadata, not decompiled C. `tools/generate_aot.ps1`
builds it automatically from your own dump:

1. Put your dump at `data/scars.z64` (Europe `.z64`).
2. Run the pipeline:

```bat
powershell -NoProfile -ExecutionPolicy Bypass -File tools/generate_aot.ps1
```

or through CMake:

```bat
cmake --build build/windows-x64-release --target generate-aot
```

What it does:

- **Ghidra headless** imports the ROM as MIPS at `0x801FF400` (so ROM `0x1000`
  maps to the linked entry `0x80200400`), seeds analysis at the entry point,
  and exports every discovered function as an N64Recomp symbols TOML.
- **n64sym** scans the ROM against its libultra signatures and names the
  runtime functions that N64Recomp replaces instead of translating.
- **merge_symbols.py** merges those names into the TOML, fixes the boot-stub
  size, and fills functions the signature scan misses.
- **N64Recomp** emits C into `build/generated`.

Current output: **841 functions → 15 `funcs_*.c` files** (~12 MB of C), plus
`funcs.h`, `lookup.cpp` and `recomp_overlays.inl` (18 files total).

3. Build with the corpus linked:

```bat
cmake --preset windows-x64-release -DSCARS_LINK_GENERATED=ON
cmake --build --preset windows-x64-release
```

If you later obtain a disassembly ELF with mdebug symbols, set `elf_path`
instead of `symbols_file_path` in `recomp/scars.toml` and run
`cmake --build ... --target recompile`.

## CMake options

| Option | Default | Meaning |
|--------|---------|---------|
| `SCARS_BUILD_RECOMPILER` | `ON` | Build the tools from `thirdparty/n64recomp` |
| `SCARS_BUILD_RUNTIME` | `ON` | Build `ultramodern`, `librecomp`, and `scars` |
| `SCARS_LINK_GENERATED` | `OFF` | Compile/link the corpus in `SCARS_GENERATED_DIR` |
| `SCARS_N64RECOMP_DIR` | `thirdparty/n64recomp` | Recompiler checkout to build |
| `SCARS_MODERN_RUNTIME_DIR` | `thirdparty/N64ModernRuntime` | Runtime checkout |
| `SCARS_GENERATED_DIR` | `build/generated` | N64Recomp output directory |
| `SCARS_GENERATED_OPTFLAGS` | compiler-dependent | Flags for generated translation units |

## Why librecomp is compiled manually

N64ModernRuntime's `librecomp/CMakeLists.txt` always adds its *own* nested
N64Recomp checkout, which would clash with `thirdparty/n64recomp`. Instead,
`cmake/ScarsRuntime.cmake` compiles `librecomp`'s sources against the targets
from your fork. Updating N64ModernRuntime only needs a re-run of CMake; new
librecomp sources are picked up by the glob.

## Next step: the renderer

`src/runtime/main.cpp` is a placeholder. A playable build still needs a
renderer backend (RT64 or equivalent) plus the audio/input/RSP callbacks passed
to `recomp::start()` — the equivalent of the `src/gfx` layer in the ToL
project.

## Legal

This is an interoperability-oriented source project, not legal advice. It is
not affiliated with or endorsed by the S.C.A.R.S rightsholders. All trademarks
and copyrighted game material belong to their respective owners. The original
code in this repository is MIT licensed (see `LICENSE`); the submodules retain
their own licenses.
