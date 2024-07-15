#include "filesys/directory.h"
#include <stdio.h>
#include <string.h>
#include <list.h>
#include "filesys/filesys.h"
#include "filesys/inode.h"
#include "threads/malloc.h"
#include <stdlib.h>
#include "threads/thread.h"
#include "userprog/process.h"

/* A directory. */
struct dir {
  struct inode* inode; /* Backing store. */
  off_t pos;           /* Current position. */
};

/* A single directory entry. */
struct dir_entry {
  block_sector_t inode_sector; /* Sector number of header. */
  char name[NAME_MAX + 1];     /* Null terminated file name. */
  bool in_use;                 /* In use or free? */
};

/* Creates a directory with space for ENTRY_CNT entries in the
   given SECTOR.  Returns true if successful, false on failure. */
bool dir_create(block_sector_t sector, size_t entry_cnt) {
  return inode_create(sector, entry_cnt * sizeof(struct dir_entry), true);
}

/* Opens and returns the directory for the given INODE, of which
   it takes ownership.  Returns a null pointer on failure. */
struct dir* dir_open(struct inode* inode) {
  struct dir* dir = calloc(1, sizeof *dir);
  if (inode != NULL && dir != NULL) {
    dir->inode = inode;
    dir->pos = 0;
    return dir;
  } else {
    inode_close(inode);
    free(dir);
    return NULL;
  }
}

/* Opens the root directory and returns a directory for it.
   Return true if successful, false on failure. */
struct dir* dir_open_root(void) {
  return dir_open(inode_open(ROOT_DIR_SECTOR));
}

/* Opens and returns a new directory for the same inode as DIR.
   Returns a null pointer on failure. */
struct dir* dir_reopen(struct dir* dir) {
  return dir_open(inode_reopen(dir->inode));
}

/* Destroys DIR and frees associated resources. */
void dir_close(struct dir* dir) {
  if (dir != NULL) {
    inode_close(dir->inode);
    free(dir);
  }
}

/* Returns the inode encapsulated by DIR. */
struct inode* dir_get_inode(struct dir* dir) {
  return dir->inode;
}

/* Searches DIR for a file with the given NAME.
   If successful, returns true, sets *EP to the directory entry
   if EP is non-null, and sets *OFSP to the byte offset of the
   directory entry if OFSP is non-null.
   otherwise, returns false and ignores EP and OFSP. */
static bool lookup(const struct dir* dir, const char* name, struct dir_entry* ep, off_t* ofsp) {
  struct dir_entry e;
  size_t ofs;

  ASSERT(dir != NULL);
  ASSERT(name != NULL);
  if (dir->inode == NULL) {
    return false;
  }
  for (ofs = 0; inode_read_at(dir->inode, &e, sizeof e, ofs) == sizeof e; ofs += sizeof e)
    if (e.in_use && !strcmp(name, e.name)) {
      if (ep != NULL)
        *ep = e;
      if (ofsp != NULL)
        *ofsp = ofs;
      return true;
    }
  return false;
}

/* Searches DIR for a file with the given NAME
   and returns true if one exists, false otherwise.
   On success, sets *INODE to an inode for the file, otherwise to
   a null pointer.  The caller must close *INODE. */
bool dir_lookup(const struct dir* dir, const char* name, struct inode** inode) {
  struct dir_entry e;

  ASSERT(dir != NULL);
  ASSERT(name != NULL);

  if (lookup(dir, name, &e, NULL)) {
    *inode = inode_open(e.inode_sector);
  } else
    *inode = NULL;

  return *inode != NULL;
}

/* Adds a file named NAME to DIR, which must not already contain a
   file by that name.  The file's inode is in sector
   INODE_SECTOR.
   Returns true if successful, false on failure.
   Fails if NAME is invalid (i.e. too long) or a disk or memory
   error occurs. */
bool dir_add(struct dir* dir, const char* name, block_sector_t inode_sector) {
  struct dir_entry e;
  off_t ofs;
  bool success = false;

  ASSERT(dir != NULL);
  ASSERT(name != NULL);

  /* Check NAME for validity. */
  if (*name == '\0' || strlen(name) > NAME_MAX)
    return false;

  /* Check that NAME is not in use. */
  if (lookup(dir, name, NULL, NULL))
    goto done;

  /* Set OFS to offset of free slot.
     If there are no free slots, then it will be set to the
     current end-of-file.

     inode_read_at() will only return a short read at end of file.
     Otherwise, we'd need to verify that we didn't get a short
     read due to something intermittent such as low memory. */
  if (dir->inode != NULL) {
    for (ofs = 0; inode_read_at(dir->inode, &e, sizeof e, ofs) == sizeof e; ofs += sizeof e)
      if (!e.in_use)
        break;
  }

  /* Write slot. */
  e.in_use = true;
  strlcpy(e.name, name, sizeof e.name);
  e.inode_sector = inode_sector;
  success = inode_write_at(dir->inode, &e, sizeof e, ofs) == sizeof e;

done:
  return success;
}

