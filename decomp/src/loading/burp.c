#include "decomp.h"
#include "burp.h"
#include "heap.h"
#include "rom.h"
#include "string.h"

/* Keep this much of the largest free heap block free when allocating the inflate input buffer. */
#define INFLATE_HEAP_RESERVE 0x8000
/* Don't inflate unless the largest free block is bigger than this. */
#define INFLATE_MIN_FREE 0x8800

/* Opens the archive at romStart..romEnd: reads the header, the entry table and the name strings, and turns the
 * table's offsets into a names pointer and absolute ROM addresses. */
DECOMP BurpArchive* Burp_Open(u32 romStart, u32 romEnd) {
    u32 header[2]; /* 'Burp', entry count */
    BurpArchive* archive;
    u32 tableSize;
    u32 namesRom;
    u32 namesSize;
    u32 i;

    archive = Heap_Alloc(sizeof(BurpArchive));
    if (archive == NULL) {
        return NULL;
    }
    archive->romStart = romStart;
    archive->romSize = romEnd - romStart;
    Rom_Read(romStart, header, sizeof(header));
    if (header[0] != BURP_MAGIC) {
        /* Returned as is, with no entry table. */
        return archive;
    }

    tableSize = header[1] * sizeof(BurpEntry);
    archive->entries = Heap_Alloc(tableSize);
    if (archive->entries == NULL) {
        return archive;
    }
    archive->count = header[1];
    Rom_Read(romStart + 8, archive->entries, tableSize);

    /* The name strings run from the first entry's name to the end of the archive. */
    if (archive->entries[0].name != 0) {
        namesRom = romStart + archive->entries[0].name;
        namesSize = romEnd - namesRom;
        archive->namesOffset = archive->romSize - namesSize;
        archive->names = Heap_Alloc(namesSize);
        if (archive->names != NULL) {
            Rom_Read(namesRom, archive->names, namesSize);
        } else {
            /* The game frees the archive here and still goes on to fix up and return it. */
            Burp_Close(archive);
        }
    } else {
        archive->names = NULL;
    }

    /* Each name is 2 bytes into its string's slot in the names buffer. */
    for (i = 0; i < archive->count; i++) {
        archive->entries[i].name += 2 + ((u32)archive->names - archive->namesOffset);
        archive->entries[i].romAddr += archive->romStart;
    }
    return archive;
}

DECOMP void Burp_Close(BurpArchive* archive) {
    if (archive->names != NULL) {
        Heap_Free(archive->names);
    }
    if (archive->entries != NULL) {
        Heap_Free(archive->entries);
    }
    Heap_Free(archive);
}

/* Returns entry index, or NULL if out of range. */
DECOMP BurpEntry* Burp_GetEntry(BurpArchive* archive, u32 index) {
    if (index >= archive->count) {
        return NULL;
    }
    return &archive->entries[index];
}

/* Returns the name of entry index, or NULL if out of range or the archive has no names. */
DECOMP char* Burp_GetName(BurpArchive* archive, u32 index) {
    if (archive->names == NULL || index >= archive->count) {
        return NULL;
    }
    return (char*)archive->entries[index].name;
}

/* Returns the index of the entry called name, or -1. */
DECOMP s32 Burp_FindByName(BurpArchive* archive, char* name) {
    u32 i;

    for (i = 0; i < archive->count; i++) {
        if (Str_Compare(name, Burp_GetName(archive, i)) == 0) {
            break;
        }
    }
    if (i >= archive->count) {
        return -1;
    }
    return i;
}

/* Copies entry index to out. The game doesn't check the index. */
DECOMP s32 Burp_CopyEntry(BurpArchive* archive, u32 index, BurpEntry* out) {
    *out = *Burp_GetEntry(archive, index);
    return TRUE;
}

DECOMP u32 Burp_GetCount(BurpArchive* archive) {
    return archive->count;
}

/* Reads entry index's stored bytes into dest without decoding them. Returns TRUE on success. */
DECOMP s32 Burp_ReadRaw(BurpArchive* archive, u32 index, void* dest) {
    BurpEntry* entry = Burp_GetEntry(archive, index);

    if (entry == NULL) {
        return FALSE;
    }
    Rom_Read(entry->romAddr, dest, entry->storedSize);
    return TRUE;
}

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

/* Burp_ReadRaw by name. */
DECOMP s32 Burp_ReadRawByName(BurpArchive* archive, char* name, void* dest) {
    s32 index = Burp_FindByName(archive, name);

    if (index == -1) {
        return FALSE;
    }
    return Burp_ReadRaw(archive, index, dest);
}

/* Burp_LoadTo by name. Unlike Burp_LoadTo, the game applies no data override here. */
DECOMP u8 Burp_LoadToByName(BurpArchive* archive, char* name, void* dest) {
    s32 index = Burp_FindByName(archive, name);
    BurpEntry* entry;

    if (index == -1) {
        return FALSE;
    }
    entry = Burp_GetEntry(archive, index);
    if (entry == NULL) {
        return FALSE;
    }
    return Burp_Decode(entry, dest);
}

/* Burp_Load by name. */
DECOMP void* Burp_LoadByName(BurpArchive* archive, char* name) {
    s32 index = Burp_FindByName(archive, name);

    if (index == -1) {
        return NULL;
    }
    return Burp_Load(archive, index);
}
