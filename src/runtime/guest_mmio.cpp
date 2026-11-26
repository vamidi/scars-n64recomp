#include "guest_mmio.h"
#include "scars_log.h"

#include <algorithm>
#include <cstring>

#include <librecomp/game.hpp>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

namespace scars {
namespace {

struct GuestWindow {
    int32_t guest_base;   // sign-extended guest address as the CPU sees it
    size_t size;
    const char* name;
};

// The SP/DP/MI/VI/AI/PI/SI register blocks are spread over 0xA4040000..0xA4800000.
// One window covers all of them. 0xB0000000 is the KSEG1 alias of the cartridge
// ROM, 0x1FC00000 the PIF boot ROM.
constexpr GuestWindow kWindows[] = {
    {static_cast<int32_t>(0xA4000000), 0x01000000, "SP/DP/MI/VI/AI/PI/SI registers"},
    {static_cast<int32_t>(0xB0000000), 0x00100000, "cartridge ROM (KSEG1 alias)"},
};

constexpr size_t kRomAliasBytes = 0x00100000;

// guest address -> offset into the RDRAM allocation, matching MEM_*/TO_PTR.
size_t rdram_offset(int32_t guest_address) {
    return static_cast<size_t>(static_cast<uint64_t>(static_cast<int64_t>(guest_address)) - 0xFFFFFFFF80000000ull);
}

} // namespace

void map_guest_hardware_windows(uint8_t* rdram) {
    if (rdram == nullptr) {
        return;
    }

#ifdef _WIN32
    for (const GuestWindow& window : kWindows) {
        const size_t offset = rdram_offset(window.guest_base);
        DWORD old_protect = 0;
        if (!VirtualProtect(rdram + offset, window.size, PAGE_READWRITE, &old_protect)) {
            logf("cannot map %s (guest 0x%08X, offset 0x%zX): Win32 error %lu", window.name,
                 static_cast<uint32_t>(window.guest_base), offset, GetLastError());
            continue;
        }
        logf("mapped %s: guest 0x%08X..0x%08X at RDRAM offset 0x%zX", window.name,
             static_cast<uint32_t>(window.guest_base),
             static_cast<uint32_t>(window.guest_base) + static_cast<uint32_t>(window.size), offset);
    }

    // KSEG1 0xB0000000 + n is ROM byte n, so the header and boot image the game
    // reads directly return real data instead of zeros.
    const std::span<const uint8_t> rom = recomp::get_rom();
    if (!rom.empty()) {
        const size_t copy_size = (std::min)(rom.size(), kRomAliasBytes);
        std::memcpy(rdram + rdram_offset(static_cast<int32_t>(0xB0000000)), rom.data(), copy_size);
        logf("cart ROM alias preloaded with %zu bytes", copy_size);
    } else {
        logf("cart ROM alias left empty: no ROM loaded");
    }
#else
    (void) rdram;
    logf("guest hardware windows are only mapped on Windows so far");
#endif
}

} // namespace scars
