#ifndef VM_PAGE_H
#define VM_PAGE_H

#include "filesys/file.h"
#define MAX_STACK_SIZE (1<<23)
struct spte{
  void *page;
  void *frame;
  int on_type;      /* 0 means on memory, 1 means on file, 2 means on swap disk */
  struct file *file;
  off_t ofs;
  uint32_t read_bytes;
  uint32_t zero_bytes;
  bool writable;
  bool from_mmap;   /* true if the backing store is memory mapped file */
  size_t swap_index;
  bool accessing;      /* true if it's in page_fault of syscall */
  struct hash_elem hash_elem;
};

void init_spt(void);
unsigned page_hash(const struct hash_elem *e, void *aux);
bool page_less(const struct hash_elem *a_, const struct hash_elem *b_, void *aux);
void destroy_spt(void);
void destroy_hash_action_func(struct hash_elem *e, void *aux UNUSED);
struct spte* find_spte(void *page);
bool add_spte(void *page, struct file *file, off_t ofs, uint32_t read_bytes,
              uint32_t zero_bytes, bool writable, bool from_mmap);
bool load_from_file(struct spte *spte1);
bool load_from_swap_disk(struct spte *spte1);
bool stack_grow(void *page);
#endif
