// Native functions the decompiled code calls (decomp/imports.ld gives each a dummy address that N64Recomp turns into
// a call here). Arguments arrive in the MIPS argument registers.
#include "recomp.h"

extern "C" void mi_override_entry(uint8_t* rdram, uint32_t entry, uint32_t buffer);

extern "C" void mi_override_entry_import(uint8_t* rdram, recomp_context* ctx) {
    mi_override_entry(rdram, (uint32_t)ctx->r4, (uint32_t)ctx->r5);
}
