#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "userprog/process.h"
#include "lib/kernel/console.h"
#include <stdlib.h>
#include "userprog/pagedir.h"
#include "threads/vaddr.h"

#include "filesys/directory.h"

static void syscall_handler(struct intr_frame*);


void syscall_init(void) {
  intr_register_int(0x30, 3, INTR_ON, syscall_handler, "syscall");
}

/* verify if address is a valid address in user space */
bool valid_address(const void* addr) {
  if (addr != NULL && is_user_vaddr(addr)) {
    if ((unsigned long)pg_round_up(addr) - (unsigned long)addr < 4 &&
        (!is_user_vaddr(addr + 1) ||
         pagedir_get_page(thread_current()->pcb->pagedir, addr + 1) == NULL))
      return false;
    else if (*((int*)addr) == NULL)
      return false;
    return true;
  }
  return false;
}

bool valid_address_int(const void* addr) {
  if (addr != NULL && is_user_vaddr(addr) && addr < 0xc0000000) {
    if ((unsigned long)pg_round_up(addr) - (unsigned long)addr < 4 &&
        (!is_user_vaddr(addr + 1) ||
         pagedir_get_page(thread_current()->pcb->pagedir, addr + 1) == NULL))
      return false;
    return true;
  }
  return false;
}

/* Function prototypes */
pid_t exec(const char* cmd_line);