/* Removes any entry for NAME in DIR.
   Returns true if successful, false on failure,
   which occurs only if there is no file with the given NAME. */
bool dir_remove(struct dir* dir, const char* name) {
  struct dir_entry e;
  struct inode* inode = NULL;
  bool success = false;
  off_t ofs;

  ASSERT(dir != NULL);
  ASSERT(name != NULL);

  /* Find directory entry. */
  if (!lookup(dir, name, &e, &ofs))
    goto done;

  /* Open inode. */
  inode = inode_open(e.inode_sector);
  if (inode == NULL)
    goto done;

  /* Erase directory entry. */
  e.in_use = false;
  if (inode_write_at(dir->inode, &e, sizeof e, ofs) != sizeof e)
    goto done;

  /* Remove inode. */
  inode_remove(inode);
  success = true;

done:
  inode_close(inode);
  return success;
}

/* Reads the next directory entry in DIR and stores the name in
   NAME.  Returns true if successful, false if the directory
   contains no more entries. */
bool dir_readdir(struct dir* dir, char name[NAME_MAX + 1]) {
  struct dir_entry e;

  while (inode_read_at(dir->inode, &e, sizeof e, dir->pos) == sizeof e) {
    dir->pos += sizeof e;
    if (e.in_use) {
      strlcpy(name, e.name, NAME_MAX + 1);
      return true;
    }
  }
  return false;
}

/* Extracts a file name part from *SRCP into PART, and updates *SRCP so that the
   next call will return the next file name part. Returns 1 if successful, 0 at
   end of string, -1 for a too-long file name part. */
static int get_next_part(char part[NAME_MAX + 1], const char** srcp) {
  const char* src = *srcp;
  char* dst = part;

  /* Skip leading slashes.  If it's all slashes, we're done. */
  while (*src == '/')
    src++;
  if (*src == '\0')
    return 0;

  /* Copy up to NAME_MAX character from SRC to DST.  Add null terminator. */
  while (*src != '/' && *src != '\0') {
    if (dst < part + NAME_MAX)
      *dst++ = *src;
    else
      return -1;
    src++;
  }
  *dst = '\0';

  /* Advance source pointer. */
  *srcp = src;
  return 1;
}

/* Returns the directory at the provided path. Unlike get_dir_at_filepath,
  it can return NULL, as it is not "best effort" */
struct dir* get_dir_at_path(const char* dir, struct dir* cwd) {
  struct dir* directory;
  struct dir* next_directory;
  if (dir[0] == '/')
    directory = dir_open_root();
  else
    directory = cwd;
  char part[NAME_MAX + 1];
  int res = get_next_part(part, &dir);
  next_directory = malloc(sizeof(struct dir));
  while (res != 0) {
    if (!dir_lookup(directory, part, &(next_directory->inode))) {
      if (directory != cwd)
        dir_close(directory);
      return NULL;
    }
    next_directory = dir_open(next_directory->inode);
    if (directory != cwd)
      dir_close(directory);
    directory = next_directory;
    res = get_next_part(part, &dir);
  }
  return directory;
}

/* Returns the directory one level above the provided file path. For example,
  providing a/b/c would return a dir* to b (if b exists). 
  Optionally, this also takes in a cwd for relative paths. */
struct dir* get_dir_at_filepath(const char* path, struct dir* cwd) {
  struct dir* dir;
  if (cwd == NULL) // If a cwd isn't provided, use root
    dir = dir_open_root();
  else
    dir = path[0] == '/' ? dir_open_root() : dir_reopen(cwd);

  char part[NAME_MAX + 1];
  int res = get_next_part(part, &path);
  /* Consecutively open directories until at the end of the path */
  for (;;) {
    struct inode** next_inode = (struct inode**)malloc(sizeof(struct inode*));
    if (!dir_lookup(dir, part, next_inode)) {
      return dir;
    }
    res = get_next_part(part, &path);
    if (res == 0) {
      return dir;
    }
    dir_close(dir);
    dir = dir_open(*next_inode);
    free(next_inode);
  }
}

