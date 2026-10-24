#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "decomp/compare.hpp"
#include "decomp/dispatch.hpp"

namespace {
    std::vector<bool> use_decomp;

    // Check mode (MI_DECOMP_CHECK=1): see check_call.
    bool check_enabled = false;
    bool check_selftest = false;
    uint32_t check_every = 1;
    std::string report_path;
    constexpr int kLoggedMismatchingCalls = 5;

    struct Stats {
        std::atomic<uint64_t> calls{0};
        uint64_t checked = 0;
        uint64_t mismatching_calls = 0;
        std::string first_mismatch;
    };
    std::unique_ptr<Stats[]> stats;

    // One check at a time: the RDRAM copies are shared. A call that finds them busy (another thread, or a nested
    // dispatched call inside a check) just runs its selected version.
    std::mutex check_mutex;
    std::vector<uint8_t> ram_before, ram_after_orig;

    int find_entry(const std::string& name) {
        for (int i = 0; i < mi_decomp_entry_count; i++) {
            if (name == mi_decomp_entries[i].name) {
                return i;
            }
        }
        return -1;
    }

    // Lines are `name system on|off check|read [flags]`; only the name and the state matter at run time.
    void read_list(const char* path, std::vector<bool>& listed) {
        std::ifstream file(path);
        if (!file) {
            std::fprintf(stderr, "[decomp] can't read %s; every function uses the original\n", path);
            return;
        }
        std::string line;
        while (std::getline(file, line)) {
            line = line.substr(0, line.find('#'));
            std::istringstream fields(line);
            std::string name, system, state;
            if (!(fields >> name >> system >> state)) {
                continue;
            }
            int id = find_entry(name);
            if (id < 0) {
                std::fprintf(stderr, "[decomp] %s is listed in %s but not built in (run scripts/decomp.sh)\n", name.c_str(), path);
                continue;
            }
            listed[id] = true;
            use_decomp[id] = state == "on";
        }
    }

    std::string describe(const mi::decomp::Mismatch& m) {
        char buf[128];
        if (m.what == "ram") {
            std::snprintf(buf, sizeof(buf), "ram @%08X+%u orig=%08llX decomp=%08llX", m.addr, m.length,
                (unsigned long long)m.orig, (unsigned long long)m.decomp);
        }
        else {
            std::snprintf(buf, sizeof(buf), "%s orig=%llX decomp=%llX", m.what.c_str(),
                (unsigned long long)m.orig, (unsigned long long)m.decomp);
        }
        return buf;
    }

    // Called with check_mutex held.
    void write_report() {
        if (report_path.empty()) {
            return;
        }
        std::ofstream file(report_path, std::ios::trunc);
        file << "name,method,state,calls,checked,mismatching_calls,first_mismatch\n";
        for (int i = 0; i < mi_decomp_entry_count; i++) {
            const Stats& s = stats[i];
            file << mi_decomp_entries[i].name << ','
                 << (mi_decomp_entries[i].method == mi::decomp::Method::Check ? "check" : "read") << ','
                 << (use_decomp[i] ? "on" : "off") << ',' << s.calls.load() << ',' << s.checked << ','
                 << s.mismatching_calls << ',' << s.first_mismatch << '\n';
        }
    }

    // Run the original and the decompiled version from the same state and compare. The game always continues with
    // the original's result, so a wrong decompiled function can't change the play session.
    void check_call(int id, uint8_t* rdram, recomp_context* ctx, uint64_t call_number) {
        const mi::decomp::Entry& entry = mi_decomp_entries[id];
        Stats& s = stats[id];

        recomp_context ctx_before = *ctx;
        std::memcpy(ram_before.data(), rdram, mi::decomp::kRamSize);

        entry.orig(rdram, ctx);
        recomp_context ctx_orig = *ctx;
        std::memcpy(ram_after_orig.data(), rdram, mi::decomp::kRamSize);

        *ctx = ctx_before;
        std::memcpy(rdram, ram_before.data(), mi::decomp::kRamSize);
        entry.decomp(rdram, ctx);
        if (check_selftest) {
            ctx->r2 ^= 1;
        }

        auto mismatches = mi::decomp::compare(ctx_orig, *ctx, ram_after_orig.data(), rdram, (uint32_t)ctx_before.r29);

        *ctx = ctx_orig;
        std::memcpy(rdram, ram_after_orig.data(), mi::decomp::kRamSize);

        s.checked++;
        if (!mismatches.empty()) {
            s.mismatching_calls++;
            if (s.first_mismatch.empty()) {
                s.first_mismatch = "call " + std::to_string(call_number) + ": " + describe(mismatches[0]);
            }
            if (s.mismatching_calls <= kLoggedMismatchingCalls) {
                for (const auto& m : mismatches) {
                    std::fprintf(stderr, "[decomp] MISMATCH %s call #%llu: %s\n", entry.name,
                        (unsigned long long)call_number, describe(m).c_str());
                }
            }
        }

    }
}

