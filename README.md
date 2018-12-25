# Pintos-Project-3
Pintos Project 3 : Virtual Memory



## Modified files

* CY:
  * Added vm/page.c, vm/page.h
  * Added vm/frame.c, vm/frame.h



## Modified Functions

* CY:
  * in page.c
  	* unsigned page_hash_func (const struct hash_elem *e, void *aux UNUSED)
  	  * Since the sup_page_table will be implemented as a hash table, those three function is needed by the the Pintos specifican Returns a hash of element's data, as a value anywhere in the range of unsigned int.
    * bool page_allocate_user(void *upage);
      * Allocates a new virtual page for current user process and install the page into the process' page table. 
    * bool page_hash_less_func (const struct hash_elem *elem1,const struct hash_elem *elem2,void *aux UNUSED)
      * Function to compare the keys stored in elements a and b. Returns true if a is less than b, false if a is greater than or equal to b.If two elements compare equal, then they must hash to equal values..
    * void page_hash_action_func (struct hash_elem *e, void *aux UNUSED)
      * Functions to perform free() action on hash elements.
    * void page_table_init (struct hash *spt)
      * Function to initialize the sup page table.
    * void page_table_destroy (struct hash *spt)
      * Function to free the sup page table
    * struct sup_page_table_entry * get_spte (void *uva)
      * Function to find whether the current thread have the spte with given uva inside.
    * bool page_load (void *uva)
      * Function to load in page using specific functions.
    * bool page_grow_stack (void *uva)
      Function to impelement stack growth when new page is needed (with given uav inside)
  * in frame.c
    * void* frame_allocate_user(void);
      * Allocates a new physical frame for current user process.
    * void frame_table_init (void);
      * Function to initialize the frame table and lock.
    * void frame_free (void *frame)
      * Function to free the specific frame using given address
    * void frame_add_to_table (void *frame)
      * Function adding the allocated frame to the frame table
    * bool frame_evict ()
      * Function to evict a frame from the list.