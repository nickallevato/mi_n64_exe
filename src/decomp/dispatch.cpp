#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "decomp/dispatch.hpp"

namespace {
    std::vector<bool> use_decomp;

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
}

#ifndef MI_DECOMP_LIST_DEFAULT
#define MI_DECOMP_LIST_DEFAULT "decomp/functions.txt"
#endif

void mi::decomp::init() {
    use_decomp.assign(mi_decomp_entry_count, false);
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
}

void mi::decomp::call(int id, uint8_t* rdram, recomp_context* ctx) {
    const Entry& entry = mi_decomp_entries[id];
    if (use_decomp[id]) {
        entry.decomp(rdram, ctx);
    }
    else {
        entry.orig(rdram, ctx);
    }
}
