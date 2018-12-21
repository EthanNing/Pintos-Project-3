# Pintos-Project-3
Pintos Project 3 : Virtual Memory



## Modified files

* CY:
  * Added vm/page.c, vm/page.h
  * Added vm/frame.c, vm/frame.h



## Modified Functions

* CY:
  * in page.c
    * bool page_allocate_user(void *upage);
      * Allocates a new virtual page for current user process and install the page into the process' page table. 
  * in frame.c
    * void* frame_allocate_user(void);
      * Allocates a new physical frame for current user process.