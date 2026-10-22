// Development stats, printed once per second:
//   MI_STATS=1   graphics tasks the game submitted, VIs (retraces) the runtime generated and audio samples queued.
//   MI_WATCH=... game RAM words, as comma-separated "ADDR[:COUNT]" (hex). "*ADDR+OFF" reads the pointer at ADDR
//                and adds OFF, e.g. "*0x8009FE38+0x6C:4" for 4 words starting at a struct field.
//   MI_DUMP_DIR=dir  write the first 8 MB of game RAM to dir/ram_<second>.bin (native word order).
// Applied every VI rather than once per second:
//   MI_POKE=ADDR=VALUE,...  GameShark-style byte writes (hex), e.g. "0x8008A846=1" enables a level.
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "mi_game.h"

static const bool stats_enabled = std::getenv("MI_STATS") != nullptr;
static std::atomic<uint32_t> gfx_tasks{0};
static std::atomic<uint32_t> vis{0};
static std::atomic<uint32_t> audio_samples{0};
static const char* watch_spec = std::getenv("MI_WATCH");
static const char* dump_dir = std::getenv("MI_DUMP_DIR");
static const char* poke_spec = std::getenv("MI_POKE");
static uint8_t* stats_rdram = nullptr;

static void apply_pokes() {
    std::string spec = poke_spec;
    size_t start = 0;
    while (start < spec.size()) {
        size_t end = spec.find(',', start);
        std::string item = spec.substr(start, end == std::string::npos ? std::string::npos : end - start);
        start = end == std::string::npos ? spec.size() : end + 1;
        size_t eq = item.find('=');
        if (eq == std::string::npos) {
            continue;
        }
        uint32_t offset = static_cast<uint32_t>(std::strtoul(item.c_str(), nullptr, 16)) - 0x80000000u;
        if (offset < 0x800000u) {
            // Bytes are stored with the address XOR'd by 3 (words are in native order).
            stats_rdram[offset ^ 3] = static_cast<uint8_t>(std::strtoul(item.c_str() + eq + 1, nullptr, 16));
        }
    }
}

void mi::stats_set_rdram(uint8_t* rdram) {
    stats_rdram = rdram;
}

uint8_t* mi::game_rdram() {
    return stats_rdram;
}

// Reads a 32-bit word of game RAM (rdram stores words in native byte order). Returns false if out of range.
static bool read_word(uint32_t addr, uint32_t& out) {
    uint32_t offset = addr - 0x80000000u;
    if (stats_rdram == nullptr || offset >= 0x800000u || (offset & 3) != 0) {
        return false;
    }
    out = *reinterpret_cast<uint32_t*>(stats_rdram + offset);
    return true;
}

static void print_watches() {
    std::string spec = watch_spec;
    size_t start = 0;
    while (start < spec.size()) {
        size_t end = spec.find(',', start);
        std::string item = spec.substr(start, end == std::string::npos ? std::string::npos : end - start);
        start = end == std::string::npos ? spec.size() : end + 1;

        uint32_t count = 1;
        size_t colon = item.find(':');
        if (colon != std::string::npos) {
            count = std::strtoul(item.c_str() + colon + 1, nullptr, 0);
            item.resize(colon);
        }
        uint32_t addr;
        if (item[0] == '*') {
            size_t plus = item.find('+');
            uint32_t ptr;
            if (!read_word(std::strtoul(item.c_str() + 1, nullptr, 0), ptr)) {
                continue;
            }
            addr = ptr + (plus == std::string::npos ? 0 : std::strtoul(item.c_str() + plus + 1, nullptr, 0));
        }
        else {
            addr = std::strtoul(item.c_str(), nullptr, 0);
        }
        fprintf(stderr, "[watch] %08X:", addr);
        for (uint32_t i = 0; i < count; i++) {
            uint32_t value;
            if (read_word(addr + i * 4, value)) {
                fprintf(stderr, " %08X", value);
            }
        }
        fprintf(stderr, "\n");
    }
}

// Called from a recompiler hook at the start of the game's gfx task submit function (see mi.us.toml).
extern "C" void mi_stats_gfx_submit() {
    gfx_tasks++;
}

void mi::stats_on_audio(size_t sample_count) {
    audio_samples += static_cast<uint32_t>(sample_count);
}

static void dump_ram() {
    static uint32_t index = 0;
    std::string path = std::string(dump_dir) + "/ram_" + std::to_string(index++) + ".bin";
    if (FILE* f = std::fopen(path.c_str(), "wb")) {
        std::fwrite(stats_rdram, 1, 0x800000, f);
        std::fclose(f);
    }
}

void mi::stats_on_vi() {
    if (poke_spec != nullptr && stats_rdram != nullptr) {
        apply_pokes();
    }
    if (!stats_enabled && watch_spec == nullptr && dump_dir == nullptr) {
        return;
    }
    vis++;
    static auto last = std::chrono::steady_clock::now();
    auto now = std::chrono::steady_clock::now();
    if (now - last >= std::chrono::seconds(1)) {
        last = now;
        if (stats_enabled) {
            fprintf(stderr, "[stats] gfx tasks/s: %u  VIs/s: %u  audio samples/s: %u\n",
                gfx_tasks.exchange(0), vis.exchange(0), audio_samples.exchange(0));
        }
        if (watch_spec != nullptr) {
            print_watches();
        }
        if (dump_dir != nullptr && stats_rdram != nullptr) {
            dump_ram();
        }
    }
}