static void syscall_handler(struct intr_frame* f UNUSED) {
  uint32_t* args = ((uint32_t*)f->esp);
  int fd;
  unsigned int size;

  /*
   * The following print statement, if uncommented, will print out the syscall
   * number whenever a process enters a system call. You might find it useful
   * when debugging. It will cause tests to fail, however, so you should not
   * include it in your final submission.
   */

  /* printf("System call number: %d\n", args[0]); */
  if (f->esp <= f->eip || !valid_address(args)) {
    printf("%s: exit(%d)\n", thread_current()->pcb->process_name, -1);
    process_exit(-1);
    return;
  }
  switch (args[0]) {
    int fd;
    void* buffer;
    unsigned size;
    const char* file;
    struct list_elem* e;
    const char* dir;

    case SYS_HALT:
      cache_flush();
      shutdown_power_off();
      break;
    case SYS_EXIT:
      if (!valid_address_int(args + 1)) {
        printf("%s: exit(%d)\n", thread_current()->pcb->process_name, -1);
        process_exit(-1);
        break;
      }
      f->eax = args[1];
      printf("%s: exit(%d)\n", thread_current()->pcb->process_name, args[1]);
      process_exit(args[1]); 
      break;
    case SYS_WAIT:
      if (!valid_address_int(args + 1)) {
        printf("%s: exit(%d)\n", thread_current()->pcb->process_name, -1);
        process_exit(-1);
        break;
      }
      pid_t child_pid = args[1];
      f->eax = process_wait(child_pid);
      break;
    case SYS_PRACTICE:
      if (!valid_address_int(args + 1)) {
        printf("%s: exit(%d)\n", thread_current()->pcb->process_name, -1);
        process_exit(-1);
        break;
      }
      args[1]++;
      f->eax = args[1];
      break;
    case SYS_COMPUTE_E:
      if (!valid_address_int(args + 1)) {
        printf("%s: exit(%d)\n", thread_current()->pcb->process_name, -1);
        process_exit(-1);
        break;
      }
      fd = args[1];
      f->eax = sys_sum_to_e(fd);
      break;
    case SYS_EXEC:
      if (!valid_address(args + 1)) {
        printf("%s: exit(%d)\n", thread_current()->pcb->process_name, -1);
        process_exit(-1);
        break;
      }
      file = (char*)args[1];
      f->eax = exec(file);
      break;
    case SYS_CREATE:
      if (!valid_address(args + 1) || !valid_address_int(args + 2)) {
        printf("%s: exit(%d)\n", thread_current()->pcb->process_name, -1);
        process_exit(-1);
        break;
      }
      file = (char*)args[1];
      size = args[2];
      f->eax = false;
      if (strlen(file) < 15) {
        char* file_name = get_file_at_path(file);
        struct dir* d = get_dir_at_filepath(file, thread_current()->pcb->cwd);
        f->eax = filesys_create_dir(file_name, size, d);
        free(file_name);
      }
      break;
    case SYS_OPEN:
      if (!valid_address(args + 1)) {
        printf("%s: exit(%d)\n", thread_current()->pcb->process_name, -1);
        process_exit(-1);
        break;
      }
      const char* path = (const char*)args[1];
      struct file* new_file = filesys_open(path);
      if (new_file && inode_is_dir(pget_inode(new_file))) {
        file_close(new_file);
        new_file = NULL;
      }
      struct dir* new_dir = NULL;
      if (!new_file) {
        if (path[0] == '/') {
          if (strlen(path) == 1)
            new_dir = dir_open_root();
          else {
            struct dir* dir = get_dir_at_filepath(path, NULL);
            struct inode* inode = get_dir_entry_inode(dir, path);
            if (inode == NULL)
              f->eax = -1;
            else if (inode_is_dir(inode))
              new_dir = dir_open(inode);
            else
              new_file = file_open(inode);
            dir_close(dir);
          }
        } else {
          struct dir* dir = get_dir_at_filepath(path, thread_current()->pcb->cwd);
          struct inode* inode = get_dir_entry_inode(dir, path);
          if (inode == NULL)
            f->eax = -1;
          else if (inode_is_dir(inode))
            new_dir = dir_open(inode);
          else
            new_file = file_open(inode);
          dir_close(dir);
        }
      }
      if (new_file || new_dir) {
        fd_node* newFileNode = (fd_node*)malloc(sizeof(fd_node));
        newFileNode->fdIndex = thread_current()->pcb->next_fd;
        newFileNode->file = new_file;
        newFileNode->dir = new_dir;
        if (new_dir) {
          char name[15];
          dir_readdir(newFileNode->dir, name);
          dir_readdir(newFileNode->dir, name);
        }
        list_push_back(&thread_current()->pcb->fd_list, &newFileNode->elem);
        thread_current()->pcb->next_fd++;
        f->eax = newFileNode->fdIndex;
      }
      break;
    case SYS_FILESIZE:
      if (!valid_address_int(args + 1)) {
        printf("%s: exit(%d)\n", thread_current()->pcb->process_name, -1);
        process_exit(-1);
        break;
      }
      fd = args[1];
      for (e = list_begin(&thread_current()->pcb->fd_list);
           e != list_end(&thread_current()->pcb->fd_list); e = list_next(e)) {
        fd_node* node = list_entry(e, fd_node, elem);
        if (node->fdIndex == fd) {
          f->eax = file_length(node->file);
        }
      }
      break;
    case SYS_READ:
      if (!valid_address_int(args + 1) || !valid_address(args + 2) || args[2] >= 0xc0000000 ||
          !valid_address_int(args + 3)) {
        printf("%s: exit(%d)\n", thread_current()->pcb->process_name, -1);
        process_exit(-1);
        break;
      }
      fd = args[1];
      buffer = (void*)args[2];
      size = args[3];
      if (fd == 0) {
        for (unsigned i = 0; i < size; i++) {
          uint8_t key = input_getc();
          memcpy(buffer, &key, 1);
        }
        f->eax = size;
      } else if (fd == 1) { //should not be reading from stdout
        // throw error
      } else {
        for (e = list_begin(&thread_current()->pcb->fd_list);
             e != list_end(&thread_current()->pcb->fd_list); e = list_next(e)) {
          fd_node* node = list_entry(e, fd_node, elem);
          if (node->fdIndex == fd) {
            if (node->file == NULL) {
              f->eax = -1;
              break;
            }
            f->eax = file_read(node->file, buffer, size);
            break;
          }
        }
      }
      break;
    case SYS_WRITE:
      if (!valid_address_int(args + 1) || !valid_address(args + 2) ||
          !valid_address_int(args + 3)) {
        printf("%s: exit(%d)\n", thread_current()->pcb->process_name, -1);
        process_exit(-1);
        break;
      }
      fd = args[1];
      buffer = (void*)args[2];
      size = args[3];
      if (fd == 1) {
        putbuf(buffer, size);
      } else if (fd == 0) { //should not be writing to stdin
        // throw error
      } else {
        for (e = list_begin(&thread_current()->pcb->fd_list);
             e != list_end(&thread_current()->pcb->fd_list); e = list_next(e)) {
          fd_node* node = list_entry(e, fd_node, elem);
          if (node->fdIndex == fd) {
            if (node->file == NULL) {
              f->eax = -1;
              break;
            }
            f->eax = file_write(node->file, buffer, size);
            break;
          }
        }
      }
      break;
    case SYS_CLOSE:
      if (!valid_address_int(args + 1)) {
        printf("%s: exit(%d)\n", thread_current()->pcb->process_name, -1);
        process_exit(-1);
        break;
      }
      fd = args[1];
      for (e = list_begin(&thread_current()->pcb->fd_list);
           e != list_end(&thread_current()->pcb->fd_list); e = list_next(e)) {
        fd_node* node = list_entry(e, fd_node, elem);
        if (node->fdIndex == fd) {
          f->eax = file_close(node->file);
          list_remove(e);
          free(node);
          break;
        }
      }
      break;
    case SYS_REMOVE:
      if (!valid_address(args + 1)) {
        printf("%s: exit(%d)\n", thread_current()->pcb->process_name, -1);
        process_exit(-1);
        break;
      }
      file = (char*)args[1];
      f->eax = filesys_remove(file);
      break;
    case SYS_TELL:
      if (!valid_address_int(args + 1)) {
        printf("%s: exit(%d)\n", thread_current()->pcb->process_name, -1);
        process_exit(-1);
        break;
      }
      fd = args[1];
      for (e = list_begin(&thread_current()->pcb->fd_list);
           e != list_end(&thread_current()->pcb->fd_list); e = list_next(e)) {
        fd_node* node = list_entry(e, fd_node, elem);
        if (node->fdIndex == fd) {
          f->eax = file_tell(node->file);
        }
      }
      break;
    case SYS_SEEK:
      if (!valid_address_int(args + 1) || !valid_address_int(args + 2)) {
        printf("%s: exit(%d)\n", thread_current()->pcb->process_name, -1);
        process_exit(-1);
        break;
      }
      fd = args[1];
      unsigned position = args[2];
      for (e = list_begin(&thread_current()->pcb->fd_list);
           e != list_end(&thread_current()->pcb->fd_list); e = list_next(e)) {
        fd_node* node = list_entry(e, fd_node, elem);
        if (node->fdIndex == fd) {
          f->eax = file_seek(node->file, position);
        }
      }
      break;
    case SYS_CHDIR:
      if (!valid_address(args + 1)) {
        printf("%s: exit(%d)\n", thread_current()->pcb->process_name, -1);
        process_exit(-1);
        break;
      }
      struct dir* new_directory = get_dir_at_path((const char*)args[1], thread_current()->pcb->cwd);
      if (new_directory != NULL) {
        dir_close(thread_current()->pcb->cwd);
        thread_current()->pcb->cwd = new_directory;
        f->eax = true;
      } else {
        f->eax = false;
      }
      break;
    case SYS_MKDIR:
      if (!valid_address(args + 1)) {
        printf("%s: exit(%d)\n", thread_current()->pcb->process_name, -1);
        process_exit(-1);
        break;
      }
      dir = (const char*)args[1];
      if (strlen(dir) == 0) {
        f->eax = false;
        break;
      }
      f->eax = mk_dir(dir, thread_current()->pcb->cwd);
      break;
    case SYS_READDIR:
      if (!valid_address_int(args + 1) || !valid_address(args + 2)) {
        printf("%s: exit(%d)\n", thread_current()->pcb->process_name, -1);
        process_exit(-1);
        break;
      }
      fd = args[1];
      char* name = args[2];
      f->eax = false;
      for (struct list_elem* e = list_begin(&thread_current()->pcb->fd_list);
           e != list_end(&thread_current()->pcb->fd_list); e = list_next(e)) {
        fd_node* node = list_entry(e, fd_node, elem);
        if (node->fdIndex == fd && node->dir != NULL) {
          f->eax = dir_readdir(node->dir, name);
          break;
        }
      }
      break;
    case SYS_ISDIR:
      if (!valid_address_int(args + 1)) {
        printf("%s: exit(%d)\n", thread_current()->pcb->process_name, -1);
        process_exit(-1);
        break;
      }
      fd = args[1];
      f->eax = false;
      for (e = list_begin(&thread_current()->pcb->fd_list);
           e != list_end(&thread_current()->pcb->fd_list); e = list_next(e)) {
        fd_node* node = list_entry(e, fd_node, elem);
        if (node->fdIndex == fd) {
          f->eax = node->dir != NULL;
          break;
        }
      }
      break;
    case SYS_INUMBER:
      if (!valid_address_int(args + 1)) {
        printf("%s: exit(%d)\n", thread_current()->pcb->process_name, -1);
        process_exit(-1);
        break;
      }
      fd = args[1];
      for (e = list_begin(&thread_current()->pcb->fd_list);
           e != list_end(&thread_current()->pcb->fd_list); e = list_next(e)) {
        fd_node* node = list_entry(e, fd_node, elem);
        if (node->fdIndex == fd) {
          if (node->file)
            f->eax = pget_inum(node->file);
          else
            f->eax = inode_get_inumber(dir_get_inode(node->dir));
          break;
        }
      }
      break;
    case SYS_CACHE_HR:
      f->eax = get_hitrate();
      break;
    case SYS_CACHE_RESET:
      reset_cache();
      break;
    case SYS_BLK_RD:
      f->eax = get_fs_reads();
      break;
    case SYS_BLK_WR:
      f->eax = get_fs_writes();
      break;
  }
}
