#ifndef BURP_H
#define BURP_H

#include "ultra64.h"

/* "Burp" archives: the game's compressed asset containers (docs/levels-loading.md). In ROM: 'Burp', a u32 entry
 * count, the entry table, then the name strings and the entry data. */

#define BURP_MAGIC 0x42757270 /* 'Burp' */

/* One archive entry (0x14 bytes). Burp_Open rewrites the two offsets in its RAM copy of the table. */
typedef struct BurpEntry {
    /* 0x00 */ u32 size;       /* decoded size */
    /* 0x04 */ u32 storedSize; /* size in ROM */
    /* 0x08 */ u32 name;       /* in ROM: offset of the name; after Burp_Open: pointer into BurpArchive.names */
    /* 0x0C */ u32 romAddr;    /* in ROM: offset of the data; after Burp_Open: absolute ROM address */
    /* 0x10 */ u32 packing;    /* 4 = raw DEFLATE */
} BurpEntry; /* size = 0x14 */

/* An open archive, allocated by Burp_Open. */
typedef struct BurpArchive {
    /* 0x00 */ u32 count;
    /* 0x04 */ BurpEntry* entries;
    /* 0x08 */ char* names;      /* the archive's name strings, or NULL */
    /* 0x0C */ u32 romStart;     /* ROM address of the archive */
    /* 0x10 */ u32 romSize;
    /* 0x14 */ u32 namesOffset;  /* offset of the name strings in the archive */
} BurpArchive; /* size = 0x18 */

u32 Burp_GetCount(BurpArchive* archive);

#endif
