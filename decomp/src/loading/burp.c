#include "decomp.h"
#include "burp.h"
#include "heap.h"
#include "rom.h"

/* Keep this much of the largest free heap block free when allocating the inflate input buffer. */
#define INFLATE_HEAP_RESERVE 0x8000
/* Don't inflate unless the largest free block is bigger than this. */
#define INFLATE_MIN_FREE 0x8800

/* Decodes entry into dest: a ROM read when stored, otherwise inflate through a temporary heap buffer. Returns TRUE on
 * success. For an inflated entry that is the result of freeing the temporary buffer: the game ignores the inflate
 * error code. */
static u8 Burp_Decode(BurpEntry* entry, void* dest) {
    u32 tmpSize;
    void* tmp;

    if (entry->packing == BURP_STORED) {
        Rom_Read(entry->romAddr, dest, entry->storedSize);
        return TRUE;
    }

    tmpSize = Heap_GetLargestFree();
    if (tmpSize <= INFLATE_MIN_FREE) {
        return FALSE;
    }
    tmpSize -= INFLATE_HEAP_RESERVE;
    if (entry->storedSize < tmpSize) {
        tmpSize = (entry->storedSize + 1) & ~1;
    }
    tmp = Heap_Alloc(tmpSize);
    if (tmp == NULL) {
        return FALSE;
    }
    Inflate_FromRom(entry->romAddr, dest, entry->storedSize, tmp, tmpSize);
    return Heap_Free(tmp);
}

DECOMP u32 Burp_GetCount(BurpArchive* archive) {
    return archive->count;
}

/* Decodes entry index into dest. Returns TRUE on success. */
DECOMP u8 Burp_LoadTo(BurpArchive* archive, u32 index, void* dest) {
    BurpEntry* entry = Burp_GetEntry(archive, index);
    u8 ok;

    if (entry == NULL) {
        return FALSE;
    }
    ok = Burp_Decode(entry, dest);
    if (ok) {
        mi_override_entry_import(entry, dest);
    }
    return ok;
}

/* Allocates a buffer for entry index and decodes it there. Returns the buffer, or NULL on failure. */
DECOMP void* Burp_Load(BurpArchive* archive, u32 index) {
    BurpEntry* entry = Burp_GetEntry(archive, index);
    void* buffer;

    if (entry == NULL) {
        return NULL;
    }
    buffer = Heap_Alloc(entry->size);
    if (buffer == NULL) {
        return NULL;
    }
    if (!Burp_Decode(entry, buffer)) {
        Heap_Free(buffer);
        return NULL;
    }
    mi_override_entry_import(entry, buffer);
    return buffer;
}
