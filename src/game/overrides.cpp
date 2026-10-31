// Data overrides: replace decoded Burp archive entries with files from a folder, so level edits can be loaded without
// changing the ROM (the app rejects modified ROMs). Called from hooks at the end of Burp_LoadTo and Burp_Load
// (mi.us.toml), after an entry has been decoded into RAM and before the game reads or relocates it.
//
// MI_OVERRIDE_DIR=dir (or Debug > Load Data Overrides, which reads <app folder>/overrides): dir/rom_XXXXXXXX.bin
// replaces the entry whose data starts at ROM address XXXXXXXX (8 hex digits, as listed by scripts/levels/burp.py).
// The file is the decoded entry in the N64's (big-endian) byte order and must be the same size as the original for now.
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <unordered_map>
#include <vector>

#include "mi_config.h"

namespace {
    std::unordered_map<uint32_t, std::vector<uint8_t>> load_overrides() {
        std::unordered_map<uint32_t, std::vector<uint8_t>> overrides;
        std::string dir;
        if (const char* env = std::getenv("MI_OVERRIDE_DIR")) {
            dir = env;
        }
        else if (mi::get_load_overrides()) {
            dir = mi::get_overrides_dir();
            std::fprintf(stderr, "Override folder: %s\n", dir.c_str());
        }
        if (dir.empty()) {
            return overrides;
        }
        std::error_code ec;
        for (const auto& file : std::filesystem::directory_iterator(dir, ec)) {
            std::string name = file.path().filename().string();
            unsigned int rom_address = 0;
            if (std::sscanf(name.c_str(), "rom_%8X.bin", &rom_address) != 1) {
                continue;
            }
            std::ifstream in(file.path(), std::ios::binary);
            overrides[rom_address] = std::vector<uint8_t>(std::istreambuf_iterator<char>(in), {});
            std::fprintf(stderr, "Override: %s (%zu bytes)\n", name.c_str(), overrides[rom_address].size());
        }
        return overrides;
    }

    uint32_t ram_u32(uint8_t* rdram, uint32_t addr) {
        return *reinterpret_cast<uint32_t*>(rdram + (addr - 0x80000000u));
    }
}

// Frustration-free Terminal Room (General > Frustration-free Terminal Room). The Terminal Room's level script (archive
// 0x8C5B10, entry 1: "SOOL", 45 functions of stack bytecode) has the card reader in function 14 and the computer in
// function 13; state 5 of each is its interaction (the card reader's switches the computer on, and the computer's says
// "I need to switch on the computer first" until then). Normally a state starts only when Ethan touches the target's
// trigger box while facing it at the right height. Function 6 says the height callouts ("This seems like the correct
// height to reach the card-reader" at height 120-145, "Seems to be the proper height to reach the computer" at 90-120),
// once per visit to each height. Make each callout also start its interaction, with op 0x38 (set a function's state):
// push 5, push <function>, op 0x38.
//  - Card reader: in the callout's one-time block, replace the check that skips a first-visit-only step (load var 0x28,
//    not, jz; 7 bytes), so that step runs on every visit too.
//  - Computer: replace the reset of the card reader callout's flag (push 0, push 0x1C, store, drop; 8 bytes; it only
//    lets that callout show again, and the card reader is done by then), plus a yield (op 0x32) to fill the 8th byte.
namespace {
    constexpr uint32_t terminal_room_script_rom = 0x8C5BF8;
    constexpr uint32_t terminal_room_script_size = 0xBACE;
    constexpr uint32_t callout_function = 0x1906;  // function 6
    struct ScriptPatch {
        uint32_t offset;
        std::vector<uint8_t> original;
        std::vector<uint8_t> replacement;
    };
    const ScriptPatch terminal_room_patches[] = {
        {callout_function + 0xAF, {0x07, 0x28, 0x00, 0x14, 0x03, 0xCF, 0x00}, {0x05, 0x05, 0x00, 0x05, 0x0E, 0x00, 0x38}},
        {callout_function + 0x119, {0x05, 0x00, 0x00, 0x09, 0x1C, 0x00, 0x28, 0x3B},
            {0x05, 0x05, 0x00, 0x05, 0x0D, 0x00, 0x38, 0x32}},
    };

    void patch_terminal_room(uint8_t* rdram, uint32_t buffer) {
        // Big-endian byte n of the buffer lives at RDRAM address n ^ 3.
        uint32_t base = buffer - 0x80000000u;
        auto byte = [&](uint32_t offset) -> uint8_t& { return rdram[(base + offset) ^ 3]; };
        for (const ScriptPatch& patch : terminal_room_patches) {
            for (size_t i = 0; i < patch.original.size(); i++) {
                if (byte(patch.offset + i) != patch.original[i]) {
                    std::fprintf(stderr, "Frustration-free Terminal Room: unexpected script data, not applied\n");
                    return;
                }
            }
        }
        for (const ScriptPatch& patch : terminal_room_patches) {
            for (size_t i = 0; i < patch.replacement.size(); i++) {
                byte(patch.offset + i) = patch.replacement[i];
            }
        }
        std::fprintf(stderr, "Frustration-free Terminal Room: applied\n");
    }
}

// entry: the archive entry (0x14 bytes: +0 decoded size, +0xC ROM address of the data); buffer: where it was decoded.
extern "C" void mi_override_entry(uint8_t* rdram, uint32_t entry, uint32_t buffer) {
    if (entry >= 0x80000000u && buffer >= 0x80000000u && ram_u32(rdram, entry + 0xC) == terminal_room_script_rom &&
        ram_u32(rdram, entry + 0x0) == terminal_room_script_size && mi::get_easy_terminal_room()) {
        patch_terminal_room(rdram, buffer);
    }
    static const auto overrides = load_overrides();
    if (overrides.empty() || entry < 0x80000000u || buffer < 0x80000000u) {
        return;
    }
    uint32_t rom_address = ram_u32(rdram, entry + 0xC);
    auto it = overrides.find(rom_address);
    if (it == overrides.end()) {
        return;
    }
    uint32_t size = ram_u32(rdram, entry + 0x0);
    if (it->second.size() != size) {
        std::fprintf(stderr, "Override rom_%08X.bin skipped: %zu bytes, the entry is %u\n", rom_address, it->second.size(), size);
        return;
    }
    // RDRAM keeps 32-bit words in native byte order, so byte n of a big-endian buffer lives at address n ^ 3.
    uint32_t offset = buffer - 0x80000000u;
    for (uint32_t i = 0; i < size; i++) {
        rdram[(offset + i) ^ 3] = it->second[i];
    }
    std::fprintf(stderr, "Override applied: rom_%08X.bin -> 0x%08X (%u bytes)\n", rom_address, buffer, size);
}
