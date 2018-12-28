#include "threads/malloc.h"
#include "threads/palloc.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "userprog/process.h"
#include "userprog/pagedir.h"
#include "userprog/syscall.h"
#include "filesys/file.h"
#include "vm/frame.h"
#include "vm/page.h"
#include "vm/swap.h"
#include <string.h>
#include <stdio.h>

/*Since the sup_page_table will be implemented as a hash table, those three function is needed by the the Pintos specifican*/
/*Returns a hash of element's data, as a value anywhere in the range of unsigned int.*/
unsigned page_hash_func (const struct hash_elem *e, void *aux UNUSED){
	struct sup_page_table_entry *spte = hash_entry(e, struct sup_page_table_entry,elem);
   	return hash_int((int) spte->uvaddr);
 }

/*Function to compare the keys stored in elements a and b. Returns true if a is less than b, false if a is greater than or equal to b.
If two elements compare equal, then they must hash to equal values.*/
bool page_hash_less_func (const struct hash_elem *elem1,const struct hash_elem *elem2,void *aux UNUSED){
   	struct sup_page_table_entry *spte1 = hash_entry(elem1, struct sup_page_table_entry, elem);
   	struct sup_page_table_entry *spte2 = hash_entry(elem2, struct sup_page_table_entry, elem);
   	return spte1->uvaddr < spte2->uvaddr;
 }

/*Functions to perform free() action on hash elements.*/
void page_hash_action_func (struct hash_elem *e, void *aux UNUSED){
   struct sup_page_table_entry *spte = hash_entry(e, struct sup_page_table_entry, elem);
   free(spte);
}

/*Function to initialize the sup page table*/
void page_table_init (struct hash *sup_page_table){
	hash_init(sup_page_table, page_hash_func, page_hash_less_func, NULL);
}

/*Function to free the sup page table*/
void page_table_destroy (struct hash *sup_page_table){
	hash_destroy (sup_page_table, page_hash_action_func);
}

/*Function to find whether the current thread have the spte with given uvaddr inside*/
struct sup_page_table_entry * get_spte (const void *uvaddr){
   	struct sup_page_table_entry spte;
   	spte.uvaddr = pg_round_down(uvaddr);
   	//We need to find the beginning of the page in order to hash.
    struct hash_elem *e = hash_find(&thread_current()->sup_page_table, &spte.elem);
   	if (e==NULL)
   		return NULL;
   	//Current thread don't have the given page.
   	return hash_entry (e, struct sup_page_table_entry, elem);
}

bool page_load_swap (struct sup_page_table_entry * spte){
	uint8_t *frame = frame_allocate_user(spte);
    install_page(spte->uvaddr, frame, spte->writable);
    swap_in(spte->swap_index, spte->uvaddr);
    spte->is_loaded = true;
    return true;
}

/*Function to load page from a file*/
bool page_load_file (struct sup_page_table_entry * spte){
	// printf("inside page_load_file, loading for %p\n",spte->uvaddr);
	file_seek(spte->file, spte->offset);
	void *kpage = frame_allocate_user(spte);
	if(kpage == NULL) return false;  // Unknown error happened
	if(file_read (spte->file, kpage, spte->read_bytes) != (int) spte->read_bytes) {
		frame_free(kpage);
		return false;
	}
	memset (kpage + spte->read_bytes, 0, spte->zero_bytes);
	install_page(spte->uvaddr, kpage, spte->writable);
	spte->is_loaded = true;
  return true;
}

/*Function to load in page using specific functions*/
bool page_load (const void *uvaddr){
    struct sup_page_table_entry * spte = get_spte(uvaddr);
    //Using uvaddr and get_spte() to acquire the specific spet
    if (spte==NULL)
      return false;  
    return (spte->type==FILE||spte->type==MMAP)? page_load_file(spte): page_load_swap(spte);
 }


/* Allocate a new user page for current process. Initially allocates one new
    physical frame. Returns the success status. */
