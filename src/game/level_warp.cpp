// Debug level warp: MI_LEVEL boots straight into a stage, skipping the logos, intro, title, save select, difficulty,
// mission menus and the pre-mission OBJECTIVES/BRIEFING/TEAM screens. For iterating on level edits.
//
//   MI_LEVEL=<mission>,<stage>[,<difficulty>]   indices as printed by scripts/levels/level_table.py, e.g. 1,0 = Recover
//                                              NOC List: Embassy Function; difficulty 0 = POSSIBLE (default), 1 = IMPOSSIBLE
//   MI_LEVEL=<stage name>[,<difficulty>]       e.g. "Train roof" (first match, case-insensitive), or
//                                              "<mission name>/<stage name>", e.g. "Ice Storm/Subpen"
//
// Without MI_LEVEL, Debug > Start in Level (General > Developer Tools) picks the stage: at launch, and again when the
// game returns to the title screen if the choice changed since the last warp (so finishing a mission still reaches
// the title).
//
// How: a hook in Game_ThreadMain, right after boot init and before the first FrontEnd_Title, sets up the same state the
// menus would with no save device (all save slots unused, so nothing is ever written to the EEPROM), the difficulty,
// CurMission and CurStage, then calls Game_RunMission like Game_MissionSelect does. Until the stage starts, hooks in
// Game_RunMission skip the mission intro scene, Mission_BriefingMenu and the in-level objectives screen. The rest of the
// mission then plays normally; when it ends or is aborted the game continues to the title screen.
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "recomp.h"
#include "mi_config.h"
#include "mi_game.h"

extern "C" {
    void Game_RunMission(uint8_t* rdram, recomp_context* ctx);
    void Save_ProbeSlots(uint8_t* rdram, recomp_context* ctx);
    void Difficulty_Set(uint8_t* rdram, recomp_context* ctx);
    void Progress_ApplyUnlocks(uint8_t* rdram, recomp_context* ctx);
    void func_80035E6C(uint8_t* rdram, recomp_context* ctx);
    void func_80035E04(uint8_t* rdram, recomp_context* ctx);
}

namespace {
    constexpr uint32_t MISSION_TABLE = 0x8008AB28;
    constexpr uint32_t CUR_MISSION = 0x800BADD8;
    constexpr uint32_t CUR_STAGE = 0x800A9868;
    constexpr uint32_t UI_STRINGS = 0x800A9508;
    constexpr uint32_t INPUT_WAIT_RELEASE = 0x8008AC2A;
    constexpr uint32_t ENTRY_SIZE = 0x14;

    // RDRAM keeps 32-bit words in native byte order: u16 at addr ^ 2, u8 at addr ^ 3.
    uint32_t ram_u32(uint8_t* rdram, uint32_t addr) { return *reinterpret_cast<uint32_t*>(rdram + (addr - 0x80000000u)); }
    uint16_t ram_u16(uint8_t* rdram, uint32_t addr) { return *reinterpret_cast<uint16_t*>(rdram + ((addr ^ 2) - 0x80000000u)); }
    uint8_t ram_u8(uint8_t* rdram, uint32_t addr) { return rdram[(addr ^ 3) - 0x80000000u]; }
    void ram_w8(uint8_t* rdram, uint32_t addr, uint8_t v) { rdram[(addr ^ 3) - 0x80000000u] = v; }

    bool in_ram(uint32_t addr) { return addr >= 0x80000000u && addr < 0x80800000u; }

    // Missions end at the first entry with no name (as Game_MissionSelect counts them); stage lists end at the first
    // entry without an archive (as Game_RunMission stops).
    int mission_count(uint8_t* rdram) {
        int n = 0;
        while (n < 16 && ram_u16(rdram, MISSION_TABLE + n * ENTRY_SIZE) != 0) n++;
        return n;
    }
    uint32_t stage_list(uint8_t* rdram, int mission) { return ram_u32(rdram, MISSION_TABLE + mission * ENTRY_SIZE + 0x10); }
    int stage_count(uint8_t* rdram, int mission) {
        uint32_t list = stage_list(rdram, mission);
        int n = 0;
        while (in_ram(list) && n < 64 && ram_u32(rdram, list + n * ENTRY_SIZE + 4) != 0 && ram_u32(rdram, list + n * ENTRY_SIZE + 8) != 0) n++;
        return n;
    }

