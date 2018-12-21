#ifndef VM_PAGE_H
#define VM_PAGE_H

#include "threads/thread.h"

/* Allocates a new virtual page for current user process and install the page
    into the process' page table. */
bool page_allocate_user(void *upage);

#endif /* vm/page.h */
