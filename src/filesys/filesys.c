#include "filesys/filesys.h"
#include <debug.h>
#include <stdio.h>
#include <string.h>
#include "filesys/file.h"
#include "filesys/free-map.h"
#include "filesys/inode.h"
#include "filesys/directory.h"

/* Partition that contains the file system. */
struct block* fs_device;

/* Sector cache. */
struct sector_cache* s_cache;

static void do_format(void);

/* Initializes the file system module.
   If FORMAT is true, reformats the file system. */
void filesys_init(bool format) {
  /* Sector cache setup */
  s_cache = (struct sector_cache*)malloc(sizeof(struct sector_cache));
  s_cache->dirty_bitmap = s_cache->clock_bitmap = s_cache->valid_bitmap = 0;
  s_cache->hits = s_cache->misses = 0;
  s_cache->last_evict = -1;
  lock_init(&(s_cache->global_lock));
  s_cache->sector_list = (sector_node**)malloc(sizeof(sector_node*) * 64);
  s_cache->sector_locks = (struct lock**)malloc(sizeof(struct lock*) * 64);
  for (int i = 0; i < 64; i++) {
    s_cache->sector_list[i] = (sector_node*)malloc(sizeof(sector_node));
    s_cache->sector_locks[i] = (struct lock*)malloc(sizeof(struct lock));
    lock_init(s_cache->sector_locks[i]);
  }
  /* End of sector cache setup */
  fs_device = block_get_role(BLOCK_FILESYS);
  if (fs_device == NULL)
    PANIC("No file system device found, can't initialize file system.");

  inode_init();
  free_map_init();

  if (format)
    do_format();

  free_map_open();
}

/* Shuts down the file system module, writing any unwritten data
   to disk. */
void filesys_done(void) {
  cache_flush();
  free_map_close();
}

/* Creates a file named NAME with the given INITIAL_SIZE.
   Returns true if successful, false otherwise.
   Fails if a file named NAME already exists,
   or if internal memory allocation fails. */
bool filesys_create(const char* name, off_t initial_size) {
  block_sector_t inode_sector = 0;
  struct dir* dir = dir_open_root();
  bool success =
      (dir != NULL && free_map_allocate(1, &inode_sector) &&
       inode_create(inode_sector, initial_size, false) && dir_add(dir, name, inode_sector));
  if (!success && inode_sector != 0)
    free_map_release(inode_sector, 1);
  dir_close(dir);

  return success;
}

/* Performs the same actions as filesys_create, just in the dir provided. */
bool filesys_create_dir(const char* name, off_t initial_size, struct dir* dir) {
  block_sector_t inode_sector = 0;
  bool success =
      (dir != NULL && free_map_allocate(1, &inode_sector) &&
       inode_create(inode_sector, initial_size, false) && dir_add(dir, name, inode_sector));
  if (!success && inode_sector != 0)
    free_map_release(inode_sector, 1);
  dir_close(dir);

  return success;
}

/* Opens the file with the given NAME.
   Returns the new file if successful or a null pointer
   otherwise.
   Fails if no file named NAME exists,
   or if an internal memory allocation fails. */
struct file* filesys_open(const char* name) {
  struct dir* dir = dir_open_root();
  struct inode* inode = NULL;

  if (dir != NULL)
    dir_lookup(dir, name, &inode);
  dir_close(dir);

  return file_open(inode);
}

/* Performs the same functionality as filesys_open, just in the dir provided. */
struct file* filesys_open_dir(const char* name, struct dir* dir) {
  struct inode* inode = NULL;
  dir = dir == NULL ? dir_open_root() : dir;

  if (dir != NULL)
    dir_lookup(dir, name, &inode);
  dir_close(dir);

  return file_open(inode);
}

/* Writes the data from entry to the correct sector on disk. */
void write_entry_to_disk(int position) {
  block_write(fs_device, s_cache->sector_list[position]->sector,
              s_cache->sector_list[position]->buf);
}

/* Iterates through the sector cache and flushes 
   dirty sectors to the disk. This implementation 
   does not evict the flushed sectors from the cache. */