    // Name of a table entry from the UI string table (loaded by Text_Init during boot).
    std::string entry_name(uint8_t* rdram, uint32_t entry) {
        uint32_t s = ram_u32(rdram, UI_STRINGS);
        if (!in_ram(s)) return "?";
        s += ram_u16(rdram, entry);
        std::string out;
        for (uint8_t c; in_ram(s) && (c = ram_u8(rdram, s)) != 0 && out.size() < 64; s++) out += static_cast<char>(c);
        return out;
    }

    bool iequals(const std::string& a, const std::string& b) {
        if (a.size() != b.size()) return false;
        for (size_t i = 0; i < a.size(); i++) {
            if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i]))) return false;
        }
        return true;
    }

    std::string trim(const std::string& s) {
        size_t b = s.find_first_not_of(" \t"), e = s.find_last_not_of(" \t");
        return b == std::string::npos ? "" : s.substr(b, e - b + 1);
    }

    bool parse_int(const std::string& s, int& out) {
        if (s.empty()) return false;
        char* end = nullptr;
        long v = std::strtol(s.c_str(), &end, 0);
        if (*end != '\0') return false;
        out = static_cast<int>(v);
        return true;
    }

    // Finds "stage" or "mission/stage" by name; returns false if nothing matches.
    bool find_by_name(uint8_t* rdram, const std::string& spec, int& mission, int& stage) {
        std::string mission_name, stage_name = spec;
        if (size_t slash = spec.find('/'); slash != std::string::npos) {
            mission_name = trim(spec.substr(0, slash));
            stage_name = trim(spec.substr(slash + 1));
        }
        for (int m = 0; m < mission_count(rdram); m++) {
            if (!mission_name.empty() && !iequals(mission_name, entry_name(rdram, MISSION_TABLE + m * ENTRY_SIZE))) continue;
            for (int s = 0; s < stage_count(rdram, m); s++) {
                if (iequals(stage_name, entry_name(rdram, stage_list(rdram, m) + s * ENTRY_SIZE))) {
                    mission = m;
                    stage = s;
                    return true;
                }
            }
        }
        return false;
    }

    // Set from the warp until the warped stage starts: Game_RunMission skips its menus while this is set.
    bool warp_pending = false;
}

// Parses MI_LEVEL into mission, stage and difficulty; logs and returns false if it's invalid.
static bool parse_level_spec(uint8_t* rdram, const char* spec, int& mission, int& stage, int& difficulty) {
    std::vector<std::string> parts;
    std::string s = spec;
    for (size_t start = 0;;) {
        size_t comma = s.find(',', start);
        parts.push_back(trim(s.substr(start, comma - start)));
        if (comma == std::string::npos) break;
        start = comma + 1;
    }

    mission = -1;
    stage = -1;
    difficulty = 0;
    bool ok;
    if (int m; parse_int(parts[0], m)) {
        mission = m;
        ok = parts.size() >= 2 && parts.size() <= 3 && parse_int(parts[1], stage) &&
             (parts.size() < 3 || parse_int(parts[2], difficulty));
    } else {
        ok = parts.size() <= 2 && (parts.size() < 2 || parse_int(parts[1], difficulty));
        if (ok && !find_by_name(rdram, parts[0], mission, stage)) {
            std::fprintf(stderr, "Level warp: no stage named \"%s\" (see scripts/levels/level_table.py); booting normally\n", parts[0].c_str());
            return false;
        }
    }
    if (!ok) {
        std::fprintf(stderr, "Level warp: can't parse MI_LEVEL=\"%s\" (expected <mission>,<stage>[,<difficulty>] or a stage name); booting normally\n", spec);
        return false;
    }
    return true;
}

