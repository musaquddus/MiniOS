#ifndef FILESYS_FILESYS_H
#define FILESYS_FILESYS_H

#include <stdbool.h>
#include "filesys/off_t.h"
#include "devices/block.h"
#include "threads/synch.h"

/* Sectors of system file inodes. */
#define FREE_MAP_SECTOR 0 /* Free map file inode sector. */
#define ROOT_DIR_SECTOR 1 /* Root directory file inode sector. */

typedef struct {
  block_sector_t sector;       // id
  char buf[BLOCK_SECTOR_SIZE]; // buffer from disk
} sector_node;

struct sector_cache {
  int hits;
  int misses;
  struct lock global_lock;    // global lock for evictions and lookups
  sector_node** sector_list;  // list of sector nodes that hold inode buffers
  struct lock** sector_locks; // list of sector locks for atomic read/write (race free lookup)
  uint64_t dirty_bitmap;
  uint64_t valid_bitmap;
  uint64_t clock_bitmap;
  int last_evict;
};

/* Block device that contains the file system. */
extern struct block* fs_device;

void filesys_init(bool format);
void filesys_done(void);
bool filesys_create(const char* name, off_t initial_size);
struct file* filesys_open(const char* name);
// bool filesys_remove(const char* name, struct dir*);

// sector cache functions
void cache_flush(void);
int cache_lookup(block_sector_t sector);
void cache_read(block_sector_t sector, void* buf);
void cache_write(block_sector_t sector, void* buf);
void write_entry_to_disk(int position);

bool is_valid(int i);
bool is_dirty(int i);
bool is_evictable(int i);

void toggle_valid(int i);
void toggle_dirty(int i);
void toggle_evictable(int i);

int get_hitrate(void);
void reset_cache(void);

int get_fs_reads(void);
int get_fs_writes(void);

#endif /* filesys/filesys.h */
