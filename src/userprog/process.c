#include "userprog/process.h"
#include <debug.h>
#include <inttypes.h>
#include <round.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "userprog/gdt.h"
#include "userprog/pagedir.h"
#include "userprog/tss.h"
#include "userprog/syscall.h"
#include "filesys/directory.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "threads/flags.h"
#include "threads/init.h"
#include "threads/interrupt.h"
#include "threads/palloc.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "threads/malloc.h"
#include "vm/page.h"

static thread_func start_process NO_RETURN;
static bool load (struct arguments *args, void (**eip) (void), void **esp);

static struct chld_stat* get_chldstat_from_tid(tid_t tid) {
    struct thread* current = thread_current();
    for(struct list_elem* iter = list_begin(&current->children);
        iter != list_end(&current->children);
        iter = list_next(iter)) {
    //do stuff with iter
    struct chld_stat* this_chld = list_entry(iter, struct chld_stat, elem);
    if(this_chld->tid == tid) {
        return this_chld;
    }
    }
    // if not found, return NULL
    return NULL;
}

/* Starts a new thread running a user program loaded from
   FILENAME.  The new thread may be scheduled (and may even exit)
   before process_execute() returns.  Returns the new process's
   thread id, or TID_ERROR if the thread cannot be created. */
tid_t
process_execute (const char *file_name)
{
  char *fn_copy;
  tid_t tid;

  /* Make a copy of FILE_NAME.
     Otherwise there's a race between the caller and load(). */
  fn_copy = palloc_get_page (0);
  if (fn_copy == NULL)
    return TID_ERROR;
  strlcpy (fn_copy, file_name, PGSIZE);

  /* Get executable name */
  char *exec_name = (char*)NULL, *argstring = (char*)NULL;
  exec_name = strtok_r(fn_copy, " ", &argstring);

  struct semaphore chld_sema;
  sema_init(&chld_sema, 0);

  struct arguments *args = malloc(sizeof(struct arguments));
  args->exec_name = exec_name;
  args->args = argstring;
  args->fn_copy = fn_copy;
  args->parent = thread_current();
  args->chld_sema = &chld_sema;
  /* Create a new thread to execute FILE_NAME. */
  tid = thread_create (exec_name, PRI_DEFAULT, start_process, (void *)args);
  if (tid == TID_ERROR) {
    palloc_free_page (fn_copy);
    free(args);
  }
  sema_down(&chld_sema);
  if(!thread_current()->exec_chld_cond) tid = -1; // load failed
  return tid;
}

/* A thread function that loads a user process and starts it
   running. */
static void
start_process (void *args)
{
  struct arguments *arg_struct = (struct arguments *)args;
  struct intr_frame if_;
  bool success;
  struct thread *parent = arg_struct->parent;
  struct semaphore *chld_sema = arg_struct->chld_sema;

  // Initialize the page table
  page_table_init(&thread_current()->sup_page_table);

  /* Initialize interrupt frame and load executable. */
  memset (&if_, 0, sizeof if_);
  if_.gs = if_.fs = if_.es = if_.ds = if_.ss = SEL_UDSEG;
  if_.cs = SEL_UCSEG;
  if_.eflags = FLAG_IF | FLAG_MBS;
  success = load (arg_struct, &if_.eip, &if_.esp);

  /* If load failed, quit. */
  palloc_free_page (arg_struct->fn_copy);
  free(arg_struct);
  if (!success) {
    if(chld_sema!= NULL) sema_up(chld_sema);
    thread_exit ();
  }

  // Load successful
  parent->exec_chld_cond = true;
  struct thread* current = thread_current();
  struct chld_stat* t_stat = malloc(sizeof(struct chld_stat));
  t_stat->t = current;
  t_stat->tid = current->tid;
  t_stat->terminated = false;
  t_stat->killed_by_kernel = false;
  t_stat->exit_status = 0;
  current->my_stat = t_stat;

  enum intr_level old_level = intr_disable();

  list_push_back(&parent->children, &current->my_stat->elem);
  current->parent = parent;

  intr_set_level(old_level);
  if(chld_sema!= NULL) sema_up(chld_sema);

  /* Start the user process by simulating a return from an
     interrupt, implemented by intr_exit (in
     threads/intr-stubs.S).  Because intr_exit takes all of its
     arguments on the stack in the form of a `struct intr_frame',
     we just point the stack pointer (%esp) to our stack frame
     and jump to it. */
  asm volatile ("movl %0, %%esp; jmp intr_exit" : : "g" (&if_) : "memory");
  NOT_REACHED ();
}

