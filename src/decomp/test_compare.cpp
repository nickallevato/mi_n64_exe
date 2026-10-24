// Tests for mi::decomp::compare (ctest -R decomp_compare). Developer builds only.
#include <cstdio>
#include <cstring>
#include <vector>

#include "decomp/compare.hpp"

using mi::decomp::compare;
using mi::decomp::kRamSize;

namespace {
    int failures = 0;

    void expect(bool ok, const char* test, const char* what) {
        if (!ok) {
            std::printf("FAIL %s: %s\n", test, what);
            failures++;
        }
    }

    struct State {
        recomp_context orig{};
        recomp_context dec{};
        std::vector<uint8_t> orig_ram = std::vector<uint8_t>(kRamSize, 0);
        std::vector<uint8_t> dec_ram = std::vector<uint8_t>(kRamSize, 0);
        static constexpr uint32_t sp = 0x80300000;

        void set_word(std::vector<uint8_t>& ram, uint32_t addr, uint32_t value) {
            std::memcpy(&ram[addr - 0x80000000], &value, 4);
        }
        std::vector<mi::decomp::Mismatch> run() {
            return compare(orig, dec, orig_ram.data(), dec_ram.data(), sp);
        }
    };

    void identical_states_match() {
        State s;
        s.orig.r2 = s.dec.r2 = 5;
        expect(s.run().empty(), __func__, "expected no mismatches");
    }

    void v0_differs() {
        State s;
        s.orig.r2 = 1;
        s.dec.r2 = 2;
        auto m = s.run();
        expect(m.size() == 1 && m[0].what == "r2" && m[0].orig == 1 && m[0].decomp == 2, __func__, "expected one r2 mismatch");
    }

    void callee_saved_differs() {
        State s;
        s.orig.r16 = 1;
        s.orig.f20.u64 = 7;
        auto m = s.run();
        expect(m.size() == 2 && m[0].what == "r16" && m[1].what == "f20", __func__, "expected r16 and f20 mismatches");
    }

    void caller_saved_ignored() {
        State s;
        s.orig.r8 = 1;
        s.orig.f4.u64 = 1;
        s.orig.hi = 1;
        expect(s.run().empty(), __func__, "temporaries must not count");
    }

    void write_below_entry_sp_ignored() {
        State s;
        s.set_word(s.orig_ram, State::sp - 8, 0x1234);
        expect(s.run().empty(), __func__, "dead stack must not count");
    }

    void write_in_arg_home_ignored() {
        State s;
        s.set_word(s.orig_ram, State::sp + 4, 0x1234);
        expect(s.run().empty(), __func__, "the o32 argument save area must not count");
    }

    void write_above_arg_home_counts() {
        State s;
        s.set_word(s.orig_ram, State::sp + 0x10, 0x1234);
        auto m = s.run();
        expect(m.size() == 1 && m[0].what == "ram" && m[0].addr == State::sp + 0x10 && m[0].length == 4 && m[0].orig == 0x1234,
            __func__, "expected one RAM mismatch at sp+0x10");
    }

    void adjacent_words_merge() {
        State s;
        s.set_word(s.orig_ram, 0x80100000, 1);
        s.set_word(s.orig_ram, 0x80100004, 2);
        s.set_word(s.orig_ram, 0x80100010, 3);
        auto m = s.run();
        expect(m.size() == 2 && m[0].addr == 0x80100000 && m[0].length == 8 && m[1].addr == 0x80100010,
            __func__, "expected two runs: 8 bytes at 0x80100000, 4 at 0x80100010");
    }

    void mismatches_capped() {
        State s;
        for (uint32_t i = 0; i < 40; i++) {
            s.set_word(s.orig_ram, 0x80100000 + i * 8, 1);
        }
        expect(s.run().size() == mi::decomp::kMaxMismatches, __func__, "expected the cap");
    }
}

int main() {
    identical_states_match();
    v0_differs();
    callee_saved_differs();
    caller_saved_ignored();
    write_below_entry_sp_ignored();
    write_in_arg_home_ignored();
    write_above_arg_home_counts();
    adjacent_words_merge();
    mismatches_capped();
    if (failures == 0) {
        std::printf("decomp_compare: all tests passed\n");
    }
    return failures == 0 ? 0 : 1;
}
