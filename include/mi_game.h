#ifndef __MI_GAME_H__
#define __MI_GAME_H__

#include <cstddef>
#include <cstdint>
#include <string>

namespace mi {
    inline const std::string program_name = "Mission: Impossible Recompiled";
    inline const std::u8string program_id = u8"MIRecompiled";

    void register_overlays();
    void stats_on_vi();
    void stats_on_audio(size_t sample_count);
    void stats_set_rdram(uint8_t* rdram);
    void mouse_on_poll(float test_dx, float test_dy);
    void set_test_right_analog(float x, float y);
    // Adjusts controller 1's stick for mouse input that the game only accepts through the stick.
    void mouse_adjust_stick(float* x, float* y);
    // Game RAM, once the renderer has been created (nullptr before).
    uint8_t* game_rdram();
}

#endif
