// Controller Pak storage.
//
// S.C.A.R.S saves to a Controller Pak in port 1 (proven by the boot-time probe
// "NO CONTROLLER PAK IN CONTROLLER 1"). The game ships libultra's PFS code
// itself: its translated `contpfs` internals call the low-level primitives that
// N64Recomp replaces (`__osContRamRead/Write`, `__osPfsSelectBank`,
// `__osCheckId`, `__osPfsRWInode`, ...), and its directory/file calls go through
// the public `osPfs*` API.
//
// This file supplies the *storage* those layers sit on: a 32 KiB pak image
// backed by a host file. It is deliberately below the checksum layer - the
// game's own code computes and verifies the PFS checksums (`__osSumcalc`,
// `__osIdCheckSum` are translated in the corpus), and the Joybus CRCs
// (`__osContAddressCrc`, `__osContDataCrc`) are never called by this ROM, so
// there is nothing to match there. Reads and writes are plain 32-byte block
// copies, which is exactly what the controller-pak hardware does at this layer.
//
// Image layout is whatever the game's own `osPfsInitPak` formats it into: the
// file starts zeroed and the game writes its header, inode tables and directory.

#include "guest_pak.h"
#include "scars_log.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <vector>

#include <librecomp/game.hpp>
#include <librecomp/helpers.hpp>
#include <recomp.h>
#include <ultramodern/ultra64.h>
#include <ultramodern/ultramodern.hpp>

