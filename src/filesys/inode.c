#include "filesys/inode.h"
#include <list.h>
#include <debug.h>
#include <round.h>
#include <string.h>
#include "filesys/filesys.h"
#include "filesys/free-map.h"
#include "threads/malloc.h"

/* Identifies an inode. */
#define INODE_MAGIC 0x494e4f44
#define MIN(X, Y) (((X) < (Y)) ? (X) : (Y))
#define MAX(X, Y) (((X) > (Y)) ? (X) : (Y))
#define NAME_MAX 14

struct lock* gil;

/* On-disk inode.
   Must be exactly BLOCK_SECTOR_SIZE bytes long. */
struct inode_disk {
  block_sector_t direct[12];      /* Direct pointers */
  block_sector_t indirect;        /* Indirect pointer */
  block_sector_t double_indirect; /* Double indirect pointer */
  bool is_dir;                    /*Is directory?*/
  block_sector_t parent;          /*Start of parent directory address*/
  off_t offset;                   /*Offset from parent directory*/
  off_t length;                   /* File size in bytes. */
  uint32_t unused[109];           /* Not used. */
  unsigned magic;
};

/* Returns the number of sectors to allocate for an inode SIZE
   bytes long. */
static inline size_t bytes_to_sectors(off_t size) { return DIV_ROUND_UP(size, BLOCK_SECTOR_SIZE); }

/* In-memory inode. */
struct inode {
  struct list_elem elem; /* Element in inode list. */
  block_sector_t sector; /* Sector number of disk location. */
  int open_cnt;          /* Number of openers. */
  bool removed;          /* True if deleted, false otherwise. */
  int deny_write_cnt;    /* 0: writes ok, >0: deny writes. */
};

/* Returns the block device sector that contains byte offset POS
   within INODE.
   Returns -1 if INODE does not contain data for a byte at offset
   POS. */
static block_sector_t byte_to_sector(const struct inode* inode, off_t pos) {
  struct inode_disk* disk = calloc(1, sizeof *disk);
  if (!disk) {
    return -1;
  }
  cache_read(inode->sector, disk);
  int i;
  if (disk->magic != INODE_MAGIC)
    i = -1;
  ASSERT(inode != NULL);
  if (pos > disk->length) {
    free(disk);
    return -1;
  }
  off_t sector_num = pos / BLOCK_SECTOR_SIZE;
  if (sector_num < 12) {
    block_sector_t result = disk->direct[sector_num];
    free(disk);
    return result;
  } else if (sector_num >= 12 && sector_num < 140) {
    block_sector_t buffer[128];
    cache_read(disk->indirect, buffer);
    free(disk);
    return buffer[sector_num - 12];
  } else {
    block_sector_t buffer[128];
    cache_read(disk->double_indirect, buffer);
    cache_read(buffer[(sector_num - 140) / 128], buffer);
    free(disk);
    return buffer[(sector_num - 140) % 128];
  }
  free(disk);
  return -1;
}

/* Resize the inode to the provided size if it can, returns 
  if the operation succeeds or not. */
