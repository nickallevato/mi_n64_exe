#ifndef MI_DECOMP_COMPARE_HPP
#define MI_DECOMP_COMPARE_HPP

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "recomp.h"

namespace mi::decomp {
    // RAM the check compares: all of RDRAM.
    constexpr uint32_t kRamSize = 0x800000;
    // Dead stack below the entry $sp (the callee's frames) may differ between versions. It's ignored down to the
    // bottom of the thread's stack, at most kStackSlack bytes; for an $sp outside every known stack, kUnknownStackSlack.
    constexpr uint32_t kStackSlack = 0x10000;
    constexpr uint32_t kUnknownStackSlack = 0x400;

    // The game threads' stacks, from the osCreateThread calls (the libultra manager threads are replaced by the
    // runtime and run no game code). Three of the splat symbols also hold the thread's 0x1B0-byte OSThread first.
    struct ThreadStack {
        uint32_t bottom;
        uint32_t top;
    };
    constexpr ThreadStack kThreadStacks[] = {
        { 0x800A990C, 0x800B9910 }, // game thread (Game_ThreadMain, id 6): D_800A990C
        { 0x800A7640, 0x800A7A50 }, // boot thread (func_8000E020, id 1): D_800A7640
        { 0x800A2100, 0x800A2300 }, // scheduler (func_8006C86C, id 9): D_800A1F50 + OSThread
        { 0x800A2930, 0x800A2B30 }, // thread 7 (func_8006E730): D_800A2780 + OSThread
        { 0x800A2D50, 0x800A2F50 }, // thread 8 (func_8006EA04): D_800A2BA0 + OSThread
        { 0x800A3184, 0x800A5190 }, // audio (func_800725A4, id 3): D_800A3184
    };
    // o32: the 16-byte argument save area just above the entry $sp belongs to the callee.
    constexpr uint32_t kArgHomeSize = 0x10;
    constexpr size_t kMaxMismatches = 16;

    struct Mismatch {
        std::string what;  // register name (r2, f0, ...) or "ram"
        uint32_t addr;     // N64 address of the first differing word ("ram" only)
        uint32_t length;   // bytes in the run of differing words ("ram" only)
        uint64_t orig;     // register value, or the first differing word
        uint64_t decomp;
    };

    // Compare the state after the original and after the decompiled version of a function, both run from the same
    // starting state. Compared: return values (r2, f0, f2; with ret64 also r3, f1, f3, which only carry a 64-bit
    // result), callee-saved registers (r16-r23, r29, r30, f20-f31) and RAM except the dead stack around entry_sp.
    // RAM buffers are RDRAM as the recomp stores it (native-endian words), kRamSize bytes from 0x80000000.
    std::vector<Mismatch> compare(const recomp_context& orig_ctx, const recomp_context& dec_ctx,
        const uint8_t* orig_ram, const uint8_t* dec_ram, uint32_t entry_sp, bool ret64);
}

#endif
