#ifndef DECOMP_H
#define DECOMP_H

#include "ultra64.h"

/* Marks a decompiled function: it replaces the recompiled function of the same name (N64Recomp patch mode). */
#define DECOMP __attribute__((section(".recomp_patch")))

/* Native functions the decompiled code calls. Each has a dummy address in imports.ld and an adapter in
 * src/decomp/imports.cpp. */

/* Data overrides (MI_OVERRIDE_DIR, src/game/overrides.cpp): replace a just-decoded Burp entry with an edited file. */
void mi_override_entry_import(void* entry, void* buffer);

#endif