bool inode_resize(struct inode inode, size_t size) {
  struct inode_disk* disk = calloc(1, sizeof *disk);
  static char zeros[BLOCK_SECTOR_SIZE];
  cache_read(inode.sector, disk);
  for (int i = 0; i < 12; i++) {
    if (size < BLOCK_SECTOR_SIZE * i && disk->direct[i] != 0) {
      free_map_release(disk->direct[i], 1);
      disk->direct[i] = 0;
    } else if (size >= BLOCK_SECTOR_SIZE * i && disk->direct[i] == 0) {
      free_map_allocate(1, &disk->direct[i]);
      if (disk->direct[i] == 0) {
        off_t len = disk->length;
        free(disk);
        inode_resize(inode, len);
        return false;
      }
      cache_write(disk->direct[i], zeros);
    }
  }
  if (disk->indirect == 0 && size < 12 * 512) {
    disk->length = size;
    cache_write(inode.sector, disk);
    free(disk);
    return true;
  }
  block_sector_t buffer[128];
  memset(buffer, 0, 512);
  if (disk->indirect == 0) {
    /* Allocate indirect block. */
    free_map_allocate(1, &disk->indirect);
    if (disk->indirect == 0) {
      off_t len = disk->length;
      free(disk);
      inode_resize(inode, len);
      return false;
    }
  } else 
    cache_read(disk->indirect, buffer);
  for (int i = 0; i < 128; i++) {
    if (size < (12 + i) * BLOCK_SECTOR_SIZE && buffer[i] != 0) {
      free_map_release(buffer[i], 1);
      buffer[i] = 0;
    } else if (size >= (12 + i) * BLOCK_SECTOR_SIZE && buffer[i] == 0) {
      /* Grow. */
      free_map_allocate(1, &buffer[i]);
      if (buffer[i] == 0) {
        off_t len = disk->length;
        free(disk);
        inode_resize(inode, len);
        return false;
      }
    }
  }
  if (size < 12 * BLOCK_SECTOR_SIZE) {
    free_map_release(disk->indirect, 1);
    disk->indirect = 0;
  } else
    cache_write(disk->indirect, buffer);
  if (disk->double_indirect == 0 && size < 140 * 512) { //140 is 128 from indirect +12 dir
    disk->length = size;
    cache_write(inode.sector, disk);
    free(disk);
    return true;
  }
  memset(buffer, 0, 512);
  block_sector_t second_buffer[128];
  memset(second_buffer, 0, 512);
  if (disk->double_indirect == 0) {
    /* Allocate double indirect block. */
    free_map_allocate(1, &disk->double_indirect);
    if (disk->double_indirect == 0) {
      off_t len = disk->length;
      free(disk);
      inode_resize(inode, len);
      return false;
    }
  } else
    cache_read(disk->double_indirect, buffer);
  /* Handle double indirect pointers. */
  for (int i = 0; i < 128; i++) {
    if (size >= (140 + (i * 128)) * BLOCK_SECTOR_SIZE && buffer[i] == 0) {
      /* Grow. */
      free_map_allocate(1, &buffer[i]);
      if (buffer[i] == 0) {
        off_t len = disk->length;
        free(disk);
        inode_resize(inode, len);
        return false;
      }
    }
    if (buffer[i] != 0) {
      cache_read(buffer[i], second_buffer);
      for (int j = 0; j < 128; j++) {
        if (size < (140 + j + (i * 128)) * BLOCK_SECTOR_SIZE && second_buffer[j] != 0) {
          /* Shrink inner page. */
          free_map_release(second_buffer[j], 1);
          second_buffer[j] = 0;
        } else if (size >= (140 + j + (i * 128)) * BLOCK_SECTOR_SIZE && second_buffer[j] == 0) {
          /* Grow inner page. */
          free_map_allocate(1, &second_buffer[j]);
          if (second_buffer[j] == 0) {
            off_t len = disk->length;
            free(disk);
            inode_resize(inode, len);
            return false;
          }
        }
      }
      cache_write(buffer[i], second_buffer);
      if (size < (140 + (i * 128)) * BLOCK_SECTOR_SIZE && buffer[i] != 0) {
        /* Shrink. */
        free_map_release(buffer[i], 1);
        buffer[i] = 0;
      }
    }
  }
  if (size < 140 * BLOCK_SECTOR_SIZE) {
    /* We shrank the inode such that indirect pointers are no longer required. */
    free_map_release(disk->double_indirect, 1);
    disk->double_indirect = 0;
  } else
    cache_write(disk->double_indirect, buffer);
  disk->length = size;
  cache_write(inode.sector, disk);
  free(disk);
  return true;
}

/* List of open inodes, so that opening a single inode twice
   returns the same `struct inode'. */
static struct list open_inodes;

/* Initializes the inode module. */
void inode_init(void) { list_init(&open_inodes); }

/* Initializes an inode with LENGTH bytes of data and
   writes the new inode to sector SECTOR on the file system
   device.
   Returns true if successful.
   Returns false if memory or disk allocation fails. */

