#include <filesystem>

#include <fstream>

#include "recompui/config.h"
#include "recompinput/input_mapping.h"
#include "util/file.h"
#include "json/json.hpp"

#include "mi_config.h"

static void add_general_options(recomp::config::Config &config) {
    using EnumOptionVector = const std::vector<recomp::config::ConfigOptionEnumOption>;

    static EnumOptionVector analog_camera_options = {
        {mi::AnalogCamera::Off, "Off", "Off"},
        {mi::AnalogCamera::On, "On", "On"},
    };
    config.add_enum_option(
        mi::configkeys::general::analog_camera,
        "Analog Camera",
        "Turns Ethan (and the camera behind him) with the right stick, and tilts the first-person view. "
        "<recomp-color primary>Off</recomp-color> matches the original controls.",
        analog_camera_options,
        mi::AnalogCamera::On
    );
    config.add_number_option(
        mi::configkeys::general::analog_camera_speed,
        "Analog Camera Speed",
        "How fast the right stick turns Ethan, in degrees per second at full tilt.",
        60, 360, 10, 0, false, 180
    );
    config.add_option_hidden_dependency(
        mi::configkeys::general::analog_camera_speed,
        mi::configkeys::general::analog_camera,
        mi::AnalogCamera::Off
    );
}

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

template <typename T = uint32_t>
T get_general_config_enum_value(const std::string& option_id) {
    return static_cast<T>(std::get<uint32_t>(recompui::config::get_general_config().get_option_value(option_id)));
}

bool mi::get_analog_camera_enabled() {
    return get_general_config_enum_value<mi::AnalogCamera>(mi::configkeys::general::analog_camera) == mi::AnalogCamera::On;
}

float mi::get_analog_camera_speed() {
    return static_cast<float>(std::get<double>(recompui::config::get_general_config().get_option_value(mi::configkeys::general::analog_camera_speed)));
}

mi::FrameRate mi::get_frame_rate() {
    return get_graphics_config_enum_value<mi::FrameRate>(mi::configkeys::graphics::frame_rate);
}

// Keyboard + mouse defaults (only used until the player saves their own bindings).
static void set_control_defaults() {
    using namespace recompinput;
    auto mouse = [](uint8_t button) { return InputField{ InputType::Mouse, button }; };
    set_default_mapping_for_keyboard(GameInput::Z, { mouse(SDL_BUTTON_LEFT), InputField::keyboard(SDL_SCANCODE_Q) });
    set_default_mapping_for_keyboard(GameInput::R, { mouse(SDL_BUTTON_RIGHT), InputField::keyboard(SDL_SCANCODE_E) });
    set_default_mapping_for_keyboard(GameInput::C_DOWN, { InputField::keyboard(SDL_SCANCODE_LCTRL), InputField::keyboard(SDL_SCANCODE_DOWN) });

    // The right stick drives the analog camera, so keep it off the C buttons (sidestep/zoom/crouch).
    set_default_mapping_for_controller(GameInput::C_LEFT, { InputField::controller_digital(SDL_CONTROLLER_BUTTON_NORTH) });
    set_default_mapping_for_controller(GameInput::C_RIGHT, { InputField::controller_digital(SDL_CONTROLLER_BUTTON_EAST) });
    set_default_mapping_for_controller(GameInput::C_UP, { InputField::controller_digital(SDL_CONTROLLER_BUTTON_RIGHTSTICK) });
    set_default_mapping_for_controller(GameInput::C_DOWN, { InputField::controller_digital(SDL_CONTROLLER_BUTTON_RIGHTSHOULDER) });
}

static void set_control_descriptions() {
    using namespace recompinput;
    set_game_input_description(GameInput::Z, "Shoot or punch.");
    set_game_input_description(GameInput::R, "Hold for first-person view and aiming. The mouse aims while it's held.");
    set_game_input_description(GameInput::A, "Action (open doors, push switches, read messages). Jumps by default.");
    set_game_input_description(GameInput::B, "Inventory: cycle weapons.");
    set_game_input_description(GameInput::L, "Switch camera mode.");
    set_game_input_description(GameInput::C_UP, "While aiming: zoom.");
    set_game_input_description(GameInput::C_DOWN, "Crouch / stand up. Also opens the item inventory.");
    set_game_input_description(GameInput::C_LEFT, "While aiming: sidestep left.");
    set_game_input_description(GameInput::C_RIGHT, "While aiming: sidestep right.");
    set_game_input_description(GameInput::DPAD_LEFT, "Rotate the camera around Ethan.");
    set_game_input_description(GameInput::DPAD_RIGHT, "Rotate the camera around Ethan.");
    set_game_input_description(GameInput::START, "Pause and open the IMF menu.");
}

// RecompFrontend's mouse sensitivity defaults to 0 (mouse aiming off) outside the Steam Deck. Turn it on for new
// installs by giving it a default when the saved general config doesn't have a value yet.
static void apply_mouse_default() {
    std::ifstream file{ recompui::file::get_app_folder_path() / "general.json" };
    nlohmann::json saved = nlohmann::json::object();
    if (file.good()) {
        try {
            file >> saved;
        }
        catch (...) {
            saved = nlohmann::json::object();
        }
    }
    if (!saved.contains(recompui::config::general::options::mouse_sensitivity)) {
        recompui::config::get_general_config().set_option_value(recompui::config::general::options::mouse_sensitivity, 50.0);
    }
}

void mi::init_config() {
    std::filesystem::path recomp_dir = recompui::file::get_app_folder_path();
    if (!recomp_dir.empty()) {
        std::filesystem::create_directories(recomp_dir);
    }

    recompui::config::GeneralTabOptions general_options{};
    general_options.has_mouse_sensitivity = true;
    auto &general_config = recompui::config::create_general_tab(general_options);
    add_general_options(general_config);

    auto &graphics_config = recompui::config::create_graphics_tab();
    add_graphics_options(graphics_config);

    set_control_defaults();
    set_control_descriptions();
    recompui::config::create_controls_tab();
    recompui::config::create_sound_tab();
    recompui::config::create_mods_tab();
    recompui::config::finalize();
    apply_mouse_default();
}
