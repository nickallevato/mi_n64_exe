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

    static EnumOptionVector developer_tools_options = {
        {mi::DeveloperTools::Off, "Off", "Off"},
        {mi::DeveloperTools::On, "On", "On"},
    };
    config.add_enum_option(
        mi::configkeys::general::developer_tools,
        "Developer Tools",
        "Shows the <recomp-color primary>Debug</recomp-color> tab: start straight in any level, unlock all levels and "
        "load edited level data.",
        developer_tools_options,
        mi::DeveloperTools::Off
    );
}

// Levels listed in the mission menu, in menu order (indices into the mission table D_8008AB28 and each mission's stage
// list, as printed by scripts/levels/level_table.py; hidden test and cutscene stages are left out).
struct StartLevel {
    const char* key;
    const char* name;
    mi::LevelChoice choice;
};

static const std::vector<StartLevel> start_levels = {
    {"ice_hit_lundkwist_base", "Ice Hit: Lundkwist Base", {0, 0}},
    {"ice_hit_subpen", "Ice Hit: Subpen", {0, 1}},
    {"noc_embassy_function", "Recover NOC List: Embassy Function", {1, 0}},
    {"noc_warehouse", "Recover NOC List: Warehouse", {1, 1}},
    {"noc_kgb_hq", "Recover NOC List: K.G.B. HQ", {1, 2}},
    {"noc_security_hallway", "Recover NOC List: Security Hallway", {1, 3}},
    {"noc_sewage_control", "Recover NOC List: Sewage Control", {1, 4}},
    {"noc_escape", "Recover NOC List: Escape", {1, 5}},
    {"noc_fire_alarm", "Recover NOC List: Fire Alarm", {1, 7}},
    {"cia_interrogation", "CIA Escape: Interrogation", {2, 0}},
    {"cia_rooftop", "CIA Escape: CIA Rooftop", {2, 4}},
    {"cia_terminal_room", "CIA Escape: Terminal Room", {2, 6}},
    {"cia_rooftop_escape", "CIA Escape: Rooftop Escape", {2, 8}},
    {"mole_station", "Mole Hunt: Station", {3, 0}},
    {"mole_train_car", "Mole Hunt: Train car", {3, 1}},
    {"mole_train_roof", "Mole Hunt: Train roof", {3, 3}},
    {"ice_storm_subpen", "Ice Storm: Subpen", {4, 0}},
    {"ice_storm_tunnel", "Ice Storm: Tunnel", {4, 1}},
    {"ice_storm_mainland", "Ice Storm: Mainland", {4, 3}},
    {"ice_storm_gunboat", "Ice Storm: Gunboat", {4, 4}},
};

static void add_debug_options(recomp::config::Config &config) {
    // Value 0 is Off; value i + 1 is start_levels[i].
    static std::vector<recomp::config::ConfigOptionEnumOption> start_level_options = [] {
        std::vector<recomp::config::ConfigOptionEnumOption> options = {{0u, "off", "Off"}};
        for (size_t i = 0; i < start_levels.size(); i++) {
            options.push_back({static_cast<uint32_t>(i + 1), start_levels[i].key, start_levels[i].name});
        }
        return options;
    }();
    config.add_enum_option(
        mi::configkeys::debug::start_level,
        "Start in Level",
        "Skips the intro and menus and starts straight in this level, at launch and whenever the game gets back to the "
        "title screen after you change it. The level's own opening still plays. Progress isn't saved.",
        start_level_options,
        0u
    );
    config.add_bool_option(
        mi::configkeys::debug::unlock_all_levels,
        "Unlock All Levels",
        "Shows every level in the mission menu.",
        false
    );
    config.add_bool_option(
        mi::configkeys::debug::load_overrides,
        "Load Data Overrides",
        "Loads edited game data (<b>rom_XXXXXXXX.bin</b> files made by scripts/levels/level_mod.py) from the "
        "<b>overrides</b> folder next to your settings. Takes effect at the next launch.",
        false
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

static bool developer_tools_enabled() {
    return get_general_config_enum_value<mi::DeveloperTools>(mi::configkeys::general::developer_tools) == mi::DeveloperTools::On;
}

static bool debug_config_created = false;

static recomp::config::Config& debug_config() {
    return recompui::config::get_config(mi::configkeys::debug::id);
}

bool mi::get_start_level(mi::LevelChoice& out) {
    if (!debug_config_created || !developer_tools_enabled()) {
        return false;
    }
    uint32_t value = std::get<uint32_t>(debug_config().get_option_value(mi::configkeys::debug::start_level));
    if (value == 0 || value > start_levels.size()) {
        return false;
    }
    out = start_levels[value - 1].choice;
    return true;
}

bool mi::get_unlock_all_levels() {
    return debug_config_created && developer_tools_enabled() &&
        std::get<bool>(debug_config().get_option_value(mi::configkeys::debug::unlock_all_levels));
}

bool mi::get_load_overrides() {
    return debug_config_created && developer_tools_enabled() &&
        std::get<bool>(debug_config().get_option_value(mi::configkeys::debug::load_overrides));
}

std::string mi::get_overrides_dir() {
    return (recompui::file::get_app_folder_path() / "overrides").string();
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
    // RecompFrontend's default puts L on E too, so aiming with E would also switch the camera mode.
    set_default_mapping_for_keyboard(GameInput::L, { InputField::keyboard(SDL_SCANCODE_V) });
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
    // Creating a tab can move the other tabs' configs, so look configs up by id after this point.
    add_debug_options(recompui::config::create_config_tab("Debug", mi::configkeys::debug::id, false));
    recompui::config::finalize();
    debug_config_created = true;
    apply_mouse_default();

    // The Debug tab only shows while General > Developer Tools is on.
    recompui::config::set_tab_visible(mi::configkeys::debug::id, developer_tools_enabled());
    recompui::config::get_general_config().add_option_change_callback(mi::configkeys::general::developer_tools,
        [](recomp::config::ConfigValueVariant cur_value, recomp::config::ConfigValueVariant, recomp::config::OptionChangeContext) {
            recompui::config::set_tab_visible(mi::configkeys::debug::id,
                static_cast<mi::DeveloperTools>(std::get<uint32_t>(cur_value)) == mi::DeveloperTools::On);
        });
}
