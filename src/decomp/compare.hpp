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
    // Dead stack below the entry $sp (the callee's frames) that may differ between versions.
    constexpr uint32_t kStackSlack = 0x10000;
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