namespace scars {
namespace {

// A Controller Pak is 32 KiB of 256-byte pages addressed in 32-byte blocks.
constexpr size_t kPakSize = 0x8000;
constexpr uint32_t kBlockSize = 32;
constexpr uint32_t kPageSize = 256;

std::vector<uint8_t> g_image;
bool g_loaded = false;
bool g_dirty = false;
std::filesystem::path g_path;

std::filesystem::path image_path() {
    if (g_path.empty()) {
        std::filesystem::path directory = recomp::get_config_path();
        if (directory.empty()) {
            directory = ".";
        }
        g_path = directory / "SCARS.pak";
    }
    return g_path;
}

void ensure_loaded() {
    if (g_loaded) {
        return;
    }
    g_loaded = true;
    g_image.assign(kPakSize, 0);

    const std::filesystem::path path = image_path();
    std::FILE* file = std::fopen(path.string().c_str(), "rb");
    if (file == nullptr) {
        logf("controller pak: creating a blank image at %s (%u bytes)", path.string().c_str(),
             static_cast<unsigned>(kPakSize));
        return;
    }
    const size_t read = std::fread(g_image.data(), 1, kPakSize, file);
    std::fclose(file);
    logf("controller pak: loaded %zu bytes from %s", read, path.string().c_str());
}

void flush() {
    if (!g_dirty) {
        return;
    }
    g_dirty = false;
    const std::filesystem::path path = image_path();
    std::FILE* file = std::fopen(path.string().c_str(), "wb");
    if (file == nullptr) {
        logf("controller pak: cannot write %s", path.string().c_str());
        return;
    }
    std::fwrite(g_image.data(), 1, g_image.size(), file);
    std::fclose(file);
}

// Copy to/from the image at a pak byte address. Addresses are 32-byte aligned
// (the hardware mask); anything outside the pak reads as zero and is not stored.
void pak_read(uint32_t address, void* destination, size_t size) {
    ensure_loaded();
    uint8_t* out = static_cast<uint8_t*>(destination);
    for (size_t i = 0; i < size; i++) {
        const size_t offset = (address & ~(kBlockSize - 1)) + i;
        out[i] = offset < g_image.size() ? g_image[offset] : 0;
    }
}

void pak_write(uint32_t address, const void* source, size_t size) {
    ensure_loaded();
    const uint8_t* in = static_cast<const uint8_t*>(source);
    const size_t base = address & ~(kBlockSize - 1);
    for (size_t i = 0; i < size; i++) {
        const size_t offset = base + i;
        if (offset < g_image.size()) {
            g_image[offset] = in[i];
        }
    }
    g_dirty = true;
}

// Whether the pak is reported as inserted. Off by default: the storage layer and
// the single-channel probe work, but the game also calls the public osPfs* API
// (AllocateFile/FindFile/Checker/RepairId), which librecomp still stubs as
// "no pak". Reporting a pak now would turn a clean "continue without save" into
// a save error. Set SCARS_CONTROLLER_PAK=1 to exercise the layer.
bool pak_enabled() {
    static const bool enabled = [] {
        const char* value = std::getenv("SCARS_CONTROLLER_PAK");
        return value != nullptr && std::strcmp(value, "0") != 0;
    }();
    return enabled;
}

// libultra's __OSInode is one 256-byte page of 128 two-byte block entries.
constexpr uint32_t kInodePageSize = 256;
constexpr uint8_t kPfsWrite = 0;

} // namespace

void guest_pak_flush() {
    flush();
}

// s32 __osContRamRead(OSMesgQueue *mq, int channel, u16 address, u8 *buffer)
// The hardware transaction is synchronous here; the caller only inspects the
// return value, so no SI completion message is posted.
extern "C" void __osContRamRead_recomp(uint8_t* rdram, recomp_context* ctx) {
    (void) rdram;
    const uint32_t address = static_cast<uint32_t>(ctx->r6) & 0xFFFF;
    u8* buffer = _arg<3, u8*>(rdram, ctx);
    if (buffer == nullptr) {
        ctx->r2 = -1;
        return;
    }
    pak_read(address, buffer, 32);
    ctx->r2 = 0;
}

// s32 __osContRamWrite(OSMesgQueue *mq, int channel, u16 address, u8 *buffer,
//                      int force)
extern "C" void __osContRamWrite_recomp(uint8_t* rdram, recomp_context* ctx) {
    (void) rdram;
    const uint32_t address = static_cast<uint32_t>(ctx->r6) & 0xFFFF;
    const u8* buffer = _arg<3, u8*>(rdram, ctx);
    if (buffer == nullptr) {
        ctx->r2 = -1;
        return;
    }
    pak_write(address, buffer, 32);
    // Saves are rare; keeping the host file current avoids losing one on a crash.
    flush();
    ctx->r2 = 0;
}

// The pak has a single bank; the call exists to switch between the two 16 KiB
// halves of a 32 KiB device and the game's own code drives it.
extern "C" void __osPfsSelectBank_recomp(uint8_t* rdram, recomp_context* ctx) {
    (void) rdram;
    ensure_loaded();
    ctx->r2 = 0;
}

// s32 __osCheckId(OSPfs *pfs): the game's contpfs code validates the header it
// reads through __osContRamRead; nothing extra to check here.
extern "C" void __osCheckId_recomp(uint8_t* rdram, recomp_context* ctx) {
    (void) rdram;
    ensure_loaded();
    ctx->r2 = 0;
}

// s32 __osPfsRWInode(OSPfs *pfs, __OSInode *inode, u8 flag, u8 bank)
// One 256-byte inode page. Writing mirrors it into the mirror inode table, which
// is what libultra does so a half-written update can be recovered.
extern "C" void __osPfsRWInode_recomp(uint8_t* rdram, recomp_context* ctx) {
    OSPfs* pfs = _arg<0, OSPfs*>(rdram, ctx);
    u8* inode = _arg<1, u8*>(rdram, ctx);
    const uint8_t flag = _arg<2, u8>(rdram, ctx);
    const uint8_t bank = _arg<3, u8>(rdram, ctx);

    if (pfs == nullptr || inode == nullptr) {
        ctx->r2 = -1;
        return;
    }

    const uint32_t table_page = static_cast<uint32_t>(pfs->inode_table) + (bank & 1u);
    const uint32_t mirror_page = static_cast<uint32_t>(pfs->minode_table) + (bank & 1u);

    if (flag == kPfsWrite) {
        pak_write(table_page * kPageSize, inode, kInodePageSize);
        if (pfs->minode_table != 0 && pfs->minode_table != pfs->inode_table) {
            pak_write(mirror_page * kPageSize, inode, kInodePageSize);
        }
        flush();
    } else {
        pak_read(table_page * kPageSize, inode, kInodePageSize);
    }
    ctx->r2 = 0;
}

// Builds the 56-byte SI request block that __osContRamRead/Write would send.
// Unused here: the storage layer answers directly, so nothing consumes it.
extern "C" void __osPackRequestData_recomp(uint8_t* rdram, recomp_context* ctx) {
    u8* data = _arg<0, u8*>(rdram, ctx);
    if (data != nullptr) {
        std::memset(data, 0, 56);
    }
}

// Raw SI DMA: the pak path never needs it now (the primitives above answer the
// request directly), but a caller that drives the channel protocol itself still
// has to see its completion message or it would block forever.
// s32 osPfsIsPlug(OSMesgQueue *mq, u8 *pattern)
// Bit n set = controller port n+1 answered with a pak. S.C.A.R.S asks this first
// and shows "NO CONTROLLER PAK IN CONTROLLER 1" when the pattern is zero.
extern "C" void osPfsIsPlug_recomp(uint8_t* rdram, recomp_context* ctx) {
    (void) rdram;
    u8* pattern = _arg<1, u8*>(rdram, ctx);
    if (pattern == nullptr) {
        ctx->r2 = 0;
        return;
    }
    if (pak_enabled()) {
        ensure_loaded();
        *pattern = 0x1; // controller port 1
    } else {
        *pattern = 0x0;
    }
    ctx->r2 = 0;
}

extern "C" void __osSiRawStartDma_recomp(uint8_t* rdram, recomp_context* ctx) {
    (void) rdram;
    (void) ctx;
    ultramodern::send_si_message();
    ctx->r2 = 0;
}

} // namespace scars
