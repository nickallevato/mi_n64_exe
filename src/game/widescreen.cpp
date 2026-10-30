// Widescreen fixes that need extra draw commands in the game's display list (RT64 extended GBI): black bars outside
// the original 4:3 frame in scripted scenes, and beside the IMF handheld's bezel.
#include <cstdint>

#include "mi_config.h"

// The in-engine cutscenes shown on the IMF handheld (e.g. the Ice Hit intro after Phelps opens his briefcase) draw
// the 3D scene full screen, then the handheld's bezel on top as four 2D images (0xA9 top, 0xAA left, 0xAB right,
// 0xAC bottom) that only cover the 4:3 area. With RT64's widescreen "Expand" the 3D scene fills the whole window, so
// it shows past the sides of the bezel. Before the last piece is drawn, add black bars from the screen edges to the
// 4:3 edges, like the other handheld screens (menus) that have nothing outside the bezel. At 4:3 the bars are empty.
namespace {
    constexpr uint32_t image_handheld_bezel_bottom = 0xAC;

    // F3DEX (Mission: Impossible uses F3DEX.NoN): RT64's hook command is G_SPNOOP (0x00).
    constexpr uint32_t rt64_hook_opcode = 0x00;
    constexpr uint32_t rt64_hook_magic = 0x525464;
    constexpr uint32_t rt64_hook_op_enable = 0x1;
    constexpr uint32_t rt64_hook_op_disable = 0x2;
    constexpr uint32_t rt64_extended_opcode = 0x64;  // Unused by F3DEX.
    constexpr uint32_t g_ex_fillrect_v1 = 0x000003;
    constexpr uint32_t g_ex_origin_left = 0x0;
    constexpr uint32_t g_ex_origin_right = 0x400;
    constexpr uint32_t g_ex_origin_none = 0x800;

    constexpr uint32_t g_rdppipesync = 0xE7000000;
    constexpr uint32_t g_setothermode_h_cycletype = 0xBA001402;  // shift 20, 2 bits
    constexpr uint32_t g_cyc_fill = 0x00300000;
    constexpr uint32_t g_setfillcolor = 0xF7000000;
    constexpr uint32_t fill_black_rgba16 = 0x00010001;  // two opaque black RGBA5551 pixels

    uint32_t* ram_u32_ptr(uint8_t* rdram, uint32_t addr) {
        return reinterpret_cast<uint32_t*>(rdram + (addr - 0x80000000u));
    }

    // RT64 G_EX_FILLRECT_V1: coordinates in 10.2 fixed point, each edge relative to its origin.
    void write_ex_fillrect(uint32_t*& cmd, uint32_t left_origin, int16_t ulx, int16_t uly, uint32_t right_origin, int16_t lrx, int16_t lry) {
        *cmd++ = (rt64_extended_opcode << 24) | g_ex_fillrect_v1;
        *cmd++ = (left_origin & 0xFFF) | ((right_origin & 0xFFF) << 12);
        *cmd++ = (uint32_t(uint16_t(ulx)) << 16) | uint16_t(uly);
        *cmd++ = (uint32_t(uint16_t(lrx)) << 16) | uint16_t(lry);
    }

    // Writes black bars from each screen edge to the 4:3 edge at cmd; returns the end. With RT64's widescreen "Expand"
    // they cover what is drawn outside the original 4:3 frame; at 4:3 they are empty.
    uint32_t* write_side_bars(uint32_t* cmd) {
        // gEXEnable
        *cmd++ = (rt64_hook_opcode << 24) | rt64_hook_magic;
        *cmd++ = (rt64_hook_op_enable << 28) | rt64_extended_opcode;
        *cmd++ = g_rdppipesync;
        *cmd++ = 0;
        *cmd++ = g_setothermode_h_cycletype;
        *cmd++ = g_cyc_fill;
        *cmd++ = g_setfillcolor;
        *cmd++ = fill_black_rgba16;
        // Left bar: left screen edge to x = 0. Right bar: x = 320 to the right screen edge. RT64 drops rectangles whose
        // raw lrx < ulx before applying the origins, so the left bar uses the same raw x for both edges; fill mode adds
        // the lower right pixel, so it ends just before x = 0.
        write_ex_fillrect(cmd, g_ex_origin_left, -4, 0, g_ex_origin_none, -4, 239 << 2);
        write_ex_fillrect(cmd, g_ex_origin_none, 320 << 2, 0, g_ex_origin_right, 320 << 2, 239 << 2);
        *cmd++ = g_rdppipesync;
        *cmd++ = 0;
        // gEXDisable
        *cmd++ = (rt64_hook_opcode << 24) | rt64_hook_magic;
        *cmd++ = rt64_hook_op_disable << 28;
        return cmd;
    }
}

// Hook at the start of the 2D image draw (Image_Draw(Gfx** gfx, image, alpha, ...)), which writes its commands at *gfx
// and advances it. The next draw sets its own cycle type and combiner, so the fill mode set here doesn't leak.
extern "C" void mi_widescreen_image_draw(uint8_t* rdram, uint32_t gfx_ptr_addr, uint32_t image) {
    if (image != image_handheld_bezel_bottom) {
        return;
    }

    uint32_t* gfx_ptr = ram_u32_ptr(rdram, gfx_ptr_addr);
    uint32_t* start = ram_u32_ptr(rdram, *gfx_ptr);
    *gfx_ptr += uint32_t(write_side_bars(start) - start) * sizeof(uint32_t);
}

// Hook at the start of Gfx_SubmitTask(Gfx* end), which closes the frame's display list at `end`: during scripted
// scenes, draw the bars last so the scene keeps its original 4:3 framing (the expanded view shows parts of the set
// that were never meant to be seen). Returns the new end.
extern "C" uint32_t mi_widescreen_frame_end(uint8_t* rdram, uint32_t gfx_end) {
    if (!mi::level_loop_is_scene()) {
        return gfx_end;
    }

    uint32_t* start = ram_u32_ptr(rdram, gfx_end);
    return gfx_end + uint32_t(write_side_bars(start) - start) * sizeof(uint32_t);
}
