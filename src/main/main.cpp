#include <cstdio>
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <array>
#include <filesystem>
#include <stdexcept>
#include <cinttypes>
#include <chrono>

#include "nfd.h"

#include "ultramodern/ultra64.h"
#include "ultramodern/ultramodern.hpp"
#include "ultramodern/config.hpp"
#define SDL_MAIN_HANDLED
#ifdef _WIN32
#include "SDL.h"
#else
#include "SDL2/SDL.h"
#include "SDL2/SDL_syswm.h"
// Undefine x11 macros that get included by SDL_syswm.h.
#undef None
#undef Status
#undef LockMask
#undef ControlMask
#undef Success
#undef Always
#endif

#include "recompui/recompui.h"
#include "recompui/program_config.h"
#include "recompui/renderer.h"
#include "recompui/config.h"
#include "util/file.h"
#include "recompinput/input_events.h"
#include "recompinput/recompinput.h"
#include "recompinput/profiles.h"
#include "recompinput/players.h"
#include "librecomp/game.hpp"
#include "librecomp/mods.hpp"
#include "librecomp/rsp.hpp"

#include "mi_game.h"
#include "mi_config.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <timeapi.h>
#include "SDL_syswm.h"
#endif

#ifdef __linux__
#include <csignal>
#include <execinfo.h>
#include <unistd.h>

// Print a backtrace on fatal signals so crashes in recompiled code can be located (needs -rdynamic for names).
static void crash_handler(int sig) {
    void* frames[64];
    int count = backtrace(frames, 64);
    fprintf(stderr, "\nFatal signal %d, backtrace:\n", sig);
    backtrace_symbols_fd(frames, count, STDERR_FILENO);
    signal(sig, SIG_DFL);
    raise(sig);
}

static void install_crash_handler() {
    for (int sig : { SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGABRT }) {
        signal(sig, crash_handler);
    }
}
#else
static void install_crash_handler() {}
#endif

const std::string version_string = "0.1.0";

// Logs a startup milestone with the time since launch, to diagnose slow starts.
static void log_startup(const char* what) {
    static const auto launch = std::chrono::steady_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - launch).count();
    fprintf(stderr, "[startup %5lld ms] %s\n", (long long)ms, what);
}

template<typename... Ts>
void exit_error(const char* str, Ts ...args) {
    ((void)fprintf(stderr, str, args), ...);
    assert(false);

    ultramodern::error_handling::quick_exit(__FILE__, __LINE__, __FUNCTION__);
}

ultramodern::gfx_callbacks_t::gfx_data_t create_gfx() {
    SDL_SetHint(SDL_HINT_WINDOWS_DPI_AWARENESS, "permonitorv2");
    SDL_SetHint(SDL_HINT_GAMECONTROLLER_USE_BUTTON_LABELS, "0");
    SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI_PS4_RUMBLE, "1");
    SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI_PS5_RUMBLE, "1");
    SDL_SetHint(SDL_HINT_MOUSE_FOCUS_CLICKTHROUGH, "1");
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
#if defined(__linux__)
    // SDL2 defaults to X11. In a Wayland session that means XWayland, where Vulkan presentation can block for
    // long stretches (seen with NVIDIA on GNOME), stalling the game. Prefer native Wayland unless overridden.
    if (std::getenv("SDL_VIDEODRIVER") == nullptr && std::getenv("WAYLAND_DISPLAY") != nullptr) {
        SDL_SetHint(SDL_HINT_VIDEODRIVER, "wayland,x11");
    }
#endif

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER | SDL_INIT_JOYSTICK | SDL_INIT_HAPTIC) > 0) {
        exit_error("Failed to initialize SDL2: %s\n", SDL_GetError());
    }

    fprintf(stdout, "SDL Video Driver: %s\n", SDL_GetCurrentVideoDriver());
    log_startup("SDL video/input initialized");

    return {};
}