#ifndef MI_DECOMP_LIST_DEFAULT
#define MI_DECOMP_LIST_DEFAULT "decomp/functions.txt"
#endif

void mi::decomp::init() {
    use_decomp.assign(mi_decomp_entry_count, false);
    stats = std::make_unique<Stats[]>(mi_decomp_entry_count);
    std::vector<bool> listed(mi_decomp_entry_count, false);

    const char* list_path = std::getenv("MI_DECOMP_LIST");
    read_list(list_path != nullptr ? list_path : MI_DECOMP_LIST_DEFAULT, listed);
    for (int i = 0; i < mi_decomp_entry_count; i++) {
        if (!listed[i]) {
            std::fprintf(stderr, "[decomp] %s is built in but not listed; using the original\n", mi_decomp_entries[i].name);
        }
    }

    if (const char* off = std::getenv("MI_DECOMP_OFF")) {
        std::istringstream names(off);
        std::string name;
        while (std::getline(names, name, ',')) {
            int id = find_entry(name);
            if (id < 0) {
                std::fprintf(stderr, "[decomp] MI_DECOMP_OFF: unknown function %s\n", name.c_str());
                continue;
            }
            use_decomp[id] = false;
        }
    }

    int on = 0;
    for (bool b : use_decomp) {
        on += b;
    }
    std::fprintf(stderr, "[decomp] %d functions: %d decomp, %d original\n", mi_decomp_entry_count, on, mi_decomp_entry_count - on);

    const char* check = std::getenv("MI_DECOMP_CHECK");
    check_enabled = check != nullptr && std::strcmp(check, "1") == 0;
    const char* selftest = std::getenv("MI_DECOMP_CHECK_SELFTEST");
    check_selftest = selftest != nullptr && std::strcmp(selftest, "1") == 0;
    if (const char* every = std::getenv("MI_DECOMP_CHECK_EVERY")) {
        check_every = std::max(1ul, std::strtoul(every, nullptr, 10));
    }
    if (const char* path = std::getenv("MI_DECOMP_REPORT")) {
        report_path = path;
        // Every 2 s from a thread of its own (test runs kill the game, so an exit hook alone isn't enough), and at exit.
        std::thread([] {
            while (true) {
                std::this_thread::sleep_for(std::chrono::seconds(2));
                std::lock_guard lock(check_mutex);
                write_report();
            }
        }).detach();
        std::atexit([] {
            std::lock_guard lock(check_mutex);
            write_report();
        });
    }
    if (check_enabled) {
        ram_before.resize(mi::decomp::kRamSize);
        ram_after_orig.resize(mi::decomp::kRamSize);
        std::fprintf(stderr, "[decomp] check mode: every %u call(s)%s\n", check_every, check_selftest ? ", self-test" : "");
    }
}

void mi::decomp::call(int id, uint8_t* rdram, recomp_context* ctx) {
    const Entry& entry = mi_decomp_entries[id];
    uint64_t call_number = ++stats[id].calls;
    if (check_enabled && use_decomp[id] && entry.method == Method::Check && call_number % check_every == 0) {
        std::unique_lock lock(check_mutex, std::try_to_lock);
        if (lock.owns_lock()) {
            check_call(id, rdram, ctx, call_number);
            return;
        }
    }
    if (use_decomp[id]) {
        entry.decomp(rdram, ctx);
    }
    else {
        entry.orig(rdram, ctx);
    }
}
