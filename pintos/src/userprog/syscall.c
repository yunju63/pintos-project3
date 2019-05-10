#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include <string.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "threads/init.h"
#include "userprog/process.h"
#include "filesys/filesys.h"
#include "vm/frame.h"
#include "vm/page.h"
#include "vm/swap.h"

struct lock filesys_lock;

static void syscall_handler (struct intr_frame *);
void halt(void);
void exit(int status);
pid_t wait(pid_t pid);
int write(int fd, const void *buffer, unsigned size);
bool create(const char *file, unsigned initial_size);

int check_valid_pointer(const void* page, uint8_t num_byte)
{
  void *ptr;
  uint8_t i;
  for(i = 0; i < num_byte; i++){
    if(!is_user_vaddr(page+i) || page+i == NULL || page+i < (void *) 0x08048000)
      return 0;
    
    struct spte *spte1 = find_spte(page+i);
    
    if(spte1!=NULL){
      /* load from executable or mmaped file */
      if(spte1->on_type == 1)
        if(!load_from_file(spte1))
          return 0;
      /* swap in */
      if(spte1->on_type == 2)
        if(!load_from_swap_disk(spte1))
          return 0;
    }
    /* stack growth */
    else if(page+i >= page-32){ //32 is HEURISTIC
      if(!stack_grow(page+i))
        return 0;
    }
    else
      return 0;
  }
  return 1;
}

struct spte* check_valid_buffer_helper(const void* page, void* esp)
{
  void *ptr;
  uint8_t i;
  struct spte *spte1 = NULL;
  struct spte *temp_spte1 = NULL;
  
  
  if(!is_user_vaddr(page) || page == NULL || page < (void *) 0x08048000)
    exit(-1);

  spte1 = find_spte(page+i);
    
  if(spte1 != NULL){
    temp_spte1 = spte1;
    /* load from executable or mmaped file */
    if(spte1->on_type == 1)
      if(!load_from_file(spte1))
        exit(-1);
    /* swap in */
    if(spte1->on_type == 2)
      if(!load_from_swap_disk(spte1))
        exit(-1);
  }
    
  else if(page+i >= esp - 32){
    /* stack growth */
    if(!stack_grow(page+i))
      exit(-1);
    temp_spte1 = find_spte(page+i);
  }
  else
    exit(-1);

  return temp_spte1;
}

void check_valid_buffer (void* buffer, unsigned* size, void* esp, bool to_write)
{
  unsigned i;
  char* local_buffer = (char *) buffer;
  char* local_buffer_last = (char *) buffer;
  local_buffer_last = local_buffer_last + (*size) -1;

 
    for (i = 0; i < ((*size)/2) ; i++)
    {
      struct spte *spte = check_valid_buffer_helper((const void*) local_buffer, esp);
      if(spte == NULL)
        exit(-1);
    
      if (to_write)
        if (!spte->writable)
          exit(-1);
        
      local_buffer=local_buffer + 2;
    }
    struct spte* spte2 = check_valid_buffer_helper((const void*) local_buffer_last, esp);
    if(spte2 == NULL)
      exit(-1);
    if(to_write)
      if(!spte2->writable)
        exit(-1);
}

void halt()
{
  power_off();
}

void exit(int status)
{
  struct thread* curr;
  curr = thread_current();
  curr->exit_status = status;
 
  struct list* parent_list = &(curr->parent)->child_list;
  struct list_elem* elem;
  struct child_elem *ce1;
  struct child_elem *ce2;
 
  ce2 = NULL;
  for(elem = list_begin(parent_list); elem != list_end(parent_list); elem=list_next(elem)){
    ce1 = list_entry(elem, struct child_elem, e);
    if(curr->tid == ce1->tid){
      ce2 = ce1;
      ce2->exit_status = status;
      break;
    }
  }
  curr->exit_status = status;
  if(curr->parent->waiting_tid == curr->tid)
  {
    sema_up(&curr->parent->child_lock);
    curr->parent->waiting_tid = -1;
  }
  thread_exit();
}

pid_t exec(const char *cmd_line){
  
  char *fn, *saveptr;
  struct file * f;

  lock_acquire(&filesys_lock);
  
  fn = malloc(strlen(cmd_line)+1);
  strlcpy(fn, cmd_line, strlen(cmd_line)+1);
  fn = strtok_r(fn," ",&saveptr);

  f = filesys_open(fn);

  if(f=NULL){
    lock_release(&filesys_lock);
    return -1;
  }
  
  file_close(f);
  lock_release(&filesys_lock);
  
  return process_execute(cmd_line);
}