ultramodern::input::connected_device_info_t get_connected_device_info(int controller_num) {
    // Single player only uses port 1. Reporting more ports also hits an ultramodern bug where osContInit's
    // bitpattern only keeps the last connected port (`*pattern = 1 << controller` instead of `|=`).
    bool connected = recompinput::players::is_single_player_mode()
        ? controller_num == 0
        : recompinput::players::get_player_is_assigned(controller_num);
    if (connected) {
        return ultramodern::input::connected_device_info_t{
            .connected_device = ultramodern::input::Device::Controller,
            .connected_pak = ultramodern::input::Pak::None,
        };
    }

    return ultramodern::input::connected_device_info_t{
        .connected_device = ultramodern::input::Device::None,
        .connected_pak = ultramodern::input::Pak::None,
    };
}

// Development/testing input injection: when MI_INPUT names a file, its contents are added to controller 1's
// input on every poll. The file holds whitespace-separated N64 button names (A B Z START L R DU DD DL DR CU CD
// CL CR), optional stick values (x=-1..1 y=-1..1), right stick (rx=, ry=) and mouse motion per poll (mx=, my=).
// Empty the file to release everything.
bool get_input(int controller_num, uint16_t* buttons, float* x, float* y) {
    bool ret = recompinput::profiles::get_n64_input(controller_num, buttons, x, y);
    if (controller_num != 0) {
        return ret;
    }

    static const char* input_path = std::getenv("MI_INPUT");
    FILE* f = input_path != nullptr ? std::fopen(input_path, "r") : nullptr;
    if (f == nullptr) {
        mi::mouse_on_poll(0.0f, 0.0f);
        mi::mouse_adjust_stick(x, y);
        return ret;
    }

    static const std::pair<const char*, uint16_t> button_names[] = {
        {"A", 0x8000}, {"B", 0x4000}, {"Z", 0x2000}, {"START", 0x1000},
        {"DU", 0x0800}, {"DD", 0x0400}, {"DL", 0x0200}, {"DR", 0x0100},
        {"L", 0x0020}, {"R", 0x0010}, {"CU", 0x0008}, {"CD", 0x0004}, {"CL", 0x0002}, {"CR", 0x0001},
    };
    char token[32];
    float mouse_dx = 0.0f, mouse_dy = 0.0f, right_x = 0.0f, right_y = 0.0f;
    while (std::fscanf(f, "%31s", token) == 1) {
        float value;
        if (std::sscanf(token, "rx=%f", &value) == 1) {
            right_x = value;
        }
        else if (std::sscanf(token, "ry=%f", &value) == 1) {
            right_y = value;
        }
        else if (std::sscanf(token, "mx=%f", &value) == 1) {
            mouse_dx = value;
        }
        else if (std::sscanf(token, "my=%f", &value) == 1) {
            mouse_dy = value;
        }
        else if (std::sscanf(token, "x=%f", &value) == 1) {
            *x = value;
        }
        else if (std::sscanf(token, "y=%f", &value) == 1) {
            *y = value;
        }
        else {
            for (const auto& [name, mask] : button_names) {
                if (std::strcmp(token, name) == 0) {
                    *buttons |= mask;
                }
            }
        }
    }
    std::fclose(f);
    mi::set_test_right_analog(right_x, right_y);
    mi::mouse_on_poll(mouse_dx, mouse_dy);
    mi::mouse_adjust_stick(x, y);
    return true;
}

SDL_Window* window;

ultramodern::renderer::WindowHandle create_window(ultramodern::gfx_callbacks_t::gfx_data_t) {
    uint32_t flags = SDL_WINDOW_RESIZABLE;

#if defined(__APPLE__)
    flags |= SDL_WINDOW_METAL;
#elif defined(RT64_SDL_WINDOW_VULKAN)
    flags |= SDL_WINDOW_VULKAN;
#endif

    window = SDL_CreateWindow(mi::program_name.c_str(), SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1600, 900, flags);

    if (window == nullptr) {
        exit_error("Failed to create window: %s\n", SDL_GetError());
    }
    log_startup("window created");

    SDL_SysWMinfo wmInfo;
    SDL_VERSION(&wmInfo.version);
    SDL_GetWindowWMInfo(window, &wmInfo);

#if defined(_WIN32)
    return ultramodern::renderer::WindowHandle{ wmInfo.info.win.window, GetCurrentThreadId() };
#elif defined(__linux__) || defined(__ANDROID__)
    return ultramodern::renderer::WindowHandle{ window };
#elif defined(__APPLE__)
    SDL_MetalView view = SDL_Metal_CreateView(window);
    return ultramodern::renderer::WindowHandle{ wmInfo.info.cocoa.window,  SDL_Metal_GetLayer(view) };
#else
    static_assert(false && "Unimplemented");
#endif
}