/* Waits for thread TID to die and returns its exit status.  If
   it was terminated by the kernel (i.e. killed due to an
   exception), returns -1.  If TID is invalid or if it was not a
   child of the calling process, or if process_wait() has already
   been successfully called for the given TID, returns -1
   immediately, without waiting.

   This function will be implemented in problem 2-2.  For now, it
   does nothing. */
int
process_wait (tid_t child_tid)
{
  struct thread* current = thread_current();
  struct chld_stat* chld = get_chldstat_from_tid(child_tid);
  if(chld == NULL)  return -1;
  lock_acquire(&current->wait_lock);
  while(chld->terminated != true) {
      cond_wait(&current->wait_cond, &current->wait_lock);
  }
  lock_release(&current->wait_lock);
  int exit_status;
  if(chld->killed_by_kernel == true) exit_status = -1;
  else exit_status = chld->exit_status;
  // remove chld from children's list
  list_remove(&chld->elem);
  // clean zombies
  free(chld);
  return exit_status;

  // int i=0;
  // while(i < 100000) {i++;thread_yield();}
  // return 0;
}

/* Free the current process's resources. */
void
process_exit (void)
{
  struct thread *cur = thread_current ();
  uint32_t *pd;
  if(cur->my_exec != NULL) file_close(cur->my_exec);

  /* Close all opened files */
  for(struct list_elem* iter = list_begin(&cur->file_desc);
      iter != list_end(&cur->file_desc);) {
          struct file_desc* this_file = list_entry(iter, struct file_desc, elem);
          file_close(this_file->file);
          iter = list_next(iter);
          free(this_file);
  }

  #ifdef VM
    // Unmap all mmap files
    for(struct list_elem* iter = list_begin(&cur->mmap_desc);
        iter != list_end(&cur->mmap_desc);) {
            struct mmap_desc* md = list_entry(iter, struct mmap_desc, elem);
            for (void* i = md->addr; i < md->addr+md->n_pages*PGSIZE; i+=PGSIZE) {
                struct sup_page_table_entry *spte = get_spte(i);
                mmap_release_page(i, spte->file, spte->offset, spte->read_bytes);
                //remove spte from supplementary page table
                page_delete_spte(spte);
            }
            file_close(md->file);
            iter = list_next(iter);
            free(md);
    }
  #endif

  enum intr_level old_level = intr_disable();
  /* Free chld_stat resources */
  for(struct list_elem* iter = list_begin(&cur->children);
      iter != list_end(&cur->children);) {
          struct chld_stat* this_chld = list_entry(iter, struct chld_stat, elem);
          if(!is_thread(this_chld->t)) {
              free(this_chld);
          }
          iter = list_next(iter);
  }
  intr_set_level(old_level);

  if(!is_thread(cur->parent)) free(cur->my_stat);
  else {
      struct lock *plock = &cur->parent->wait_lock;
      lock_acquire(plock);
      cur->my_stat->terminated = true;
      cond_signal(&cur->parent->wait_cond, plock);
      lock_release(plock);
  }
  //Destroy the page table
  page_table_destroy(&cur->sup_page_table);

  /* Destroy the current process's page directory and switch back
     to the kernel-only page directory. */
  pd = cur->pagedir;
  if (pd != NULL)
    {
      /* Correct ordering here is crucial.  We must set
         cur->pagedir to NULL before switching page directories,
         so that a timer interrupt can't switch back to the
         process page directory.  We must activate the base page
         directory before destroying the process's page
         directory, or our active page directory will be one
         that's been freed (and cleared). */
      cur->pagedir = NULL;
      pagedir_activate (NULL);
      pagedir_destroy (pd);
    }
}

/* Sets up the CPU for running user code in the current
   thread.
   This function is called on every context switch. */
void
process_activate (void)
{
  struct thread *t = thread_current ();

  /* Activate thread's page tables. */
  pagedir_activate (t->pagedir);

  /* Set thread's kernel stack for use in processing
     interrupts. */
  tss_update ();
}

/* We load ELF binaries.  The following definitions are taken
   from the ELF specification, [ELF1], more-or-less verbatim.  */

/* ELF types.  See [ELF1] 1-2. */
typedef uint32_t Elf32_Word, Elf32_Addr, Elf32_Off;
typedef uint16_t Elf32_Half;