bool inode_create(block_sector_t sector, off_t length, bool is_dir) {
  struct inode_disk* disk_inode = NULL;
  bool success = false;

  ASSERT(length >= 0);

  /* If this assertion fails, the inode structure is not exactly
     one sector in size, and you should fix that. */
  ASSERT(sizeof *disk_inode == BLOCK_SECTOR_SIZE);

  disk_inode = calloc(1, sizeof *disk_inode);
  disk_inode->is_dir = is_dir;
  if (disk_inode != NULL) {
    size_t sectors = bytes_to_sectors(length);
    if (sector == 0) {
      disk_inode->length = 512;
    } else {
      disk_inode->length = 0; //was using 512 temp fix
    }
    disk_inode->magic = INODE_MAGIC;
    if (free_map_allocate(1, &disk_inode->direct[0])) {
      //block_write(fs_device, sector, disk_inode);
      cache_write(sector, disk_inode);
      static char zeros[BLOCK_SECTOR_SIZE];
      //block_write(fs_device, disk_inode->direct[0], zeros);
      cache_write(disk_inode->direct[0], zeros);
      success = true;
    }
    free(disk_inode);
    if (sector != 0) {
      struct inode temp;
      temp.sector = sector;
      inode_resize(temp, length);
    }
  }
  return success;
}

/* Reads an inode from SECTOR
   and returns a `struct inode' that contains it.
   Returns a null pointer if memory allocation fails. */
struct inode* inode_open(block_sector_t sector) {
  struct list_elem* e;
  struct inode* inode;

  /* Check whether this inode is already open. */
  for (e = list_begin(&open_inodes); e != list_end(&open_inodes); e = list_next(e)) {
    inode = list_entry(e, struct inode, elem);
    if (inode->sector == sector) {
      inode_reopen(inode);
      return inode;
    }
  }

  /* Allocate memory. */
  inode = malloc(sizeof *inode);
  if (inode == NULL)
    return NULL;

  /* Initialize. */
  list_push_front(&open_inodes, &inode->elem);
  inode->sector = sector;
  inode->open_cnt = 1;
  inode->deny_write_cnt = 0;
  inode->removed = false;
  return inode;
}

/* Reopens and returns INODE. */
struct inode* inode_reopen(struct inode* inode) {
  if (inode != NULL)
    inode->open_cnt++;
  return inode;
}

/* Returns INODE's inode number. */
block_sector_t inode_get_inumber(const struct inode* inode) { return inode->sector; }

/* Closes INODE and writes it to disk.
   If this was the last reference to INODE, frees its memory.
   If INODE was also a removed inode, frees its blocks. */
void inode_close(struct inode* inode) {
  /* Ignore null pointer. */
  if (inode == NULL)
    return;

  struct inode_disk* disk_inode = calloc(1, sizeof *disk_inode);
  if (disk_inode == NULL)
    return;
  cache_read(inode->sector, disk_inode);

  /* Release resources if this was the last opener. */
  if (--inode->open_cnt == 0) {
    /* Remove from inode list and release lock. */
    list_remove(&inode->elem);

    /* Deallocate blocks if removed. */
    if (inode->removed) {
      free_map_release(inode->sector, 1);
      free_map_release(disk_inode->direct[0], bytes_to_sectors(disk_inode->length));
    }
    free(inode);
  }
  free(disk_inode);
}

/* Marks INODE to be deleted when it is closed by the last caller who
   has it open. */
void inode_remove(struct inode* inode) {
  ASSERT(inode != NULL);
  inode->removed = true;
}

/* Reads SIZE bytes from INODE into BUFFER, starting at position OFFSET.
   Returns the number of bytes actually read, which may be less
   than SIZE if an error occurs or end of file is reached. */
off_t inode_read_at(struct inode* inode, void* buffer_, off_t size, off_t offset) {
  uint8_t* buffer = buffer_;
  off_t bytes_read = 0;
  uint8_t* bounce = NULL;

  while (size > 0) {
    /* Disk sector to read, starting byte offset within sector. */
    block_sector_t sector_idx = byte_to_sector(inode, offset);
    int sector_ofs = offset % BLOCK_SECTOR_SIZE;

    /* Bytes left in inode, bytes left in sector, lesser of the two. */
    off_t inode_left = inode_length(inode) - offset;
    int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
    int min_left = inode_left < sector_left ? inode_left : sector_left;

    /* Number of bytes to actually copy out of this sector. */
    int chunk_size = size < min_left ? size : min_left;
    if (chunk_size <= 0)
      break;

    if (sector_ofs == 0 && chunk_size == BLOCK_SECTOR_SIZE) {
      /* Read full sector directly into caller's buffer. */
      //block_read(fs_device, sector_idx, buffer + bytes_read);
      cache_read(sector_idx, buffer + bytes_read);
    } else {
      /* Read sector into bounce buffer, then partially copy
             into caller's buffer. */
      if (bounce == NULL) {
        bounce = malloc(BLOCK_SECTOR_SIZE);
        if (bounce == NULL)
          break;
      }
      cache_read(sector_idx, bounce);
      memcpy(buffer + bytes_read, bounce + sector_ofs, chunk_size);
    }

    /* Advance. */
    size -= chunk_size;
    offset += chunk_size;
    bytes_read += chunk_size;
  }
  free(bounce);

  return bytes_read;
}