void cache_flush(void) {
  lock_acquire(&(s_cache->global_lock));
  for (int i = 0; i < 64; i++) {
    lock_acquire(s_cache->sector_locks[i]);
    if (is_valid(i) && is_dirty(i)) {
      write_entry_to_disk(i);
      toggle_dirty(i);
    }
    lock_release(s_cache->sector_locks[i]);
  }
  lock_release(&(s_cache->global_lock));
}

/* Performs a cache lookup for the provided sector.
   returns the */
int cache_lookup(block_sector_t sector) {
  for (int i = 0; i < 64; i++) {
    sector_node* entry = s_cache->sector_list[i];
    if (is_valid(i) && entry->sector == sector) {
      return i;
    }
  }
  return -1;
}

/* Reads data at sector into buf. Stores the result 
   in the cache. If the cache is full, evicts an entry by LRU,
   flushing the entry if it is dirty. */
void cache_read(block_sector_t sector, void* buf) {
  /* Perform cache lookup and access entry if one exists */
  int position = cache_lookup(sector);
  while (position != -1) {
    lock_acquire(s_cache->sector_locks[position]);
    sector_node* entry = s_cache->sector_list[position];
    if (is_valid(position) && entry->sector == sector) {
      memcpy(buf, entry->buf, BLOCK_SECTOR_SIZE);
      s_cache->hits++;
      lock_release(s_cache->sector_locks[position]);
      return;
    } else
      lock_release(s_cache->sector_locks[position]);
    position = cache_lookup(sector);
  }
  lock_acquire(&s_cache->global_lock);
  /* Look for empty space for cache entry */
  for (int i = 0; i < 64; i++) {
    if (!is_valid(i)) {
      lock_acquire(s_cache->sector_locks[i]);
      if (!is_valid(i)) {
        sector_node* entry = s_cache->sector_list[i];
        entry->sector = sector;
        block_read(fs_device, sector, entry->buf);
        memcpy(buf, entry->buf, BLOCK_SECTOR_SIZE);
        s_cache->misses++;
        toggle_valid(i);
        if (is_dirty(i))
          toggle_dirty(i);
        if (is_evictable(i))
          toggle_evictable(i);
        lock_release(s_cache->sector_locks[i]);
        lock_release(&s_cache->global_lock);
        return;
      } else
        lock_release(s_cache->sector_locks[i]);
    }
  }
  /* Evict something if no empty space*/
  int last_evict = s_cache->last_evict;
  int i = (last_evict + 1) % 64;
  for (;;) {
    lock_acquire(s_cache->sector_locks[i]);
    if (is_evictable(i)) {
      if (is_dirty(i)) {
        write_entry_to_disk(i);
        toggle_dirty(i);
      }
      toggle_evictable(i);
      s_cache->last_evict = i;
      sector_node* entry = s_cache->sector_list[i];
      entry->sector = sector;
      block_read(fs_device, sector, entry->buf);
      memcpy(buf, entry->buf, BLOCK_SECTOR_SIZE);
      s_cache->misses++;
      lock_release(s_cache->sector_locks[i]);
      lock_release(&s_cache->global_lock);
      return;
    } else
      toggle_evictable(i);
    lock_release(s_cache->sector_locks[i]);
    i = (i + 1) % 64;
  }
}

/* Writes data from buf into a new/existing cache entry for sector. 
   If the cache is full, evicts an entry by LRU, flushing 
   the entry if it is dirty. */
