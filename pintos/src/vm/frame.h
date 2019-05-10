#ifndef VM_FRAME_H
#define VM_FRAME_H

#include "threads/palloc.h"
#include "threads/synch.h"
#include <hash.h>

struct list frame_list;

struct lock frame_lock;

struct fte{
  void *frame;                    /* physical address */
  struct spte *spte;              /* spte of occupying page */
  struct list_elem elem;          /* elements of frame_list */
  struct thread* thread; 
};

void init_frame_table(void);
void free_frame_table(void *);
void add_frame_table(void *, struct spte *);
void* find_victim_frame(enum palloc_flags flags);
void* frame_alloc(enum palloc_flags flags, struct spte *);

#endif /* vm/frame.h */
