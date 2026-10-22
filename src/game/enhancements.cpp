// Native helpers called from recompiler hooks (mi.us.toml) to implement enhancements.
#include "mi_config.h"

// Minimum VIs per in-level frame: 2 = original 30 fps cap, 1 = 60 fps.
extern "C" int mi_level_min_vis_per_frame() {
    return mi::get_frame_rate() == mi::FrameRate::Fps60 ? 1 : 2;
}