void cache_write(block_sector_t sector, void* buf) {
  /* Perform cache lookup and access entry if one exists */
  int position = cache_lookup(sector);
  while (position != -1) {
    lock_acquire(s_cache->sector_locks[position]);
    sector_node* entry = s_cache->sector_list[position];
    if (is_valid(position) && entry->sector == sector) {
      memcpy(entry->buf, buf, BLOCK_SECTOR_SIZE);
      if (!is_dirty(position))
        toggle_dirty(position);
      if (is_evictable(position))
        toggle_evictable(position);
      s_cache->hits++;
      lock_release(s_cache->sector_locks[position]);
      return;
    } else {
      lock_release(s_cache->sector_locks[position]);
    }
    position = cache_lookup(sector);
  }
  lock_acquire(&s_cache->global_lock);
  /* Look for empty space for cache entry */
  for (int i = 0; i < 64; i++) {
    if (!is_valid(i)) {
      lock_acquire(s_cache->sector_locks[i]);
      if (!is_valid(i)) {
        sector_node* entry = s_cache->sector_list[i];
        entry->sector = sector;
        memcpy(entry->buf, buf, BLOCK_SECTOR_SIZE);
        s_cache->misses++;
        toggle_valid(i);
        if (!is_dirty(i))
          toggle_dirty(i);
        if (is_evictable(i))
          toggle_evictable(i);
        lock_release(s_cache->sector_locks[i]);
        lock_release(&s_cache->global_lock);
        return;
      } else
        lock_release(s_cache->sector_locks[i]);
    }
  }
  /* Evict something if no empty space*/
  int last_evict = s_cache->last_evict;
  int i = (last_evict + 1) % 64;
  for (;;) { // not in cache, no open space
    lock_acquire(s_cache->sector_locks[i]);
    if (is_evictable(i)) {
      if (is_dirty(i))
        write_entry_to_disk(i);
      else
        toggle_dirty(i);
      toggle_evictable(i);
      s_cache->last_evict = i;
      sector_node* entry = s_cache->sector_list[i];
      entry->sector = sector;
      memcpy(entry->buf, buf, BLOCK_SECTOR_SIZE);
      s_cache->misses++;
      lock_release(s_cache->sector_locks[i]);
      lock_release(&s_cache->global_lock);
      return;
    } else
      toggle_evictable(i);
    lock_release(s_cache->sector_locks[i]);
    i = (i + 1) % 64;
  }
}

/* Function for bitmap checks. */
bool is_true(uint64_t bitmap, int i);
bool is_true(uint64_t bitmap, int i) { return bitmap & (((int64_t)1) << i); }

/* Checks the s_cache valid bitmap and returns whether the bit is true or false. */
bool is_valid(int i) { return is_true(s_cache->valid_bitmap, i); }

/* Checks the s_cache dirty bitmap and returns whether the bit is true or false. */
bool is_dirty(int i) { return is_true(s_cache->dirty_bitmap, i); }

/* Checks the s_cache clock bitmap and returns whether the bit is true or false. */
bool is_evictable(int i) { return is_true(s_cache->clock_bitmap, i); }

/* Function for bitmap sets. */
void toggle_bit(uint64_t* bitmap, int i);
void toggle_bit(uint64_t* bitmap, int i) { *bitmap = *bitmap ^ (((int64_t)1) << i); }

/* Toggles a sector's valid bit. */
void toggle_valid(int i) { toggle_bit(&(s_cache->valid_bitmap), i); }

/* Toggles a sector's dirty bit. */
void toggle_dirty(int i) { toggle_bit(&(s_cache->dirty_bitmap), i); }

/* Toggles a sector's clock bit. */
void toggle_evictable(int i) { toggle_bit(&(s_cache->clock_bitmap), i); }

/* Gets the current hitrate of the cache. */
int get_hitrate() {
  if (s_cache->hits + s_cache->misses == 0)
    return 0;
  return (int)(((double)s_cache->hits / (s_cache->hits + s_cache->misses)) * 100);
}

/* Flushes the cache, then marks everything as invalid. */
void reset_cache() {
  lock_acquire(&s_cache->global_lock);
  for (int i = 0; i < 64; i++)
    lock_acquire(s_cache->sector_locks[i]);
  for (int i = 0; i < 64; i++)
    if (is_valid(i) && is_dirty(i))
      write_entry_to_disk(i);
  s_cache->valid_bitmap = 0;
  for (int i = 0; i < 64; i++)
    lock_release(s_cache->sector_locks[i]);
  s_cache->hits = s_cache->misses = 0;
  lock_release(&s_cache->global_lock);
}

/* Gets the reads of the fs_device. */
int get_fs_reads() { return get_reads(fs_device); }

/* Gets the writes of the fs_device. */
int get_fs_writes() { return get_writes(fs_device); }

/* Formats the file system. */
static void do_format(void) {
  printf("Formatting file system...");
  free_map_create();
  if (!dir_create(ROOT_DIR_SECTOR, 16))
    PANIC("root directory creation failed");
  free_map_close();
  struct dir* root = dir_open_root();
  if (!dir_add(root, ".", ROOT_DIR_SECTOR) || !dir_add(root, "..", ROOT_DIR_SECTOR))
    PANIC("root directory . & .. failed");
  dir_close(root);
  printf("done.\n");
}