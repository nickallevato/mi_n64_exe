// Tests for mi::decomp::call (ctest -R decomp_dispatch), with a fake function table. Developer builds only.
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

#include "decomp/dispatch.hpp"

namespace {
    int failures = 0;

    void expect(bool ok, const char* test, const char* what) {
        if (!ok) {
            std::printf("FAIL %s: %s\n", test, what);
            failures++;
        }
    }

    struct Counts {
        int orig = 0;
        int decomp = 0;
    };
    Counts check_fn, read_fn, off_fn, outer_fn, inner_fn;

    void check_orig(uint8_t*, recomp_context* ctx) { check_fn.orig++; ctx->r2 = 1; }
    void check_decomp(uint8_t*, recomp_context* ctx) { check_fn.decomp++; ctx->r2 = 1; }
    void read_orig(uint8_t*, recomp_context*) { read_fn.orig++; }
    void read_decomp(uint8_t*, recomp_context*) { read_fn.decomp++; }
    void off_orig(uint8_t*, recomp_context*) { off_fn.orig++; }
    void off_decomp(uint8_t*, recomp_context*) { off_fn.decomp++; }
    void inner_orig(uint8_t*, recomp_context*) { inner_fn.orig++; }
    void inner_decomp(uint8_t*, recomp_context*) { inner_fn.decomp++; }
    // Both versions of outer call inner through the dispatcher, like decompiled code calling another function.
    void outer_orig(uint8_t* rdram, recomp_context* ctx) { outer_fn.orig++; mi::decomp::call(4, rdram, ctx); }
    void outer_decomp(uint8_t* rdram, recomp_context* ctx) { outer_fn.decomp++; mi::decomp::call(4, rdram, ctx); }
}

extern "C" const mi::decomp::Entry mi_decomp_entries[] = {
    { "CheckFn", check_orig, check_decomp, mi::decomp::Method::Check, false },
    { "ReadFn", read_orig, read_decomp, mi::decomp::Method::Read, false },
    { "OffFn", off_orig, off_decomp, mi::decomp::Method::Check, false },
    { "OuterFn", outer_orig, outer_decomp, mi::decomp::Method::Check, false },
    { "InnerFn", inner_orig, inner_decomp, mi::decomp::Method::Check, false },
};
extern "C" const int mi_decomp_entry_count = 5;

int main() {
    std::string list = std::string(std::getenv("TMPDIR") ? std::getenv("TMPDIR") : "/tmp") + "/decomp_dispatch_test.txt";
    std::ofstream(list) << "CheckFn t on check\nReadFn t on read\nOffFn t off check\nOuterFn t on check\nInnerFn t on check\n";
    setenv("MI_DECOMP_LIST", list.c_str(), 1);
    setenv("MI_DECOMP_CHECK", "1", 1);
    mi::decomp::init();

    std::vector<uint8_t> rdram(0x1000000, 0);
    recomp_context ctx{};
    ctx.f_odd = &ctx.f0.u32h;
    ctx.r29 = 0x800B9000;

    // A check function runs the original on a private copy and for real, and the decompiled version once.
    mi::decomp::call(0, rdram.data(), &ctx);
    expect(check_fn.orig == 2 && check_fn.decomp == 1, "check_function_checked", "expected orig 2, decomp 1");

    // A read function is never run twice.
    mi::decomp::call(1, rdram.data(), &ctx);
    expect(read_fn.orig == 0 && read_fn.decomp == 1, "read_never_doubled", "expected orig 0, decomp 1");

    // An off function runs only the original, unchecked.
    mi::decomp::call(2, rdram.data(), &ctx);
    expect(off_fn.orig == 1 && off_fn.decomp == 0, "off_never_checked", "expected orig 1, decomp 0");

    // Calls made inside a check run their selected version once per run and aren't checked themselves; the real run
    // of outer happens after the check, so its inner call is checked: inner decomp 2 (private runs) + 1, orig 2.
    mi::decomp::call(3, rdram.data(), &ctx);
    expect(outer_fn.orig == 2 && outer_fn.decomp == 1, "nested_outer", "expected outer orig 2, decomp 1");
    expect(inner_fn.decomp == 3 && inner_fn.orig == 2, "nested_not_rechecked", "expected inner decomp 3, orig 2");

    std::remove(list.c_str());
    if (failures == 0) {
        std::printf("decomp_dispatch: all tests passed\n");
    }
    return failures == 0 ? 0 : 1;
}