bool page_allocate_user(const void *upage, bool writable, struct sup_page_table_entry *spte) {
    void* uvpage = pg_round_down(upage);
    void *kpage = frame_allocate_user(spte);
    if(kpage == NULL) return false;  // Unknown error happened
    return install_page(uvpage, kpage, writable);
}

/*Function to impelement stack growth when new page is needed (with given uav inside)*/
bool page_grow_stack (const void *uvaddr){
  //First we check whether we have reached the preset upper bound of stack.
  struct sup_page_table_entry *spte = malloc(
    sizeof(struct sup_page_table_entry));
  if (spte==NULL)
    return false;
  //Failed to allowcate the sup page entry
  spte->uvaddr = pg_round_down(uvaddr);
  spte->is_loaded = true;
  spte->type = SWAP;
  spte->writable= true;

  if( !page_allocate_user(uvaddr, true, spte) ) {
      // Failed to allocate a new user page.
      free(spte);
      return false;
  }
  hash_insert(&thread_current()->sup_page_table, &spte->elem);
  thread_current()->bottom_of_allocated_stack = pg_round_down(uvaddr);
  return true;
  //If all the process is OK, we will hash insert the new page into current thread's list.
}

// grow the stack of current thread to the point of esp
void page_grow_to_esp(void* esp) {
    void* esp_stack = pg_round_down(esp);
    int npage_to_grow = (thread_current()->bottom_of_allocated_stack - esp_stack)/PGSIZE ;
    if(npage_to_grow <= 0) return;
    for(int i=0; i<npage_to_grow; i++) {
        page_grow_stack(thread_current()->bottom_of_allocated_stack - 1);
    }
}

// allocate new spte entries for userpages loaded files
bool page_grow_file(const void* uvaddr, struct file *file, off_t ofs, size_t page_read_bytes, size_t page_zero_bytes, bool writable) {
	struct sup_page_table_entry *spte = malloc(
	  sizeof(struct sup_page_table_entry));
	if (spte==NULL)
	  return false;
	//Failed to allowcate the sup page entry
	spte->uvaddr = pg_round_down(uvaddr);
	spte->is_loaded = false;
	spte->type = FILE;
	spte->writable = writable;
	spte->offset = ofs;
	spte->file = file;
	spte->read_bytes = page_read_bytes;
	spte->zero_bytes = page_zero_bytes;
	hash_insert(&thread_current()->sup_page_table, &spte->elem);
	return true;
}

// allocate new spte entries for
bool page_grow_mmap(const void* uvaddr, struct file *file, off_t ofs, size_t page_read_bytes, size_t page_zero_bytes) {
	struct sup_page_table_entry *spte = malloc(
	  sizeof(struct sup_page_table_entry));
	if (spte==NULL)
	  return false;
	//Failed to allowcate the sup page entry
	spte->uvaddr = pg_round_down(uvaddr);
	spte->is_loaded = false;
	spte->type = MMAP;
	spte->writable = true;
	spte->offset = ofs;
	spte->file = file;
	spte->read_bytes = page_read_bytes;
	spte->zero_bytes = page_zero_bytes;
	hash_insert(&thread_current()->sup_page_table, &spte->elem);
	return true;
}

struct sup_page_table_entry* mmap_release_page(void* uvaddr, struct file* f, int ofs, int write_bytes) {
	struct thread* t = thread_current();
	bool success = true;
	struct sup_page_table_entry *spte = get_spte(uvaddr);
	if(spte == NULL) return NULL;
	if(spte->is_loaded) {
		if(pagedir_is_dirty(t->pagedir, uvaddr)) {
			// write back
			lock_acquire(&file_lock);
			file_seek(f, ofs);
			if(file_write(f, uvaddr, write_bytes) != write_bytes) success = false;
			lock_release(&file_lock);
		}
		if(!success) return NULL;
		void* kpage = pagedir_get_page(thread_current()->pagedir, uvaddr);
		pagedir_clear_page(thread_current()->pagedir, uvaddr);
		frame_free(kpage);
	}
	return spte;
}

bool page_delete_spte(struct sup_page_table_entry* spte) {
	hash_delete(&thread_current()->sup_page_table, &spte->elem);
	free(spte);
	return true;
}