pid_t wait(pid_t pid)
{
  return process_wait(pid);
}

bool create(const char *file, unsigned initial_size)
{
  bool success;
  lock_acquire(&filesys_lock);
  success = filesys_create(file, initial_size);
  lock_release(&filesys_lock);
  return success;
}

bool remove(const char *file)
{
  bool success;
  lock_acquire(&filesys_lock);
  success = filesys_remove(file);
  lock_release(&filesys_lock);
  return success;
}
int open(const char *file)
{
  struct fd_elem *fd1 = malloc(sizeof(*fd1));
  struct thread * curr;
  struct file * f;
  
  lock_acquire(&filesys_lock);
  f = filesys_open(file);
  lock_release(&filesys_lock);
  
  if(f==NULL)
    return -1;
  
  curr = thread_current();
  fd1->fd = curr->fd_count;
  fd1->f = f;
  list_push_back(&curr->fd_list, &fd1->e);
  curr->fd_count ++;
  return fd1->fd;
}

struct fd_elem * find_fd(struct list * l, int fd){
  struct list_elem * elem;
  struct fd_elem * fd1;
  for(elem = list_begin(l); elem != list_end(l); elem = list_next(elem)){
    fd1 = list_entry(elem, struct fd_elem, e);
    if(fd1->fd == fd){
      return fd1;
    }
  }
  return NULL;
}

int filesize(int fd){
  struct fd_elem * fd1;
  int leng;

  fd1 = find_fd(&thread_current()->fd_list,fd);
  if(fd1==NULL)
    return -1;
  else{
    lock_acquire(&filesys_lock);
    leng = file_length(fd1->f);
    lock_release(&filesys_lock);
    return leng;
  }
}

int read(int fd, const void *buffer, unsigned size){
  struct fd_elem * fd1 = NULL;
  int i;
  int readbyte;

  if(fd == 0){
    for(i=0; i<size; i++){
      *((char *)buffer+i)= input_getc();
    }
    return size;
  }
  
  fd1 = find_fd(&thread_current()->fd_list,fd);
  if(fd1 == NULL)
  {
    return -1;
  }
  
  lock_acquire(&filesys_lock);
  readbyte = file_read(fd1->f, buffer, size);
  lock_release(&filesys_lock);
  return readbyte;
}

int write(int fd, const void *buffer, unsigned size)
{
  struct fd_elem * fd1;
  int writebyte;
  
  if(fd == 1)
  {
    putbuf(buffer, size);
    return size;
  }
  
  fd1 = find_fd(&thread_current()->fd_list,fd);
  if(fd1 == NULL)
    return -1;

  lock_acquire(&filesys_lock);
  writebyte = file_write(fd1->f,buffer,size);
  lock_release(&filesys_lock);
  return writebyte;
}

void seek(int fd, unsigned pos){
  struct fd_elem * fd1;

  fd1 = find_fd(&thread_current()->fd_list,fd);
  lock_acquire(&filesys_lock);
  file_seek(fd1->f,pos);
  lock_release(&filesys_lock);
}

unsigned tell(int fd){
  struct fd_elem * fd1;
  unsigned pos; 
  fd1 = find_fd(&thread_current()->fd_list,fd);
  lock_acquire(&filesys_lock);
  pos = file_tell(fd1->f);
  lock_release(&filesys_lock);
  return pos;
}

void close(int fd){
  struct fd_elem * fd1;
  
  fd1 = find_fd(&thread_current()->fd_list,fd);
  if(fd1 != NULL){ 
    file_close(fd1->f);
    list_remove(&fd1->e);
    free(fd1);
  }
  return;
}

void close_all()
{
  struct list *f_list = &thread_current()->fd_list;
  struct fd_elem *f1;
  struct list_elem *elem;

  elem = list_begin(f_list);
  while(elem != list_end(f_list)){
    f1 = list_entry(elem, struct fd_elem, e);
    elem = list_next(elem);
    close(f1->fd);
  }
  file_close(thread_current()->executable);
}

void free_all_child(){
  struct list * c_list = &thread_current()->child_list;
  struct child_elem *ce1;
  struct list_elem *elem1;
  struct list_elem *elem2;

  elem1 = list_begin(c_list);
  while(elem1 != list_end(c_list)){
    ce1 = list_entry(elem1, struct child_elem, e);
    elem2 = list_next(elem1);
    list_remove(elem1);
    elem1 = elem2;
  }
}

