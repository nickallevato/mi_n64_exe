// Native helpers called from recompiler hooks (mi.us.toml) to implement enhancements.
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <unordered_map>

#include "mi_config.h"

// Game_LevelLoop's mode ($a2): bit 0 set for playable stages (1, 3), clear for scripted scenes (0) such as mission
// intros. Mission scenes run at 25 fps: their scripts count frames between voice lines (the game comes from a PAL
// studio), so at higher frame rates the next line starts before the current one ends (at 60 fps "Good luck, Jim" cut into
// the end of Phelps' briefing in the Ice Hit intro). The title screen's team introduction runs at 30 fps: it is paced in
// frames for 30, and at 60 it ends too soon.
static bool scene_playing = false;
static bool title_scene_playing = false;

// TitleSceneStage: the team introduction played from the title screen.
constexpr uint32_t title_scene_stage = 0x8008A880;

extern "C" void mi_level_loop_start(uint32_t mode, uint32_t stage) {
    scene_playing = (mode & 1) == 0;
    title_scene_playing = scene_playing && stage == title_scene_stage;
}

bool mi::level_loop_is_scene() {
    return scene_playing;
}

bool mi::level_loop_is_title_scene() {
    return title_scene_playing;
}

// Minimum VIs per in-level frame: 2 = original 30 fps cap, 1 = 60 fps. Mission scenes alternate 3 and 2 VIs (2.4 on
// average, 25 fps at 60 Hz); the title screen's introduction takes 2 (30 fps).
extern "C" int mi_level_min_vis_per_frame() {
    if (title_scene_playing) {
        return 2;
    }
    if (scene_playing) {
        static const int scene_vis[] = { 3, 2, 3, 2, 2 };
        static unsigned scene_frame = 0;
        return scene_vis[scene_frame++ % 5];
    }
    return mi::get_frame_rate() == mi::FrameRate::Fps60 ? 1 : 2;
}

// Game RAM stores 32-bit words in native byte order, so halfwords and bytes are at XOR'd addresses.
static uint32_t ram_u32(uint8_t* rdram, uint32_t addr) {
    return *reinterpret_cast<uint32_t*>(rdram + (addr - 0x80000000u));
}

static float ram_f32(uint8_t* rdram, uint32_t addr) {
    return *reinterpret_cast<float*>(rdram + (addr - 0x80000000u));
}

static float& ram_f32_ref(uint8_t* rdram, uint32_t addr) {
    return *reinterpret_cast<float*>(rdram + (addr - 0x80000000u));
}

static uint16_t ram_u16(uint8_t* rdram, uint32_t addr) {
    return *reinterpret_cast<uint16_t*>(rdram + ((addr - 0x80000000u) ^ 2));
}

static uint8_t ram_u8(uint8_t* rdram, uint32_t addr) {
    return rdram[(addr - 0x80000000u) ^ 3];
}

// Smooth root motion. Walking characters move by their animation's root track, one keyframe step at a time
// (Anim_GetRootDelta sums the steps between the previous and current keyframe). Animations are authored for 30 Hz
// ticks, so at 60 fps a run playing 1.25 keyframes per tick only reaches a new keyframe on about 5 frames in 8: the
// character stands still for a frame, then jumps a whole step. Add the partial step for the fraction of a keyframe
// played so far, so the motion is continuous; over whole keyframes the distance is unchanged.
namespace {
    struct RootMotionState {
        uint16_t anim_id;
        uint16_t keyframe;
        float time;
    };

    constexpr uint32_t anim_table_ptr = 0x800BA97C;
    constexpr float root_track_scale = 1.0f / 256.0f;
    std::unordered_map<uint32_t, RootMotionState> root_motion_states;
    bool root_motion_restarted = false;
}

// At the start of Anim_GetRootDelta: flag 0x80 marks an animation that has just started (the delta is then absolute).
extern "C" void mi_root_motion_begin(uint8_t* rdram, uint32_t state) {
    root_motion_restarted = (ram_u8(rdram, state + 0x7) & 0x80) != 0;
}

// At the common exit of Anim_GetRootDelta, after the X/Z delta has been stored to out+0x0 and out+0x8.
extern "C" void mi_root_motion_end(uint8_t* rdram, uint32_t state, uint32_t out) {
    uint16_t anim_id = ram_u16(rdram, state + 0x4);
    float speed = ram_f32(rdram, state + 0x8);
    float time = ram_f32(rdram, state + 0xC);
    uint16_t keyframe = ram_u16(rdram, state + 0x14);
    uint16_t prev_keyframe = ram_u16(rdram, state + 0x16);

    auto [it, inserted] = root_motion_states.try_emplace(state);
    RootMotionState last = it->second;
    it->second = { anim_id, keyframe, time };

    if (inserted || root_motion_restarted || last.anim_id != anim_id || last.keyframe != prev_keyframe ||
        speed < 0.0f || mi::get_frame_rate() != mi::FrameRate::Fps60) {
        return;
    }
    float frac = time - keyframe;
    float last_frac = last.time - last.keyframe;
    if (frac < 0.0f || frac >= 1.0f || last_frac < 0.0f || last_frac >= 1.0f) {
        return;
    }

    uint32_t entry = ram_u32(rdram, anim_table_ptr) + anim_id * 12;
    uint16_t keyframe_count = ram_u16(rdram, entry + 0x2);
    uint32_t track = ram_u32(rdram, entry + 0x4);
    if (keyframe_count == 0 || track < 0x80000000u || track >= 0x80800000u) {
        return;
    }
    // Step k moves the root from keyframe k-1 to keyframe k (6 bytes: s16 dx, y, dz).
    auto step = [&](uint32_t k, uint32_t component) {
        k = (k >= keyframe_count) ? 0 : k;
        return static_cast<float>(static_cast<int16_t>(ram_u16(rdram, track + k * 6 + component)));
    };
    ram_f32_ref(rdram, out + 0x0) += (frac * step(keyframe + 1, 0) - last_frac * step(prev_keyframe + 1, 0)) * root_track_scale;
    ram_f32_ref(rdram, out + 0x8) += (frac * step(keyframe + 1, 4) - last_frac * step(prev_keyframe + 1, 4)) * root_track_scale;
}

static int16_t& ram_s16_ref(uint8_t* rdram, uint32_t addr) {
    return *reinterpret_cast<int16_t*>(rdram + ((addr - 0x80000000u) ^ 2));
}

// Audio frame size clamp (bug fix). The audio manager sizes each frame as frameSize - osAiGetLength()/4 + extra,
// stored as s16, then clamps it to a minimum with an unsigned compare, so a negative size (more audio queued than a
// frame) slips through and the next osAiSetNextBuffer gets a ~4 GB length. Hardware never reports that much queued
// audio; the runtime's output queue can after a hitch. Clamp with a signed compare, as intended.
extern "C" void mi_audio_clamp_frame_samples(uint8_t* rdram, uint32_t audio_info) {
    constexpr uint32_t min_frame_samples = 0x800A519C;
    int16_t& frame_samples = ram_s16_ref(rdram, audio_info + 0x4);
    int32_t min_samples = static_cast<int32_t>(ram_u32(rdram, min_frame_samples));
    if (frame_samples < min_samples) {
        if (frame_samples < 0) {
            static bool reported = false;
            if (!reported) {
                reported = true;
                std::fprintf(stderr, "Audio: clamped a negative frame size (%d samples)\n", frame_samples);
            }
        }
        frame_samples = static_cast<int16_t>(min_samples);
    }
}
