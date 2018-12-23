#include "vm/frame.h"
#include "threads/palloc.h"

/*Function to initialize the frame table and lock*/
void frame_table_init (void){
    list_init(&frame_table);
    lock_init(&frame_table_lock);
}

/*Function to free the specific frame using given address*/
void frame_free (void *frame){
    struct list_elem *e;
    lock_acquire(&frame_table_lock);
    //Need lock cause we may have multipule access different processes
    for (e = list_begin(&frame_table); e != list_end(&frame_table);e = list_next(e)){
        struct frame_entry *fte = list_entry(e, struct frame_entry, elem);
        if (fte->frame == frame){
            //If we can find the frame within the given list
            list_remove(e);
            //First we remove it from the list
            free(fte);
            //Second we free the entire fte entry
            break;
        }
    }
    lock_release(&frame_table_lock);
    palloc_free_page(frame);
    //Finally we free that frame directly
}

/*Function to allocate the frame, the allocated frame will be added to the frame table*/
void* frame_allocate_user(void) {
    void *kpage = palloc_get_page(PAL_USER | PAL_ZERO);
    if(kpage != NULL) {
        // Successfully acquired a frame from user pool
        frame_add_to_table(kpage)
        //Add that page to page table 
        return kpage;
    }
    else {
        // NOT IMPLEMENTED ERROR: NEEDS SWAPPING
        ASSERT(0);
        // not reached
        return NULL;
    }
}

/*Function adding the allocated frame to the frame table*/
void frame_add_to_table (void *frame){
    struct frame_entry *fte = malloc(sizeof(struct frame_entry));
    fte->frame = frame;
    //Set the address
    fte->tid = thread_tid(c);
    //Set the tid to be current thread's tid
 
    lock_acquire(&frame_table_lock);
    list_push_back(&frame_table, &fte->elem);
    //Add this thread to list
    lock_release(&frame_table_lock);
}
/*Function to evict a frame from the list*/
bool frame_evict (){
    struct list_elem *e;
    lock_acquire(&frame_table_lock);
    //Need lock cause we may have multipule access different processes
    for (e = list_begin(&frame_table); e != list_end(&frame_table);e = list_next(e)){
        //Here we should find a victim and swap it to the disk sector
        //Need further mpelementation
        return false;
    }
}