void update_gfx(void*) {
    recompinput::handle_events();
}

static SDL_AudioCVT audio_convert;
static SDL_AudioDeviceID audio_device = 0;

// Samples per channel per second.
static uint32_t sample_rate = 48000;
static uint32_t output_sample_rate = 48000;
// Channel count.
constexpr uint32_t input_channels = 2;
static uint32_t output_channels = 2;

// Terminology: a frame is a collection of samples for each channel. e.g. 2 input samples is one input frame. This is unrelated to graphical frames.

// Number of frames to duplicate for fixing interpolation at the start and end of a chunk.
constexpr uint32_t duplicated_input_frames = 4;
// The number of output frames to skip for playback (to avoid playing duplicate inputs twice).
static uint32_t discarded_output_frames;

constexpr uint32_t bytes_per_frame = input_channels * sizeof(float);

void queue_samples(int16_t* audio_data, size_t sample_count) {
    mi::stats_on_audio(sample_count);
    // MI_AUDIO_DUMP=<file>: append the game's raw interleaved s16 output (for checking audio without listening).
    static FILE* audio_dump = std::getenv("MI_AUDIO_DUMP") ? std::fopen(std::getenv("MI_AUDIO_DUMP"), "wb") : nullptr;
    if (audio_dump != nullptr) {
        std::fwrite(audio_data, sizeof(int16_t), sample_count, audio_dump);
        std::fflush(audio_dump);
    }
    // Buffer for holding the output of swapping the audio channels. This is reused across
    // calls to reduce runtime allocations.
    static std::vector<float> swap_buffer;
    static std::array<float, duplicated_input_frames * input_channels> duplicated_sample_buffer;

    // Make sure the swap buffer is large enough to hold the audio data, including any extra space needed for resampling.
    size_t resampled_sample_count = sample_count + duplicated_input_frames * input_channels;
    size_t max_sample_count = std::max(resampled_sample_count, resampled_sample_count * audio_convert.len_mult);
    if (max_sample_count > swap_buffer.size()) {
        swap_buffer.resize(max_sample_count);
    }

    // Copy the duplicated frames from last chunk into this chunk
    for (size_t i = 0; i < duplicated_input_frames * input_channels; i++) {
        swap_buffer[i] = duplicated_sample_buffer[i];
    }

    // Convert the audio from 16-bit values to floats and swap the audio channels into the
    // swap buffer to correct for the address xor caused by endianness handling.
    float cur_main_volume = static_cast<float>(recompui::config::sound::get_main_volume()) / 100.0f;
    for (size_t i = 0; i < sample_count; i += input_channels) {
        swap_buffer[i + 0 + duplicated_input_frames * input_channels] = audio_data[i + 1] * (0.5f / 32768.0f) * cur_main_volume;
        swap_buffer[i + 1 + duplicated_input_frames * input_channels] = audio_data[i + 0] * (0.5f / 32768.0f) * cur_main_volume;
    }

    // TODO handle cases where a chunk is smaller than the duplicated frame count.
    assert(sample_count > duplicated_input_frames * input_channels);

    // Copy the last converted samples into the duplicated sample buffer to reuse in resampling the next queued chunk.
    for (size_t i = 0; i < duplicated_input_frames * input_channels; i++) {
        duplicated_sample_buffer[i] = swap_buffer[i + sample_count];
    }

    audio_convert.buf = reinterpret_cast<Uint8*>(swap_buffer.data());
    audio_convert.len = (sample_count + duplicated_input_frames * input_channels) * sizeof(swap_buffer[0]);

    int ret = SDL_ConvertAudio(&audio_convert);

    if (ret < 0) {
        printf("Error using SDL audio converter: %s\n", SDL_GetError());
        throw std::runtime_error("Error using SDL audio converter");
    }

    uint64_t cur_queued_microseconds = uint64_t(SDL_GetQueuedAudioSize(audio_device)) / bytes_per_frame * 1000000 / sample_rate;
    uint32_t num_bytes_to_queue = audio_convert.len_cvt - output_channels * discarded_output_frames * sizeof(swap_buffer[0]);
    float* samples_to_queue = swap_buffer.data() + output_channels * discarded_output_frames / 2;

    // Prevent audio latency from building up by skipping samples in incoming audio when too many samples are already queued.
    // Skip samples based on how many microseconds of samples are queued already.
    uint32_t skip_factor = cur_queued_microseconds / 100000;
    if (skip_factor != 0) {
        uint32_t skip_ratio = 1 << skip_factor;
        num_bytes_to_queue /= skip_ratio;
        for (size_t i = 0; i < num_bytes_to_queue / (output_channels * sizeof(swap_buffer[0])); i++) {
            samples_to_queue[2 * i + 0] = samples_to_queue[2 * skip_ratio * i + 0];
            samples_to_queue[2 * i + 1] = samples_to_queue[2 * skip_ratio * i + 1];
        }
    }

    // Queue the swapped audio data.
    // Offset the data start by only half the discarded frame count as the other half of the discarded frames are at the end of the buffer.
    SDL_QueueAudio(audio_device, samples_to_queue, num_bytes_to_queue);
}

