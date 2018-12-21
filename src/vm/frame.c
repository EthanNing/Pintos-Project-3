#include "vm/frame.h"
#include "threads/palloc.h"

void* frame_allocate_user(void) {
    void *kpage = palloc_get_page(PAL_USER | PAL_ZERO);
    if(kpage != NULL) {
        // Successfully acquired a page from user pool
        return kpage;
    }
    else {
        // NOT IMPLEMENTED ERROR: NEEDS SWAPPING
        ASSERT(0);
        // not reached
        return NULL;
    }
}
