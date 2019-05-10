#include "filesys/file.h"
#include "threads/malloc.h"
#include "threads/palloc.h"
#include "threads/synch.h"
#include "threads/thread.h"
#include "userprog/pagedir.h"
#include "userprog/syscall.h"
#include "userprog/process.h"
#include "vm/frame.h"
#include <hash.h>
#include "vm/page.h"
#include "vm/swap.h"

void init_frame_table(void){
  lock_init(&frame_lock);
  list_init(&frame_list);
}

/* deleting of frame_table_entry */
void free_frame_table(void *frame){
  struct fte *fte1;
  struct list_elem *elem;
  
  lock_acquire(&frame_lock);

  for(elem = list_begin(&frame_list); elem != list_end(&frame_list); elem = list_next(elem)){
    fte1 = list_entry(elem, struct fte, elem);
    if(fte1->frame == frame){
      list_remove(elem);
      free(fte1);
      palloc_free_page(frame);
      break;
    }
  }

  lock_release(&frame_lock);
}

/* adding of frame_table_entry */
void add_frame_table(void *frame, struct spte *spte1){
  struct fte *fte1 = malloc(sizeof(struct fte));
  fte1->frame = frame;
  fte1->spte = spte1;
  fte1->thread = thread_current();
  lock_acquire(&frame_lock);
  list_push_back(&frame_list, &fte1->elem);
  lock_release(&frame_lock);
}

/* finding and evicting of victim */
void *find_victim_frame(enum palloc_flags flags){
  struct list_elem *e = list_begin(&frame_list);
  struct fte *fte1;
  struct spte *spte1;
  bool dirty_flag;

  while(1)
  {
    fte1 = list_entry(e, struct fte, elem);
    spte1 = fte1->spte;
    if(!spte1->accessing){
      if(pagedir_is_accessed(fte1->thread->pagedir, spte1->page))
        pagedir_set_accessed(fte1->thread->pagedir, spte1->page, false);
      else
      {
        dirty_flag = false;
        if(spte1->from_mmap){
          dirty_flag = pagedir_is_dirty(fte1->thread->pagedir, spte1->page);
          pagedir_clear_page(fte1->thread->pagedir, spte1->page);
          spte1->on_type = 1;
          
          /* write back to mmap file */
          if(dirty_flag){
            lock_acquire(&filesys_lock);
            file_write_at(spte1->file, fte1->frame, spte1->read_bytes, spte1->ofs);
            lock_release(&filesys_lock);
          }

          list_remove(&fte1->elem);
          palloc_free_page(fte1->frame);
          free(fte1);
          
          return palloc_get_page(flags);
        }
        else if(spte1->writable){ 
          pagedir_clear_page(fte1->thread->pagedir, spte1->page);
          spte1->on_type = 2;
          
          /* write to swap disk */
          spte1->swap_index = swap_out(fte1->frame);
        
          list_remove(&fte1->elem);
          palloc_free_page(fte1->frame);
          free(fte1);
          return palloc_get_page(flags);
        }
      }
      e = list_next(e);
      if(e == list_end(&frame_list))
        e = list_begin(&frame_list);
    }
  }
}

/* bring frame using palloc_get_page
  if not exists, swap out and bring */
void* frame_alloc(enum palloc_flags flags, struct spte *spte1){
  void *frame = palloc_get_page(flags);
  if(frame != NULL)
    add_frame_table(frame, spte1);
  
  else
  {
    while(!frame)
    {
      lock_acquire(&frame_lock);
      frame = find_victim_frame(flags);
      lock_release(&frame_lock);
    }
    
    if(!frame)
      PANIC("find_victim_frame : Swap disk is full");
    
    add_frame_table(frame, spte1);
  }

  return frame;
}
