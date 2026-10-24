#ifndef HEAP_H
#define HEAP_H

#include "ultra64.h"

void* Heap_Alloc(u32 size);
/* Returns TRUE if ptr was an allocated block (and is now free), FALSE otherwise. */
s32 Heap_Free(void* ptr);
/* Size of the largest free block. */
u32 Heap_GetLargestFree(void);

#endif
