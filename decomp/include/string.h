#ifndef STRING_H
#define STRING_H

#include "ultra64.h"

/* The game's strcmp (word-at-a-time when both strings are equally aligned). Named Str_Compare because N64Recomp
 * reserves the libc names. */
s32 Str_Compare(const char* a, const char* b);

#endif
