// Mouse aiming: accumulates mouse motion between game frames and hands it to recompiler hooks in the player's
// aim code (see mi.us.toml), which add it directly to the aim angles.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <mutex>

#include "recompinput/input_state.h"
#include "recompinput/recompinput.h"
#include "recompui/config.h"

#include "mi_game.h"

namespace {
    std::mutex mouse_mutex;
    float pending_x = 0.0f;
    float pending_y = 0.0f;

    // Radians per mouse count at 100% sensitivity.
    constexpr float radians_per_count = 0.006f;
    // The game clamps aim pitch to +-85 degrees.
    constexpr float max_pitch = 85.0f * 3.14159265f / 180.0f;

    float sensitivity_scale() {
        if (!recompui::config::general::has_mouse_sensitivity_option()) {
            return 0.0f;
        }
        return static_cast<float>(recompui::config::general::get_mouse_sensitivity()) / 100.0f * radians_per_count;
    }

    float take(float& pending) {
        std::lock_guard lock{ mouse_mutex };
        float value = pending;
        pending = 0.0f;
        return value;
    }
}

// Called after every controller poll with recompinput's mouse motion since the previous poll, plus any test motion
// injected through MI_INPUT.
void mi::mouse_on_poll(float test_dx, float test_dy) {
    float dx, dy;
    recompinput::get_mouse_deltas(&dx, &dy);
    dx += test_dx;
    dy += test_dy;
    std::lock_guard lock{ mouse_mutex };
    if (recompinput::game_input_disabled()) {
        // A menu has the mouse; don't let its motion turn the player afterwards.
        pending_x = pending_y = 0.0f;
        return;
    }
    // Bound the backlog so a stall can't produce a huge snap.
    pending_x = std::clamp(pending_x + dx, -4000.0f, 4000.0f);
    pending_y = std::clamp(pending_y + dy, -4000.0f, 4000.0f);
}

// Game RAM access for native code (rdram holds 32-bit words in native byte order).
static uint32_t& ram_word(uint8_t* rdram, uint32_t addr) {
    return *reinterpret_cast<uint32_t*>(rdram + (addr - 0x80000000u));
}

static float& ram_float(uint8_t* rdram, uint32_t addr) {
    return *reinterpret_cast<float*>(rdram + (addr - 0x80000000u));
}

constexpr uint32_t player_object_ptr = 0x80093D58; // Pointer to Ethan's object; yaw (degrees) at +0x44.
constexpr uint32_t player_state_flags = 0x80093EF4;
constexpr uint32_t player_state_aiming = 0x100000;  // Player_AimControl handles the mouse while this is set.
constexpr uint32_t player_state_first_person = 0x4; // R held: first-person view, stick Y tilts the view.

// Stick deflection per mouse count of vertical motion in first-person view (at 100% sensitivity).
constexpr float stick_per_count = 0.25f;

// In first-person view the game only tilts the view through stick Y, so vertical mouse motion is turned into stick
// deflection for this poll.
void mi::mouse_adjust_stick(float* x, float* y) {
    uint8_t* rdram = mi::game_rdram();
    if (rdram == nullptr || recompinput::game_input_disabled()) {
        return;
    }
    uint32_t flags = ram_word(rdram, player_state_flags);
    if (!(flags & player_state_first_person) || (flags & player_state_aiming)) {
        return;
    }
    float scale = recompui::config::general::has_mouse_sensitivity_option()
        ? static_cast<float>(recompui::config::general::get_mouse_sensitivity()) / 100.0f : 0.0f;
    // Mouse down (positive y) moves the crosshair down, which is positive stick Y in this view.
    float tilt = take(pending_y) * stick_per_count * scale;
    *y = std::clamp(*y + tilt, -1.0f, 1.0f);
}

// Called at the start of Player_Control every frame. Outside aim mode the mouse turns Ethan directly.
extern "C" void mi_mouse_player_control(uint8_t* rdram) {
    if (ram_word(rdram, player_state_flags) & player_state_aiming) {
        return;
    }
    // Outside first-person view there's no camera pitch, so drop vertical motion.
    if (!(ram_word(rdram, player_state_flags) & player_state_first_person)) {
        take(pending_y);
    }
    float yaw_radians = take(pending_x) * sensitivity_scale();
    uint32_t player = ram_word(rdram, player_object_ptr);
    if (yaw_radians == 0.0f || player < 0x80000000u || player >= 0x80800000u) {
        return;
    }
    float yaw = ram_float(rdram, player + 0x44) - yaw_radians * (180.0f / 3.14159265f);
    ram_float(rdram, player + 0x44) = std::fmod(yaw + 360.0f, 360.0f);
}

// Aim yaw change in radians (positive turns left, like the game's angle convention).
extern "C" float mi_mouse_take_aim_yaw() {
    return -take(pending_x) * sensitivity_scale();
}

// Applies vertical mouse motion to the aim pitch (radians) and clamps it like the game does.
extern "C" float mi_mouse_apply_aim_pitch(float pitch) {
    pitch -= take(pending_y) * sensitivity_scale();
    return std::clamp(pitch, -max_pitch, max_pitch);
}
