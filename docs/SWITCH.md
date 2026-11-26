# Nintendo Switch target

The Switch build is the same sources as the PC build: N64Recomp corpus +
N64ModernRuntime + SDL2 + RT64. Only the toolchain and the Vulkan driver differ.

| Piece | PC | Switch |
|---|---|---|
| Toolchain | MSVC / clang | devkitA64 + libnx |
| Window | SDL2 (bundled with RT64) | switch-sdl2 portlib |
| Renderer | RT64 (D3D12/Vulkan) | RT64 (Vulkan) |
| Vulkan driver | system | **NXVK** (Mesa NVK port) |
| Output | `scars.exe` | `scars.nro` |
| Config/ROM | next to the exe | `sdmc:/switch/scars/` |

## Prerequisites

1. devkitPro with `switch-dev`, `switch-sdl2`, `switch-tools`
   (`dkp-pacman -Syu --needed switch-dev switch-sdl2 switch-tools`).
2. `DEVKITPRO` set to the install prefix (`C:/devkitPro` on Windows).
3. RT64 in `thirdparty/rt64` (`powershell -File tools/fetch_rt64.ps1`).
4. **NXVK** installed into `${DEVKITPRO}/portlibs/switch` so that
   `lib/pkgconfig/nxvk.pc`, its headers and the `libvulkan` import library exist.
   RT64 does not bundle a Vulkan driver and the console has none; without NXVK the
   configure step stops with a message pointing here.

## Build

**Use a Linux or WSL host.** RT64 compiles its shaders with DXC at build time and
selects the shader-compiler binary by *target* platform; for any non-Windows
target it expects `dxc-linux`, which cannot run on a Windows host. This is the
same reason dkr-n64recomp builds its Switch NRO from WSL. Configure from Windows
does work as far as devkitA64, SDL2 and the NXVK check (verified), but the RT64
shader step needs the Linux host.

```bash
cmake --preset switch
cmake --build --preset switch
```

The build produces `build/switch/scars.nro` (the `.nacp` and `.nro` steps run as a
POST_BUILD step of the `scars` target). The generated corpus must already exist
(`tools/generate_aot.ps1` / the `generate-aot` target) - it is architecture
independent and shared with the PC build.

From WSL, the Windows devkitPro install needs path translation; the pattern the
Kyujo project documents (`C:\dkp-shim`, see its `N64_PROJECT_STATE.md` §2) applies
here unchanged. A native Windows configure also works when devkitPro's CMake
platform modules are on the prefix.

## SD card layout

```
sdmc:/switch/scars/scars.nro
sdmc:/switch/scars/scars.z64        <- the Europe/PAL dump, user supplied
```

The runtime looks for the ROM next to the executable (that is `/switch/scars/`
after Horizon maps the SD path) and then up to five parent directories, so the
same build works from `sdmc:/switch/scars/` without environment variables.

## Known gaps (tracked in N64_PROJECT_STATE.md)

- Audio is silent until the audio ucode (`0x80271BB0`) is recompiled - the same
  gap as on PC.
- The runtime's VI thread paces at 60 Hz while the game is a 50 Hz PAL title;
  frame pacing is a Phase 5 item for both platforms.
- Save type is undetermined; Controller Pak code currently reports "no pak".