/* For use with ELF types in printf(). */
#define PE32Wx PRIx32   /* Print Elf32_Word in hexadecimal. */
#define PE32Ax PRIx32   /* Print Elf32_Addr in hexadecimal. */
#define PE32Ox PRIx32   /* Print Elf32_Off in hexadecimal. */
#define PE32Hx PRIx16   /* Print Elf32_Half in hexadecimal. */

/* Executable header.  See [ELF1] 1-4 to 1-8.
   This appears at the very beginning of an ELF binary. */
struct Elf32_Ehdr
  {
    unsigned char e_ident[16];
    Elf32_Half    e_type;
    Elf32_Half    e_machine;
    Elf32_Word    e_version;
    Elf32_Addr    e_entry;
    Elf32_Off     e_phoff;
    Elf32_Off     e_shoff;
    Elf32_Word    e_flags;
    Elf32_Half    e_ehsize;
    Elf32_Half    e_phentsize;
    Elf32_Half    e_phnum;
    Elf32_Half    e_shentsize;
    Elf32_Half    e_shnum;
    Elf32_Half    e_shstrndx;
  };

/* Program header.  See [ELF1] 2-2 to 2-4.
   There are e_phnum of these, starting at file offset e_phoff
   (see [ELF1] 1-6). */
struct Elf32_Phdr
  {
    Elf32_Word p_type;
    Elf32_Off  p_offset;
    Elf32_Addr p_vaddr;
    Elf32_Addr p_paddr;
    Elf32_Word p_filesz;
    Elf32_Word p_memsz;
    Elf32_Word p_flags;
    Elf32_Word p_align;
  };

/* Values for p_type.  See [ELF1] 2-3. */
#define PT_NULL    0            /* Ignore. */
#define PT_LOAD    1            /* Loadable segment. */
#define PT_DYNAMIC 2            /* Dynamic linking info. */
#define PT_INTERP  3            /* Name of dynamic loader. */
#define PT_NOTE    4            /* Auxiliary info. */
#define PT_SHLIB   5            /* Reserved. */
#define PT_PHDR    6            /* Program header table. */
#define PT_STACK   0x6474e551   /* Stack segment. */

/* Flags for p_flags.  See [ELF3] 2-3 and 2-4. */
#define PF_X 1          /* Executable. */
#define PF_W 2          /* Writable. */
#define PF_R 4          /* Readable. */

static bool setup_stack (void **esp, struct arguments * args);
static bool validate_segment (const struct Elf32_Phdr *, struct file *);
static bool load_segment (struct file *file, off_t ofs, uint8_t *upage,
                          uint32_t read_bytes, uint32_t zero_bytes,
                          bool writable);

/* Loads an ELF executable from FILE_NAME into the current thread.
   Stores the executable's entry point into *EIP
   and its initial stack pointer into *ESP.
   Returns true if successful, false otherwise. */