mapid_t mmap(int fd, void * addr){
  if(addr == 0 || addr < (void *)0x08048000)
    return -1;
  if(pg_ofs (addr) != 0)
    return -1;

  struct thread * curr = thread_current();
  struct fd_elem * fd1 = find_fd(&curr->fd_list, fd);
  struct file * reopened_file;
  uint32_t read_bytes;
  off_t ofs = 0;
  int num_of_pages = 0;
  int start_addr = addr;
  int i;

  if(fd1==NULL)
    return -1;

  lock_acquire(&filesys_lock);
  reopened_file = file_reopen(fd1->f);
  read_bytes = file_length(reopened_file);
  if(read_bytes == 0){
    file_close(reopened_file);
    lock_release(&filesys_lock);
    return -1;
  }
  lock_release(&filesys_lock);

  while (read_bytes > 0){
    size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
    size_t page_zero_bytes = PGSIZE - page_read_bytes;
    
    /* fail due to out-of-user-vadddr or overlapping */
    if((!is_user_vaddr(addr)) || (find_spte(addr) != NULL)){
      for(i=0; i<num_of_pages; i++){
        struct spte * spte1 = find_spte(start_addr+(i*PGSIZE));
        hash_delete(&curr->spt, &spte1->hash_elem);
        free(spte1);
      }
      lock_acquire(&filesys_lock);
      file_close(reopened_file);
      lock_release(&filesys_lock);
      return -1;
    }

    /* add spte with writable = true, from_mmap = true */ 
    if(!add_spte(addr, reopened_file, ofs, page_read_bytes, page_zero_bytes, true, true))
      PANIC("mmap: add_spte unexpectedly failed");

    read_bytes -= page_read_bytes;
    ofs += page_read_bytes;
    addr += PGSIZE;
    num_of_pages ++;
  }

  struct md_elem *md1 = malloc(sizeof(*md1));
  md1->mapping = curr->md_count;
  md1->addr = start_addr;
  md1->num_of_pages = num_of_pages;
  list_push_back(&curr->md_list, &md1->e);
  curr->md_count ++;
  return md1->mapping;
}

void munmap(mapid_t mapping){
  struct list_elem * elem;
  struct md_elem * md1;
  struct md_elem * md2 = NULL;
  struct thread * curr = thread_current();
  struct list * l = &curr->md_list;
  int i;
  struct file * file;

  for(elem = list_begin(l); elem != list_end(l); elem = list_next(elem)){
    md1 = list_entry(elem, struct md_elem, e);
    if(md1->mapping == mapping){
      md2 = md1;
      break;
    }
  }
  if(md2 == NULL)
    PANIC("unvalid munmap call: the mapid doesn't exist");

  for(i=0; i<(md2->num_of_pages); i++){
    struct spte * spte1 = find_spte((md2->addr)+(i*PGSIZE));
    if(pagedir_is_dirty(curr->pagedir, spte1->page)){
      lock_acquire(&filesys_lock);
      file_write_at(spte1->file, spte1->frame, spte1->read_bytes, spte1->ofs);
      lock_release(&filesys_lock);
    }
    /* clear page, free page, delete and free fte, delete and free spte, remove from md_list */
    free_frame_table(spte1->frame);
    pagedir_clear_page(curr->pagedir, spte1->page);
    hash_delete(&thread_current()->spt, &spte1->hash_elem);
    file = spte1->file;
    free(spte1);
  }

  list_remove(&md2->e);
  free(md2);
  
  lock_acquire(&filesys_lock);
  file_close(file);
  lock_release(&filesys_lock);
}

void munmap_all(){
  struct list *l = &thread_current()->md_list;
  struct md_elem *md1;
  struct list_elem *elem;
  
  elem = list_begin(l);
  while(elem != list_end(l)){
    md1 = list_entry(elem, struct md_elem, e);
    elem = list_next(elem);
    munmap(md1->mapping);
  }
}

