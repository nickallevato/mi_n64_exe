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
}

#endif
