#!/usr/bin/env bash
# Builds the Nintendo Switch .nro (devkitA64 + libnx + NXVK).
# Run from Linux/WSL: RT64's shader step uses dxc-linux for the console target.
#
# Linux/WSL: point DEVKITPRO at devkitPro and run this from the repo root.
# Windows:   use the cmake presets directly from a shell that has DEVKITPRO set
#            (see docs/SWITCH.md).
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$here"

if [ -z "${DEVKITPRO:-}" ]; then
    if [ -n "${DKP_SHIM:-}" ]; then
        export DEVKITPRO="$DKP_SHIM"
    elif [ -d /opt/devkitpro ]; then
        export DEVKITPRO=/opt/devkitpro
    else
        echo "DEVKITPRO is not set (and neither DKP_SHIM nor /opt/devkitpro exist)." >&2
        exit 1
    fi
fi

if [ ! -f thirdparty/rt64/CMakeLists.txt ]; then
    echo "RT64 is missing: run 'powershell -File tools/fetch_rt64.ps1' (or copy a checkout into thirdparty/rt64)." >&2
    exit 1
fi

if [ ! -f "$DEVKITPRO/portlibs/switch/lib/pkgconfig/nxvk.pc" ]; then
    echo "warning: nxvk.pc not found; RT64 will have no Vulkan driver on the console." >&2
    echo "         See docs/SWITCH.md for the NXVK portlib." >&2
fi

cmake --preset switch
cmake --build --preset switch

echo
echo "built: build/switch/scars.nro"
echo "copy it to sdmc:/switch/scars/ together with the Europe/PAL ROM as scars.z64"
