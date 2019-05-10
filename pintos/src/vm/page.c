#include <stdio.h>
#include <string.h>
#include "vm/frame.h"
#include "vm/page.h"
#include "vm/swap.h"
#include "filesys/file.h"
#include "threads/malloc.h"
#include "threads/thread.h"
#include "userprog/pagedir.h"
#include "threads/vaddr.h"
#include "userprog/process.h"

/* hash function for supplemental page table */
unsigned page_hash(const struct hash_elem *e, void *aux UNUSED)
{
  const struct spte *spte1 = hash_entry(e, struct spte, hash_elem);
  return hash_int((int) spte1->page);
}

/* returns true if spte a precedes spte b */
bool page_less(const struct hash_elem *a_, const struct hash_elem *b_, void *aux UNUSED)
{
  const struct spte *a = hash_entry(a_, struct spte, hash_elem);
  const struct spte *b = hash_entry(b_, struct spte, hash_elem);

  return a->page < b->page;

}

/* initiate current thread's supplemental page table */
void init_spt()
{
  hash_init(&thread_current()->spt, page_hash, page_less, NULL);
}

/* destroy current thread's supplemental page table
   and clear pages & frames and modify frame table */
void destroy_spt()
{
  hash_destroy(&thread_current()->spt, destroy_hash_action_func);
}

void destroy_hash_action_func(struct hash_elem *e, void *aux UNUSED)
{
  struct thread* t = thread_current();
  struct spte *spte1 = hash_entry(e, struct spte, hash_elem);
  if(spte1->on_type==0){//no need to for 'ontype = 1 or 2' -> they don't have fte
    free_frame_table(pagedir_get_page(t->pagedir, spte1->page));
    pagedir_clear_page(t->pagedir, spte1->page);
  }
  free(spte1);
}

/* find spte including the page from current thread's supplemental
   page table. if it exists, return it */
struct spte * find_spte(void *page){
  struct hash *spt = &thread_current()->spt;
  struct spte spte1;
  struct hash_elem *e;
  spte1.page = pg_round_down(page);

  if((e=hash_find(spt, &spte1.hash_elem)) != NULL){
    return hash_entry(e, struct spte, hash_elem);
  }

  return NULL;
}

/* add spte including the page to supplemental page table. 
   only load_segment() and mmap() call this function, so
   on_type is always 1, accessing should be false at first */
bool add_spte(void *page, struct file *file, off_t ofs, uint32_t read_bytes,
              uint32_t zero_bytes, bool writable, bool from_mmap){
  struct hash *spt = &thread_current()->spt;
  struct spte *spte1 = malloc(sizeof *spte1);
  spte1->page = page;
  spte1->file = file;
  spte1->ofs = ofs;
  spte1->read_bytes = read_bytes;
  spte1->zero_bytes = zero_bytes;
  spte1->writable = writable;
  spte1->from_mmap = from_mmap;
  spte1->on_type = 1;
  spte1->accessing = false;
  return (hash_insert(spt, &spte1->hash_elem)==NULL);
}

bool load_from_file(struct spte *spte1){
  /* Get a page of memory */
  uint8_t *kpage = frame_alloc(PAL_USER, spte1); /* code and data segment */
  if(kpage == NULL)
    return false;

  /* Load this page */
  lock_acquire(&filesys_lock);
  if(file_read_at(spte1->file, kpage, spte1->read_bytes, spte1->ofs) != (int) spte1->read_bytes){
    lock_release(&filesys_lock);
    free_frame_table(kpage);
    return false;
  }
  lock_release(&filesys_lock);
  memset(kpage + (spte1->read_bytes), 0, spte1->zero_bytes);

  /* Add the page to the process's address space */
  if(!install_page(spte1->page, kpage, spte1->writable)){
    free_frame_table(kpage);
    return false;
  }
  
  /* Update the spte */
  spte1->on_type = 0;
  spte1->frame = kpage;

  return true;
}

bool load_from_swap_disk(struct spte *spte1){
  /* Get a page of memory */
  uint8_t *kpage = frame_alloc(PAL_USER, spte1);
  if(kpage == NULL)
    return false;

  /* Add the page to the process's address space */
  if(!install_page(spte1->page, kpage, spte1->writable))
  {
    free_frame_table(kpage);
    return false;
  }
  
  swap_in(spte1->page, spte1->swap_index);

  /* Update the spte */
  spte1->on_type = 0;
  spte1->frame = kpage;
  
  return true;
}

bool stack_grow(void* page)
{
  uint8_t *kpage;
  void *upage = pg_round_down(page);
  
  if((size_t) (PHYS_BASE - upage) > MAX_STACK_SIZE)
  {
    return false;
  }
  
  /* Make supplemental page table entry */
  struct spte *spte1 = malloc(sizeof *spte1);
  if(!spte1)
    return false;
  spte1->page = upage;
  spte1->on_type = 0;
  spte1->writable = true;
  spte1->from_mmap = false;
  spte1->accessing = false;

  /* Get a page of memory */
  kpage = frame_alloc (PAL_USER | PAL_ZERO, spte1);
  if(kpage == NULL)
  {
    free(spte1);
    return false;
  }
  
  /*Add the page to the process's address space */
  bool success = install_page(upage, kpage, true);
  if(!success){
    free(spte1);
    free_frame_table(kpage);
    return false;
  }
  spte1->frame = kpage;

  /* Add spte to supplemental page table */
  if(hash_insert(&thread_current()->spt, &spte1->hash_elem) != NULL)
    PANIC("stack grow: update unexpectedly failed");
  
  return true;
}
