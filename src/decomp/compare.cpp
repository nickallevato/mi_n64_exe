#include <algorithm>
#include <cstring>

#include "decomp/compare.hpp"

namespace {
    using mi::decomp::Mismatch;

    void compare_reg(std::vector<Mismatch>& out, const char* name, uint64_t orig, uint64_t dec) {
        if (orig != dec && out.size() < mi::decomp::kMaxMismatches) {
            out.push_back({ name, 0, 0, orig, dec });
        }
    }
}

std::vector<Mismatch> mi::decomp::compare(const recomp_context& orig_ctx, const recomp_context& dec_ctx,
    const uint8_t* orig_ram, const uint8_t* dec_ram, uint32_t entry_sp, bool ret64) {
    std::vector<Mismatch> out;

    // Return values. In o32 a double spans an even/odd pair, stored as the even register's low/high words. r3 and the
    // odd float registers only carry a result for 64-bit returns; otherwise they're scratch and may differ.
    compare_reg(out, "r2", orig_ctx.r2, dec_ctx.r2);
    if (ret64) {
        compare_reg(out, "r3", orig_ctx.r3, dec_ctx.r3);
    }
    compare_reg(out, "f0", orig_ctx.f0.u32l, dec_ctx.f0.u32l);
    if (ret64) {
        compare_reg(out, "f1", orig_ctx.f0.u32h, dec_ctx.f0.u32h);
    }
    compare_reg(out, "f2", orig_ctx.f2.u32l, dec_ctx.f2.u32l);
    if (ret64) {
        compare_reg(out, "f3", orig_ctx.f2.u32h, dec_ctx.f2.u32h);
    }

    // Callee-saved registers.
    const gpr* orig_saved[] = { &orig_ctx.r16, &orig_ctx.r17, &orig_ctx.r18, &orig_ctx.r19, &orig_ctx.r20, &orig_ctx.r21,
        &orig_ctx.r22, &orig_ctx.r23, &orig_ctx.r29, &orig_ctx.r30 };
    const gpr* dec_saved[] = { &dec_ctx.r16, &dec_ctx.r17, &dec_ctx.r18, &dec_ctx.r19, &dec_ctx.r20, &dec_ctx.r21,
        &dec_ctx.r22, &dec_ctx.r23, &dec_ctx.r29, &dec_ctx.r30 };
    const char* saved_names[] = { "r16", "r17", "r18", "r19", "r20", "r21", "r22", "r23", "r29", "r30" };
    for (size_t i = 0; i < std::size(saved_names); i++) {
        compare_reg(out, saved_names[i], *orig_saved[i], *dec_saved[i]);
    }
    const fpr* orig_fsaved[] = { &orig_ctx.f20, &orig_ctx.f22, &orig_ctx.f24, &orig_ctx.f26, &orig_ctx.f28, &orig_ctx.f30 };
    const fpr* dec_fsaved[] = { &dec_ctx.f20, &dec_ctx.f22, &dec_ctx.f24, &dec_ctx.f26, &dec_ctx.f28, &dec_ctx.f30 };
    const char* fsaved_names[] = { "f20", "f22", "f24", "f26", "f28", "f30" };
    for (size_t i = 0; i < std::size(fsaved_names); i++) {
        compare_reg(out, fsaved_names[i], orig_fsaved[i]->u64, dec_fsaved[i]->u64);
    }

    // RAM, a word at a time, skipping the dead stack around the entry $sp. Runs of differing words merge.
    uint32_t dead_bottom = entry_sp >= 0x80000000u + kUnknownStackSlack ? entry_sp - kUnknownStackSlack : 0x80000000u;
    for (const ThreadStack& stack : kThreadStacks) {
        if (entry_sp > stack.bottom && entry_sp <= stack.top) {
            dead_bottom = std::max(stack.bottom, entry_sp >= stack.bottom + kStackSlack ? entry_sp - kStackSlack : stack.bottom);
            break;
        }
    }
    uint32_t skip_start = dead_bottom - 0x80000000u;
    uint32_t skip_end = entry_sp - 0x80000000u + kArgHomeSize;
    Mismatch* run = nullptr;
    for (uint32_t offset = 0; offset < kRamSize; offset += 4) {
        if (offset >= skip_start && offset < skip_end) {
            run = nullptr;
            continue;
        }
        uint32_t orig_word, dec_word;
        std::memcpy(&orig_word, orig_ram + offset, 4);
        std::memcpy(&dec_word, dec_ram + offset, 4);
        if (orig_word == dec_word) {
            run = nullptr;
            continue;
        }
        if (run != nullptr) {
            run->length += 4;
            continue;
        }
        if (out.size() >= kMaxMismatches) {
            break;
        }
        out.push_back({ "ram", 0x80000000u + offset, 4, orig_word, dec_word });
        run = &out.back();
    }
    return out;
}