bool
load (struct arguments *args, void (**eip) (void), void **esp)
{
  struct thread *t = thread_current ();
  struct Elf32_Ehdr ehdr;
  struct file *file = NULL;
  off_t file_ofs;
  bool success = false;
  int i;

  char * exec_name = args->exec_name;

  /* Allocate and activate page directory. */
  t->pagedir = pagedir_create ();
  if (t->pagedir == NULL)
    goto done;
  process_activate ();

  /* Open executable file. */
  file = filesys_open (exec_name);
  if (file == NULL)
    {
      printf ("load: %s: open failed\n", exec_name);
      goto done;
    }

  /* Read and verify executable header. */
  if (file_read (file, &ehdr, sizeof ehdr) != sizeof ehdr
      || memcmp (ehdr.e_ident, "\177ELF\1\1\1", 7)
      || ehdr.e_type != 2
      || ehdr.e_machine != 3
      || ehdr.e_version != 1
      || ehdr.e_phentsize != sizeof (struct Elf32_Phdr)
      || ehdr.e_phnum > 1024)
    {
      printf ("load: %s: error loading executable\n", exec_name);
      goto done;
    }

  /* Read program headers. */
  file_ofs = ehdr.e_phoff;
  for (i = 0; i < ehdr.e_phnum; i++)
    {
      struct Elf32_Phdr phdr;

      if (file_ofs < 0 || file_ofs > file_length (file))
        goto done;
      file_seek (file, file_ofs);

      if (file_read (file, &phdr, sizeof phdr) != sizeof phdr)
        goto done;
      file_ofs += sizeof phdr;
      switch (phdr.p_type)
        {
        case PT_NULL:
        case PT_NOTE:
        case PT_PHDR:
        case PT_STACK:
        default:
          /* Ignore this segment. */
          break;
        case PT_DYNAMIC:
        case PT_INTERP:
        case PT_SHLIB:
          goto done;
        case PT_LOAD:
          if (validate_segment (&phdr, file))
            {
              bool writable = (phdr.p_flags & PF_W) != 0;
              uint32_t file_page = phdr.p_offset & ~PGMASK;
              uint32_t mem_page = phdr.p_vaddr & ~PGMASK;
              uint32_t page_offset = phdr.p_vaddr & PGMASK;
              uint32_t read_bytes, zero_bytes;
              if (phdr.p_filesz > 0)
                {
                  /* Normal segment.
                     Read initial part from disk and zero the rest. */
                  read_bytes = page_offset + phdr.p_filesz;
                  zero_bytes = (ROUND_UP (page_offset + phdr.p_memsz, PGSIZE)
                                - read_bytes);
                }
              else
                {
                  /* Entirely zero.
                     Don't read anything from disk. */
                  read_bytes = 0;
                  zero_bytes = ROUND_UP (page_offset + phdr.p_memsz, PGSIZE);
                }
              if (!load_segment (file, file_page, (void *) mem_page,
                                 read_bytes, zero_bytes, writable))
                goto done;
            }
          else
            goto done;
          break;
        }
    }

  /* Set up stack. */
  if (!setup_stack (esp, args)) {
        file_close (file);
        goto done;
    }

  t->my_exec = file;
  file_deny_write(file);
  /* Start address. */
  *eip = (void (*) (void)) ehdr.e_entry;

  success = true;

 done:
  /* We arrive here whether the load is successful or not. */
  return success;
}


/* Checks whether PHDR describes a valid, loadable segment in
   FILE and returns true if so, false otherwise. */
static bool
validate_segment (const struct Elf32_Phdr *phdr, struct file *file)
{
  /* p_offset and p_vaddr must have the same page offset. */
  if ((phdr->p_offset & PGMASK) != (phdr->p_vaddr & PGMASK))
    return false;

  /* p_offset must point within FILE. */
  if (phdr->p_offset > (Elf32_Off) file_length (file))
    return false;

  /* p_memsz must be at least as big as p_filesz. */
  if (phdr->p_memsz < phdr->p_filesz)
    return false;

  /* The segment must not be empty. */
  if (phdr->p_memsz == 0)
    return false;

  /* The virtual memory region must both start and end within the
     user address space range. */
  if (!is_user_vaddr ((void *) phdr->p_vaddr))
    return false;
  if (!is_user_vaddr ((void *) (phdr->p_vaddr + phdr->p_memsz)))
    return false;

  /* The region cannot "wrap around" across the kernel virtual
     address space. */
  if (phdr->p_vaddr + phdr->p_memsz < phdr->p_vaddr)
    return false;

  /* Disallow mapping page 0.
     Not only is it a bad idea to map page 0, but if we allowed
     it then user code that passed a null pointer to system calls
     could quite likely panic the kernel by way of null pointer
     assertions in memcpy(), etc. */
  if (phdr->p_vaddr < PGSIZE)
    return false;

  /* It's okay. */
  return true;
}

/* Loads a segment starting at offset OFS in FILE at address
   UPAGE.  In total, READ_BYTES + ZERO_BYTES bytes of virtual
   memory are initialized, as follows:

        - READ_BYTES bytes at UPAGE must be read from FILE
          starting at offset OFS.

        - ZERO_BYTES bytes at UPAGE + READ_BYTES must be zeroed.

   The pages initialized by this function must be writable by the
   user process if WRITABLE is true, read-only otherwise.

   Return true if successful, false if a memory allocation error
   or disk read error occurs. */
