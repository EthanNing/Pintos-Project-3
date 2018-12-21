#include "vm/page.h"
#include "vm/frame.h"
#include "userprog/process.h"

bool page_allocate_user(void *upage) {
    void *kpage = frame_allocate_user();
    if(kpage == NULL) return false;  // Unknown error happened
    return install_page(upage, kpage, true);
}
