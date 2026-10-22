#ifndef __MI_CONFIG_H__
#define __MI_CONFIG_H__

#include <string>

namespace mi {
    namespace configkeys {
        namespace general {
            inline const std::string analog_camera = "analog_camera";
            inline const std::string analog_camera_speed = "analog_camera_speed";
        }
        namespace graphics {
            inline const std::string frame_rate = "frame_rate";
        }
    }

    enum class FrameRate {
        Original, // 30 fps in levels, like the N64
        Fps60,
    };

    enum class AnalogCamera {
        Off,
        On,
    };

    void init_config();
    FrameRate get_frame_rate();
    bool get_analog_camera_enabled();
    // Degrees per second at full right stick deflection.
    float get_analog_camera_speed();
}

#endif