// Sets up the menus' state and plays the mission from the given stage; returns when the mission is over.
static void warp_to(uint8_t* rdram, recomp_context* ctx, int mission, int stage, int difficulty) {
    int missions = mission_count(rdram);
    if (mission < 0 || mission >= missions) {
        std::fprintf(stderr, "Level warp: mission %d out of range (0-%d); booting normally\n", mission, missions - 1);
        return;
    }
    int stages = stage_count(rdram, mission);
    if (stage < 0 || stage >= stages) {
        std::fprintf(stderr, "Level warp: stage %d out of range for mission %d (0-%d); booting normally\n", stage, mission, stages - 1);
        return;
    }
    if (difficulty < 0 || difficulty > 1) {
        std::fprintf(stderr, "Level warp: difficulty %d out of range (0 = POSSIBLE, 1 = IMPOSSIBLE); booting normally\n", difficulty);
        return;
    }
    uint32_t entry = stage_list(rdram, mission) + stage * ENTRY_SIZE;
    std::fprintf(stderr, "Level warp: mission %d (%s) stage %d (%s, flags 0x%02X, archive 0x%06X), difficulty %d\n", mission,
        entry_name(rdram, MISSION_TABLE + mission * ENTRY_SIZE).c_str(), stage, entry_name(rdram, entry).c_str(),
        ram_u8(rdram, entry + 2), ram_u32(rdram, entry + 4), difficulty);

    // Own stack frame below Game_ThreadMain's: the callee's argument save area, plus the u16 stage counter that
    // Game_MissionSelect passes to Game_RunMission at 0x1A($sp).
    gpr sp = ctx->r29;
    ctx->r29 = sp - 0x20;
    MEM_H(0x1A, ctx->r29) = 0;

    // Save select with no save device: every slot unused, so progress is never written to the EEPROM.
    Save_ProbeSlots(rdram, ctx);
    // Difficulty menu: set the difficulty and the stage unlocks it implies (none for an empty profile).
    ctx->r4 = difficulty;
    Difficulty_Set(rdram, ctx);
    Progress_ApplyUnlocks(rdram, ctx);
    // Mission select: store the choice, free the menu fonts and play the mission from that stage.
    ram_w8(rdram, CUR_MISSION, static_cast<uint8_t>(mission));
    ram_w8(rdram, CUR_STAGE, static_cast<uint8_t>(stage));
    ram_w8(rdram, INPUT_WAIT_RELEASE, 1);
    func_80035E6C(rdram, ctx);
    func_80035E04(rdram, ctx);
    warp_pending = true;
    ctx->r4 = 1;
    ctx->r5 = 1;
    ctx->r6 = ctx->r29 + 0x1A;
    Game_RunMission(rdram, ctx);
    warp_pending = false;

    ctx->r29 = sp;
    std::fprintf(stderr, "Level warp: mission over, continuing to the title screen\n");
}

// Debug > Unlock All Levels: set the unlocked flag (0x01) on every stage the mission menu can show, like the
// GameShark level codes. Hidden stages (flags 0xE0: cutscenes, continuations, test levels) stay hidden.
void mi::levels_on_vi() {
    uint8_t* rdram = mi::game_rdram();
    if (rdram == nullptr || !mi::get_unlock_all_levels()) {
        return;
    }
    for (int m = 0; m < mission_count(rdram); m++) {
        for (int s = 0; s < stage_count(rdram, m); s++) {
            uint32_t flags = stage_list(rdram, m) + s * ENTRY_SIZE + 2;
            uint8_t value = ram_u8(rdram, flags);
            if ((value & 0xE0) == 0 && (value & 0x01) == 0) {
                ram_w8(rdram, flags, value | 0x01);
            }
        }
    }
}

// Game_ThreadMain, after boot init, before the front end (and again each time the front end loop restarts).
extern "C" void mi_level_warp(uint8_t* rdram, recomp_context* ctx) {
    static bool env_checked = false;
    static bool warped = false;
    static mi::LevelChoice last_choice{-1, -1};

    if (!env_checked) {
        env_checked = true;
        const char* spec = std::getenv("MI_LEVEL");
        if (spec != nullptr && *spec != '\0') {
            int mission, stage, difficulty;
            if (parse_level_spec(rdram, spec, mission, stage, difficulty)) {
                warped = true;
                warp_to(rdram, ctx, mission, stage, difficulty);
            }
            return;
        }
    }

    mi::LevelChoice choice;
    if (!mi::get_start_level(choice)) {
        return;
    }
    if (warped && choice.mission == last_choice.mission && choice.stage == last_choice.stage) {
        return;
    }
    warped = true;
    last_choice = choice;
    warp_to(rdram, ctx, choice.mission, choice.stage, 0);
}

// True while Game_RunMission should skip the mission intro, Mission_BriefingMenu and the in-level objectives screen.
extern "C" int mi_level_warp_skip_menus(void) {
    return warp_pending ? 1 : 0;
}

// Game_LevelLoop entry: the warped stage (or the scene it starts with) is loading; later stages play normally.
extern "C" void mi_level_warp_level_started(void) {
    warp_pending = false;
}