size_t get_queued_audio_bytes() {
    return SDL_GetQueuedAudioSize(audio_device);
}

size_t get_frames_remaining() {
    constexpr float buffer_offset_frames = 1.0f;
    // Get the number of remaining buffered audio bytes.
    uint64_t buffered_byte_count = SDL_GetQueuedAudioSize(audio_device);

    // Scale the byte count based on the ratio of sample rates and channel counts.
    buffered_byte_count = buffered_byte_count * 2 * sample_rate / output_sample_rate / output_channels;

    // Adjust the reported count to be some number of refreshes in the future, which helps ensure that
    // there are enough samples even if the audio thread experiences a small amount of lag. This prevents
    // audio popping on games that use the buffered audio byte count to determine how many samples
    // to generate.
    uint32_t frames_per_vi = (sample_rate / 60);
    if (buffered_byte_count > (buffer_offset_frames * bytes_per_frame * frames_per_vi)) {
        buffered_byte_count -= (buffer_offset_frames * bytes_per_frame * frames_per_vi);
    }
    else {
        buffered_byte_count = 0;
    }
    // Convert from byte count to sample count.
    return static_cast<uint32_t>(buffered_byte_count / bytes_per_frame);
}

void update_audio_converter() {
    int ret = SDL_BuildAudioCVT(&audio_convert, AUDIO_F32, input_channels, sample_rate, AUDIO_F32, output_channels, output_sample_rate);

    if (ret < 0) {
        printf("Error creating SDL audio converter: %s\n", SDL_GetError());
        throw std::runtime_error("Error creating SDL audio converter");
    }

    // Calculate the number of samples to discard based on the sample rate ratio and the duplicate frame count.
    discarded_output_frames = duplicated_input_frames * output_sample_rate / sample_rate;
}

void set_frequency(uint32_t freq) {
    sample_rate = freq;

    update_audio_converter();
}

bool reset_audio(uint32_t output_freq) {
    SDL_AudioSpec spec_desired{
        .freq = (int)output_freq,
        .format = AUDIO_F32,
        .channels = (Uint8)output_channels,
        .silence = 0, // calculated
        .samples = 0x100, // Fairly small sample count to reduce the latency of internal buffering
        .padding = 0, // unused
        .size = 0, // calculated
        .callback = nullptr,
        .userdata = nullptr
    };

    audio_device = SDL_OpenAudioDevice(nullptr, false, &spec_desired, nullptr, 0);
    if (audio_device == 0) {
        std::string audio_error = std::string("No audio device could be found. Please make sure an audio device is available.\nError opening audio device: ") + std::string(SDL_GetError());
        recompui::message_box(audio_error.c_str());
        return false;
    }

    SDL_PauseAudioDevice(audio_device, 0);

    output_sample_rate = output_freq;
    update_audio_converter();

    return true;
}

