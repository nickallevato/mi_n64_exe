#ifndef ROM_H
#define ROM_H

#include "ultra64.h"

/* Copies size bytes from cartridge ROM offset romAddr to dest (PI DMA in chunks; a RAM source is a plain copy). */
void Rom_Read(u32 romAddr, void* dest, u32 size);

/* Inflates raw DEFLATE data at romAddr (ROM, or RAM if 0x8xxxxxxx) into dest, streaming the input through tmp.
 * Returns 0 on success or a nonzero inflate error code (the Burp loaders ignore it). */
s32 Inflate_FromRom(u32 romAddr, void* dest, u32 srcSize, void* tmp, u32 tmpSize);

#endif
