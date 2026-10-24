// Developer builds only (-DMI_DECOMP=ON): register the decompiled game code (scripts/decomp.sh) as patch sections.
#include "mi_game.h"
#include "decomp/dispatch.hpp"
#include "../../RecompiledDecomp/recomp_overlays.inl"

#include "librecomp/overlays.hpp"

extern "C" {
    extern const unsigned char mi_decomp_bin[];
    extern const unsigned int mi_decomp_bin_len;
}

void mi::register_decomp() {
    recomp::overlays::register_patches(reinterpret_cast<const char*>(mi_decomp_bin), mi_decomp_bin_len, section_table, ARRLEN(section_table));
    mi::decomp::init();
}
