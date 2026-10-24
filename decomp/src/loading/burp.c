#include "decomp.h"
#include "burp.h"

DECOMP u32 Burp_GetCount(BurpArchive* archive) {
    return archive->count;
}