void
syscall_init (void) 
{
  lock_init(&filesys_lock);
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

static void
syscall_handler (struct intr_frame *f UNUSED) 
{
  int sys_type;
  int status;
  int fd;
  void *buffer;
  char *str;
  char *file;
  unsigned pos;
  unsigned size;
  unsigned initial_size;
  pid_t pid;
  mapid_t mapping;
  
  if(check_valid_pointer((const void*) f->esp, 4) == 0){
    exit(-1);
    return;
  }
  
  sys_type = *(int*)f->esp;
  //printf("syscall_handler : sys_type = %d\n",sys_type);
  switch(sys_type)
  {
    case SYS_HALT:
      halt();
      break;

    case SYS_EXIT:
      if(check_valid_pointer((const void*) (f->esp) + 4, 4)){
        status = *(((int *)f->esp)+1);
        exit(status);
      }
      else
        exit(-1);
      break;
    
    case SYS_EXEC:
      if(check_valid_pointer((const void*) (f->esp) + 4, 4) && check_valid_pointer((const void*) *(char **)(f->esp + 4), 1)){
        str = *(char **)(f->esp + 4);
        f->eax = exec(str);
      }
      else
        exit(-1);
      break;
    
    case SYS_WAIT:
      if(check_valid_pointer((const void*) (f->esp) + 4, 4)){
        pid = *((int *)((f->esp) + 4));
        f->eax = wait(pid);
      }
      else
        exit(-1);
      break;
    
    case SYS_CREATE:
      if(check_valid_pointer((const void*) (f->esp) + 4, 4) && check_valid_pointer((const void*) *(char **)(f->esp + 4), 1) && check_valid_pointer((const void*) (f->esp) + 8, 4)){
        file = *(char **)(f->esp + 4);
        initial_size = *(int *)(f->esp + 8);
        f->eax = create(file, initial_size);
      }
      else
        exit(-1);
      break;
    
    case SYS_REMOVE:
      if(check_valid_pointer((const void*) (f->esp) + 4, 4) && check_valid_pointer((const void*) *(char **)(f->esp + 4), 1)){
        file = *(char **)(f->esp + 4);
        f->eax = remove(file);
      }
      else
        exit(-1);
      break;
    
    case SYS_OPEN:
      if(check_valid_pointer((const void*) (f->esp) + 4, 4) && check_valid_pointer((const void*) *(char **)(f->esp + 4), 1)){
        file = *(char **)(f->esp + 4);
        f->eax = open(file);
      }
      else
        exit(-1);
      break;
    
    case SYS_FILESIZE:
      if(check_valid_pointer((const void*) (f->esp) + 4, 4)){
        fd = *(int *)(f->esp + 4);
        f->eax = filesize(fd);
      }
      else
        exit(-1);
      break;
    
    case SYS_READ:
      if(check_valid_pointer((const void*) (f->esp) + 4, 12)){
        check_valid_buffer((void *) *(char**) (f->esp + 8), (unsigned *) (f->esp + 12), f->esp, true);
        fd = *((int *)((f->esp) + 4));
        buffer = *(char**)(f->esp + 8);
        size = *((unsigned *)((f->esp) + 12));
        f->eax = read(fd, buffer, size);
      }
      else{
        exit(-1);
      }
      break;

    case SYS_WRITE:
      if(check_valid_pointer((const void*) (f->esp) + 4, 12)){  
        check_valid_buffer((void *) *(char**) (f->esp + 8), (unsigned *) (f->esp + 12), f->esp, false);
        fd = *((int *)((f->esp) + 4));
        buffer = *(char**)(f->esp + 8);
        size = *((unsigned *)((f->esp) + 12));
        f->eax = write(fd, buffer, size);
      }
      else
        exit(-1);
      break;

    case SYS_SEEK:
      if(check_valid_pointer((const void*) (f->esp) + 4, 8)){
        fd = *(int *)(f->esp + 4);
        pos = *(unsigned *)(f->esp + 8);
        seek(fd,pos);
      }
      else
        exit(-1);
      break;
    
    case SYS_TELL:
      if(check_valid_pointer((const void*) (f->esp) + 4, 4)){
        fd = *(int *)(f->esp + 4);
        f->eax = tell(fd);
      }
      else
        exit(-1);
      break;

    case SYS_CLOSE:
      if(check_valid_pointer((const void*) (f->esp) + 4, 4)){
        fd = *(int *)(f->esp + 4);
        lock_acquire(&filesys_lock);
        close(fd);
        lock_release(&filesys_lock);
      }
      else
        exit(-1);
      break;
    
    case SYS_MMAP:
      if(check_valid_pointer((const void*)(f->esp) + 4, 8)){
        fd = *(int *)(f->esp + 4);
        buffer = *(char**)(f->esp + 8);
        f->eax = mmap(fd, buffer);
      }
      else
        exit(-1);
      break;

    case SYS_MUNMAP:
      if(check_valid_pointer((const void*)(f->esp) + 4, 4)){
        mapping = *(mapid_t *)(f->esp + 4);
        munmap(mapping);
      }
      else
        exit(-1);
      break;

    default:
      exit(-1);
  }
}