/* Writes SIZE bytes from BUFFER into INODE, starting at OFFSET.
   Returns the number of bytes actually written, which may be
   less than SIZE if end of file is reached or an error occurs. */
off_t inode_write_at(struct inode* inode, const void* buffer_, off_t size, off_t offset) {
  struct inode_disk* disk_inode = calloc(1, sizeof *disk_inode);
  if (!disk_inode)
    return 0;
  cache_read(inode->sector, disk_inode);
  if (offset + size > disk_inode->length) {
    if (!inode_resize(*inode, offset + size)) {
      free(disk_inode);
      return 0;
    }
  }
  free(disk_inode);
  const uint8_t* buffer = buffer_;
  off_t bytes_written = 0;
  uint8_t* bounce = NULL;

  if (inode->deny_write_cnt)
    return 0;
  while (size > 0) {
    /* Sector to write, starting byte offset within sector. */
    block_sector_t sector_idx = byte_to_sector(inode, offset);
    if (sector_idx + 1 == 0)
      return 0;
    int sector_ofs = offset % BLOCK_SECTOR_SIZE;

    /* Bytes left in inode, bytes left in sector, lesser of the two. */
    off_t inode_left = inode_length(inode) - offset;
    int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
    int min_left = inode_left < sector_left ? inode_left : sector_left;

    /* Number of bytes to actually write into this sector. */
    int chunk_size = size < min_left ? size : min_left;
    if (chunk_size <= 0)
      break;

    if (sector_ofs == 0 && chunk_size == BLOCK_SECTOR_SIZE) {
      /* Write full sector directly to disk. */
      cache_write(sector_idx, buffer + bytes_written);
    } else {
      /* We need a bounce buffer. */
      if (bounce == NULL) {
        bounce = malloc(BLOCK_SECTOR_SIZE);
        if (bounce == NULL)
          break;
      }

      /* If the sector contains data before or after the chunk
             we're writing, then we need to read in the sector
             first.  Otherwise we start with a sector of all zeros. */
      if (sector_ofs > 0 || chunk_size < sector_left)
        cache_read(sector_idx, bounce);
      else
        memset(bounce, 0, BLOCK_SECTOR_SIZE);
      memcpy(bounce + sector_ofs, buffer + bytes_written, chunk_size);
      cache_write(sector_idx, bounce);
    }

    /* Advance. */
    size -= chunk_size;
    offset += chunk_size;
    bytes_written += chunk_size;
  }
  free(bounce);

  return bytes_written;
}

/* Disables writes to INODE.
   May be called at most once per inode opener. */
void inode_deny_write(struct inode* inode) {
  inode->deny_write_cnt++;
  ASSERT(inode->deny_write_cnt <= inode->open_cnt);
}

/* Re-enables writes to INODE.
   Must be called once by each inode opener who has called
   inode_deny_write() on the inode, before closing the inode. */
void inode_allow_write(struct inode* inode) {
  ASSERT(inode->deny_write_cnt > 0);
  ASSERT(inode->deny_write_cnt <= inode->open_cnt);
  inode->deny_write_cnt--;
}

/* Returns the length, in bytes, of INODE's data. */
off_t inode_length(const struct inode* inode) {
  struct inode_disk* disk_inode = calloc(1, sizeof *disk_inode);
  cache_read(inode->sector, disk_inode);
  off_t length = disk_inode->length;
  free(disk_inode);
  return length;
}

/* Checks if an inode belongs to a directory. */
bool inode_is_dir(struct inode* inode) {
  struct inode_disk disk;
  cache_read(inode->sector, &disk);
  return disk.is_dir;
}

bool inode_is_open(struct inode* inode) { return inode->open_cnt > 1; }
bool inode_is_root(struct inode* inode) { return inode->sector == 1; }
