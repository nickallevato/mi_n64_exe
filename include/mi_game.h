#ifndef __MI_GAME_H__
#define __MI_GAME_H__

#include <string>

namespace mi {
    inline const std::string program_name = "Mission: Impossible Recompiled";
    inline const std::u8string program_id = u8"MIRecompiled";

    void register_overlays();
}

#endif
