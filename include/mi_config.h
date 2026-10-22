#ifndef __MI_CONFIG_H__
#define __MI_CONFIG_H__

#include <string>

namespace mi {
    namespace configkeys {
        namespace general {
            inline const std::string analog_camera = "analog_camera";
            inline const std::string analog_camera_speed = "analog_camera_speed";
            inline const std::string developer_tools = "developer_tools";
        }
        namespace debug {
            inline const std::string id = "debug";
            inline const std::string start_level = "start_level";
            inline const std::string unlock_all_levels = "unlock_all_levels";
            inline const std::string load_overrides = "load_overrides";
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

    enum class DeveloperTools {
        Off,
        On,
    };

    // A level to start in (Debug > Start in Level): indices into the mission table and the mission's stage list.
    struct LevelChoice {
        int mission;
        int stage;
    };

    void init_config();
    FrameRate get_frame_rate();
    bool get_analog_camera_enabled();
    // Degrees per second at full right stick deflection.
    float get_analog_camera_speed();

    // Debug tab (General > Developer Tools). Each returns the off state while Developer Tools is off.
    bool get_start_level(LevelChoice& out);
    bool get_unlock_all_levels();
    bool get_load_overrides();
    // Folder that Debug > Load Data Overrides reads (<app folder>/overrides).
    std::string get_overrides_dir();
}

#endif
