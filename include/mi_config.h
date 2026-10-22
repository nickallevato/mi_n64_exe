#ifndef __MI_CONFIG_H__
#define __MI_CONFIG_H__

#include <string>

namespace mi {
    namespace configkeys {
        namespace graphics {
            inline const std::string frame_rate = "frame_rate";
        }
    }

    enum class FrameRate {
        Original, // 30 fps in levels, like the N64
        Fps60,
    };

    void init_config();
    FrameRate get_frame_rate();
}

#endif
