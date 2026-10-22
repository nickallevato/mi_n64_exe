#include <filesystem>

#include "recompui/config.h"
#include "util/file.h"

#include "mi_config.h"

static void add_graphics_options(recomp::config::Config &config) {
    using EnumOptionVector = const std::vector<recomp::config::ConfigOptionEnumOption>;

    static EnumOptionVector frame_rate_options = {
        {mi::FrameRate::Original, "Original", "Original (30)"},
        {mi::FrameRate::Fps60, "60", "60 FPS"},
    };
    config.add_enum_option(
        mi::configkeys::graphics::frame_rate,
        "Frame Rate",
        "Frame rate cap during gameplay and in-engine cutscenes. The game times its logic by the video clock, so "
        "<recomp-color primary>60 FPS</recomp-color> plays at normal speed. <recomp-color primary>Original</recomp-color> "
        "matches the N64's 30 FPS cap.",
        frame_rate_options,
        mi::FrameRate::Fps60
    );
}

template <typename T = uint32_t>
T get_graphics_config_enum_value(const std::string& option_id) {
    return static_cast<T>(std::get<uint32_t>(recompui::config::get_graphics_config().get_option_value(option_id)));
}

mi::FrameRate mi::get_frame_rate() {
    return get_graphics_config_enum_value<mi::FrameRate>(mi::configkeys::graphics::frame_rate);
}

void mi::init_config() {
    std::filesystem::path recomp_dir = recompui::file::get_app_folder_path();
    if (!recomp_dir.empty()) {
        std::filesystem::create_directories(recomp_dir);
    }

    recompui::config::create_general_tab(recompui::config::GeneralTabOptions{});

    auto &graphics_config = recompui::config::create_graphics_tab();
    add_graphics_options(graphics_config);

    recompui::config::create_controls_tab();
    recompui::config::create_sound_tab();
    recompui::config::create_mods_tab();
    recompui::config::finalize();
}