extern RspUcodeFunc n_aspMain;

RspUcodeFunc* get_rsp_microcode(const OSTask* task) {
    switch (task->t.type) {
    case M_AUDTASK:
        return n_aspMain;

    default:
        fprintf(stderr, "Unknown task: %" PRIu32 "\n", task->t.type);
        return nullptr;
    }
}

extern "C" void recomp_entrypoint(uint8_t * rdram, recomp_context * ctx);
gpr get_entrypoint_address();

std::vector<recomp::GameEntry> supported_games = {
    {
        .rom_hash = 0xD64AE25F3173CE5CULL, // XXH3_64 of Mission Impossible (USA).z64
        .internal_name = "MISSION IMPOSSIBLE",
        .display_name = "Mission: Impossible",
        .game_id = u8"mi.n64.us",
        .mod_game_id = "mi",
        .save_type = recomp::SaveType::Eep4k,
        .is_enabled = true,
        .has_compressed_code = false,
        .entrypoint_address = get_entrypoint_address(),
        .entrypoint = recomp_entrypoint,
    },
};

std::string get_game_thread_name(const OSThread* t) {
    return "[Game] " + std::to_string(t->id);
}

// Change to the executable's directory so assets resolve (recompui loads them relative to the cwd).
void set_cwd_to_executable_dir() {
#if defined(__linux__)
    std::error_code ec;
    std::filesystem::path exe = std::filesystem::read_symlink("/proc/self/exe", ec);
    if (!ec) {
        std::filesystem::current_path(exe.parent_path(), ec);
    }
#elif defined(_WIN32)
    wchar_t exe_path[MAX_PATH];
    DWORD n = GetModuleFileNameW(nullptr, exe_path, MAX_PATH);
    if (n > 0 && n < MAX_PATH) {
        std::error_code ec;
        std::filesystem::current_path(std::filesystem::path(exe_path).parent_path(), ec);
    }
#endif
}

