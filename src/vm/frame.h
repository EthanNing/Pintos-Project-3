#ifndef VM_FRAME_H
#define VM_FRAME_H

#include "threads/thread.h"

/* Allocates a new physical frame for current user process. */
void* frame_allocate_user(void);

#endif /* vm/frame.h */
