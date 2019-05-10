#include <stdio.h>
#include <debug.h>
#include <bitmap.h>
#include "vm/frame.h"
#include "vm/page.h"
#include "vm/swap.h"
#include "threads/synch.h"
#include "threads/vaddr.h"
#include "devices/disk.h"

void swap_init()
{
  swap_disk = disk_get(1, 1);
  if(swap_disk == NULL)
  {
    printf("disk_get error\n");
    return;
  }
  swap_bitmap = bitmap_create(disk_size(swap_disk) / SECTORS_PER_PAGE);
  if(swap_bitmap == NULL)
  {
    printf("bitmap_create error\n");
    return;
  }
  bitmap_set_all(swap_bitmap, 0);
  lock_init(&disk_lock);
}

void swap_in(void* frame, size_t used_i)
{
  if(swap_disk == NULL || swap_bitmap == NULL)
  {
    printf("swap_in error\n");
    return;
  }
  lock_acquire(&disk_lock);
  if(bitmap_test(swap_bitmap, used_i) == 0)
    PANIC("Swap with free index");
  bitmap_flip(swap_bitmap, used_i);
  size_t i;
  for(i = 0; i < SECTORS_PER_PAGE; i++)
  {
    disk_read(swap_disk, used_i * SECTORS_PER_PAGE + i, (uint8_t *) frame + i * DISK_SECTOR_SIZE);
  }
  lock_release(&disk_lock);
}

size_t swap_out(void* frame)
{
  if(swap_disk == NULL || swap_bitmap == NULL)
  {
    printf("swapout error\n");
    return;
  }
  lock_acquire(&disk_lock);
  size_t free_i = bitmap_scan_and_flip(swap_bitmap, 0, 1, 0);

  if(free_i == BITMAP_ERROR)
    PANIC("Swap_disk is full");
  
  size_t i;
  for(i = 0; i < SECTORS_PER_PAGE; i++)
  {
    disk_write(swap_disk, free_i * SECTORS_PER_PAGE + i, (uint8_t *) frame + i * DISK_SECTOR_SIZE);
  }
  lock_release(&disk_lock);
  return free_i;
}
