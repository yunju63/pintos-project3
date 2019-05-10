#ifndef VM_SWAP_H
#define VM_SWAP_H

#define SECTORS_PER_PAGE 8

struct lock disk_lock;
struct disk *swap_disk;
struct bitmap *swap_bitmap;

void swap_init(void);
void swap_in(void *frame, size_t used_i);
size_t swap_out(void *frame);

#endif