// Development convenience: MI_ROM=<path> validates and stores that ROM, then starts the game as soon
// as the launcher is up. Without MI_ROM, a previously stored ROM is started the same way unless
// MI_NO_AUTOBOOT is set.
void setup_autoboot() {
    if (std::getenv("MI_NO_AUTOBOOT") != nullptr) {
        return;
    }

    static std::filesystem::path rom_path;
    if (const char* rom_env = std::getenv("MI_ROM")) {
        rom_path = std::filesystem::absolute(rom_env);
    }

    // The ROM subsystem isn't ready on the first launcher frames, so wait a moment before selecting/starting.
    // Wall-clock based, since the launcher's frame rate is throttled while its window is hidden.
    recompui::register_launcher_update_callback([](recompui::LauncherMenu*) {
        static auto first_update = std::chrono::steady_clock::now();
        static bool done = false;
        if (done || std::chrono::steady_clock::now() - first_update < std::chrono::milliseconds(500)) {
            return;
        }
        done = true;

        std::u8string game_id = supported_games[0].game_id;
        if (!rom_path.empty()) {
            recomp::RomValidationError err = recomp::select_rom(rom_path, game_id);
            if (err != recomp::RomValidationError::Good) {
                fprintf(stderr, "MI_ROM validation failed (%d)\n", (int)err);
                return;
            }
        }
        if (recomp::is_rom_valid(game_id)) {
            fprintf(stderr, "Auto-booting %s\n", reinterpret_cast<const char*>(game_id.c_str()));
            recomp::start_game(game_id, {});
            recompui::hide_all_contexts();
        }
        else {
            fprintf(stderr, "Auto-boot skipped: no valid ROM stored\n");
        }
    });
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    recomp::Version project_version{};
    if (!recomp::Version::from_string(version_string, project_version)) {
        ultramodern::error_handling::message_box(("Invalid version string: " + version_string).c_str());
        return EXIT_FAILURE;
    }

    log_startup("main");
    install_crash_handler();
    set_cwd_to_executable_dir();

    // Keep log output ordered and immediate when redirected to a file.
    setvbuf(stdout, nullptr, _IONBF, 0);

#ifdef _WIN32
    // Set up high resolution timing period.
    timeBeginPeriod(1);
    // Force wasapi on Windows, as there seems to be some issue with sample queueing with directsound currently.
    SDL_setenv("SDL_AUDIODRIVER", "wasapi", true);
#endif

    // Initialize SDL audio and set the output frequency. This must happen before NFD_Init: once GTK has set up
    // its D-Bus connection, SDL's synchronous RealtimeKit request for the audio thread can stall startup for
    // 25+ seconds.
    SDL_InitSubSystem(SDL_INIT_AUDIO);
    if (!reset_audio(48000)) {
        // It is not possible to initialize without an audio device.
        return EXIT_FAILURE;
    }

    log_startup("audio initialized");

    // Initialize native file dialogs.
    NFD_Init();
    log_startup("file dialogs initialized");

    // Initialize program settings.
    recompui::programconfig::set_program_name(mi::program_name);
    recompui::programconfig::set_program_id(mi::program_id);

    // Source controller mappings file
    std::u8string controller_db_path = (recompui::file::get_program_path() / "recompcontrollerdb.txt").u8string();
    if (SDL_GameControllerAddMappingsFromFile(reinterpret_cast<const char *>(controller_db_path.c_str())) < 0) {
        fprintf(stderr, "Failed to load controller mappings: %s\n", SDL_GetError());
    }

    // Register fonts.
    recompui::register_primary_font("InterVariable.ttf", "Inter Variable");

    // Register configuration path.
    recomp::register_config_path(recompui::file::get_app_folder_path());

    // Register supported games
    for (const auto& game : supported_games) {
        recomp::register_game(game);
    }

    recompui::register_ui_exports();

    mi::register_overlays();

    recompinput::players::set_single_player_mode(true);

    mi::init_config();

    setup_autoboot();

    recomp::rsp::callbacks_t rsp_callbacks{
        .get_rsp_microcode = get_rsp_microcode,
    };

    ultramodern::renderer::callbacks_t renderer_callbacks{
        .create_render_context = [](uint8_t* rdram, ultramodern::renderer::WindowHandle window_handle, bool developer_mode) {
            mi::stats_set_rdram(rdram);
            auto presentation_mode = ultramodern::renderer::PresentationMode::PresentEarly;
            return recompui::renderer::create_render_context(rdram, window_handle, presentation_mode, developer_mode);
        },
    };

    ultramodern::gfx_callbacks_t gfx_callbacks{
        .create_gfx = create_gfx,
        .create_window = create_window,
        .update_gfx = update_gfx,
    };

    ultramodern::audio_callbacks_t audio_callbacks{
        .queue_samples = queue_samples,
        .get_frames_remaining = get_frames_remaining,
        .set_frequency = set_frequency,
    };

    ultramodern::input::callbacks_t input_callbacks{
        .poll_input = recompinput::poll_inputs,
        .get_input = get_input,
        .set_rumble = recompinput::set_rumble,
        .get_connected_device_info = get_connected_device_info,
    };

    ultramodern::events::callbacks_t thread_callbacks{
        .vi_callback = []() {
            recompinput::update_rumble();
            mi::stats_on_vi();
        },
        .gfx_init_callback = nullptr,
    };

    ultramodern::error_handling::callbacks_t error_handling_callbacks{
        .message_box = recompui::message_box,
    };

    ultramodern::threads::callbacks_t threads_callbacks{
        .get_game_thread_name = get_game_thread_name,
    };

    log_startup("starting runtime");
    recomp::start(
        project_version,
        {},
        rsp_callbacks,
        renderer_callbacks,
        audio_callbacks,
        input_callbacks,
        gfx_callbacks,
        thread_callbacks,
        error_handling_callbacks,
        threads_callbacks
    );

    NFD_Quit();

#ifdef _WIN32
    // End high resolution timing period.
    timeEndPeriod(1);
#endif

    // Exit without running static destructors: the game's threads are still running and use runtime and audio state
    // that those destructors would tear down. Saves are already written (the runtime joins its saving thread).
    std::fflush(nullptr);
    std::_Exit(EXIT_SUCCESS);
}