/* This removes a a file or directory given by name. It performs
  It does its own check on the inode, so both directory and file paths
  can be passed without any other flags. */
bool filesys_remove(const char* name) {
  struct dir* cwd = dir_reopen(thread_current()->pcb->cwd);
  struct dir* dir = get_dir_at_filepath(name, cwd);
  dir_close(cwd);
  if (dir == NULL)
    return false;
  struct inode** to_remove = (struct inode**)malloc(sizeof(struct inode*));
  char part[NAME_MAX + 1];
  char* dup_name = name;
  /* Makes sure the entry exists. */
  while (get_next_part(part, &dup_name) != 0) {
  }
  bool success = dir_lookup(dir, part, to_remove);
  if (!success) {
    dir_close(dir);
    return false;
  }
  if (inode_is_dir(*to_remove)) {
    /* Make sure directory is not open or not empty. */
    struct dir* temp = dir_open(*to_remove);
    int i = 0;
    char useless[NAME_MAX + 1];
    for (; dir_readdir(temp, useless); i++) {
    }
    dir_close(temp);
    if (inode_is_open(*to_remove) || i > 2) {
      dir_close(dir);
      return false;
    }
  }
  /* After all error checking, remove the entry. */
  success = dir_remove(dir, part);
  dir_close(dir);
  free(to_remove);
  return success;
}

/* This returns the file name at a given path.*/
char* get_file_at_path(const char* path) {
  char part[NAME_MAX + 1];
  char* file = malloc(sizeof(NAME_MAX + 1));
  int res = get_next_part(part, &path);
  while (res != 0) {
    memcpy(file, part, (NAME_MAX + 1));
    res = get_next_part(part, &path);
  }
  return file;
}

/* Makes a new directory at the provided path, given that the path is valid
  and a directory does not exist there already. */
bool mk_dir(const char* new_dir_path, struct dir* cwd) {
  if (new_dir_path == "")
    return false;
  char part[NAME_MAX + 1];
  char* temp_path = new_dir_path;
  while (get_next_part(part, &temp_path) != 0) {
  }
  /* Check if something already exists with that name. */
  struct dir* parent_dir = get_dir_at_filepath(new_dir_path, cwd);
  struct dir* temp_parent = dir_reopen(parent_dir);
  struct inode** temp_inode = (struct inode**)malloc(sizeof(struct inode*));
  bool success = !dir_lookup(temp_parent, part, temp_inode);
  free(temp_inode);
  if (!success) {
    dir_close(parent_dir);
    dir_close(temp_parent);
    return false;
  }

  /* Create the directory and add it to the parent directory entries. */
  block_sector_t sector = -1;
  success = free_map_allocate(1, &sector);
  if (success && !(dir_create(sector, 2) && dir_add(temp_parent, part, sector))) {
    free_map_release(sector, 1);
  } else if (!success) {
    dir_close(temp_parent);
    dir_close(parent_dir);
    return false;
  }
  
  /* Add support for . and .. */
  struct dir* new_dir = dir_open(inode_open(sector));
  dir_add(new_dir, ".", sector);
  dir_add(new_dir, "..", inode_get_inumber(temp_parent->inode));
  dir_close(new_dir);
  dir_close(parent_dir);
  dir_close(temp_parent);
  return success;
}

/* This changes the process cwd to the directory path provided,
  given that one exists. Optionally, it takes in a cwd to support 
  relative paths. */
struct dir* ch_dir(const char* dir, struct dir* cwd) {
  struct dir* directory = malloc(sizeof(struct dir));
  struct dir* next_directory = malloc(sizeof(struct dir));
  if (dir[0] == '/')
    directory = dir_open_root();
  else
    directory = cwd;
  char part[NAME_MAX + 1];
  int res = get_next_part(part, &dir);
  while (res != 0) {
    if (!dir_lookup(directory, part, &next_directory->inode)) {
      if (directory != cwd)
        dir_close(directory);
      return NULL;
    }
    next_directory = dir_open(next_directory->inode);
    if (directory != cwd)
      dir_close(directory);
    directory = next_directory;
    res = get_next_part(part, &dir);
  }
  return (directory);
}

/* This returns the inode for a path within a directory. */
struct inode* get_dir_entry_inode(struct dir* dir, char* temp_path) {
  struct inode** temp_inode = (struct inode**)malloc(sizeof(struct inode*));
  char part[NAME_MAX + 1];
  while (get_next_part(part, &temp_path) != 0) {
  }
  dir_lookup(dir, part, temp_inode);
  struct inode* to_return = *temp_inode;
  free(temp_inode);
  return to_return;
}