static bool
load_segment (struct file *file, off_t ofs, uint8_t *upage,
              uint32_t read_bytes, uint32_t zero_bytes, bool writable)
{
  ASSERT ((read_bytes + zero_bytes) % PGSIZE == 0);
  ASSERT (pg_ofs (upage) == 0);
  ASSERT (ofs % PGSIZE == 0);

  while (read_bytes > 0 || zero_bytes > 0)
    {
      /* Calculate how to fill this page.
         We will read PAGE_READ_BYTES bytes from FILE
         and zero the final PAGE_ZERO_BYTES bytes. */
      size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
      size_t page_zero_bytes = PGSIZE - page_read_bytes;

      // /* Get a page of memory. */
      // uint8_t *kpage = palloc_get_page (PAL_USER);
      // if (kpage == NULL)
      //   return false;
      //
      // /* Load this page. */
      // if (file_read (file, kpage, page_read_bytes) != (int) page_read_bytes)
      //   {
      //     palloc_free_page (kpage);
      //     return false;
      //   }
      if(!page_grow_file(upage, file, ofs, page_read_bytes, page_zero_bytes, writable)) return false;

      // /* Add the page to the process's address space. */
      // if (!install_page (upage, kpage, writable))
      //   {
      //     palloc_free_page (kpage);
      //     return false;
      //   }

      /* Advance. */
      read_bytes -= page_read_bytes;
      zero_bytes -= page_zero_bytes;
      upage += PGSIZE;
      ofs += page_read_bytes;
    }
  return true;
}

/* Create a minimal stack by mapping a zeroed page at the top of
   user virtual memory. */
static bool
setup_stack (void **esp, struct arguments * args)
{
  bool success = false;
  success = page_grow_stack(PHYS_BASE-1);
  thread_current()->bottom_of_allocated_stack = pg_round_down(PHYS_BASE-1);
  if (success) {
      *esp = PHYS_BASE;

      /* Writes argument into the stack */
      /* Delimit arguments and push them onto stack */
      int argc = 1;
      // push argstring
      char* argstring = args->args;
      int args_len = 0;
      if (argstring != NULL) {
        args_len = strlen(argstring);
        int tailptr = args_len-1;
        while(argstring[tailptr] == ' ' || argstring[tailptr] == '\n' || argstring[tailptr] == '\r') {
            argstring[tailptr] = '\0';
            args_len--;
            tailptr--;
        }
        if(args_len != 0) {
            argc +=1;
            for(int i=0; i<args_len; i++) {
                if(argstring[i] == ' ') {
                    argstring[i] = '\0';
                    argc+=1;
                    while(i+1 < args_len && argstring[i+1] == ' ') {i++; argstring[i] = '\0';}
                }
            }
            *esp -= args_len + 1;
            memcpy(*esp, argstring, args_len+1);
        }
      }
      // push exec_name
      char* exec_name = args->exec_name;
      int exec_len = strlen(exec_name);
      *esp -= exec_len+1;
      memcpy(*esp, exec_name, exec_len+1);
      // alignment
      int total_len = exec_len+1;
      if(args_len != 0) total_len += args_len +1;
      int padding_len = 4 - total_len % 4;
      if(padding_len != 4) {
          *esp -= padding_len;
          memset(*esp, 0, padding_len);
      }
      char * argstring_end = *esp;
      // last argv: 0
      *esp -= 4;
      memset(*esp, 0, 4);
      // push pointers of arguments
      int args_count = 0;
      // *esp -= argc*4;
      char* args_ptr = PHYS_BASE - 2;
      while(args_ptr >= argstring_end-1) {
          if(*args_ptr == '\0') {
              char* string_ptr = args_ptr + 1;
              if(strlen(string_ptr)!= 0) {
                  *esp -= 4;
                  memcpy(*esp , &string_ptr, sizeof(char *));
                  args_count ++;
                  if(args_count == argc) break;
              }
          }
          args_ptr --;
      }
      // push argv
      *esp -= 4;
      char* argc_addr = (*esp)+4;
      memcpy(*esp, &(argc_addr), sizeof(char *));
      // push argc
      *esp -= 4;
      memcpy(*esp, &argc, sizeof(int));
      // push a NULL return ptr
      *esp -= 4;
      memset(*esp, 0, sizeof(char *));
      // hex_dump(*esp, *esp, 60, true);
  }
    return success;
}

/* Adds a mapping from user virtual address UPAGE to kernel
   virtual address KPAGE to the page table.
   If WRITABLE is true, the user process may modify the page;
   otherwise, it is read-only.
   UPAGE must not already be mapped.
   KPAGE should probably be a page obtained from the user pool
   with palloc_get_page().
   Returns true on success, false if UPAGE is already mapped or
   if memory allocation fails. */
bool
install_page (void *upage, void *kpage, bool writable)
{
  struct thread *t = thread_current ();

  /* Verify that there's not already a page at that virtual
     address, then map our page there. */
  return (pagedir_get_page (t->pagedir, upage) == NULL
          && pagedir_set_page (t->pagedir, upage, kpage, writable));
}
