#ifndef USERPROG_PROCESS_H
#define USERPROG_PROCESS_H

#include "threads/thread.h"
#include <stdint.h>

// At most 8MB can be allocated to the stack
// These defines will be used in Project 2: Multithreading
#define MAX_STACK_PAGES (1 << 11)
#define MAX_THREADS 127

/* PIDs and TIDs are the same type. PID should be
   the TID of the main thread of the process */
typedef tid_t pid_t;

/* Thread functions (Project 2: Multithreading) */
typedef void (*pthread_fun)(void*);
typedef void (*stub_fun)(pthread_fun, void*);

/* The process control block for a given process. Since
   there can be multiple threads per process, we need a separate
   PCB from the TCB. All TCBs in a process will have a pointer
   to the PCB, and the PCB will have a pointer to the main thread
   of the process, which is `special`. */
struct process {
  /* Owned by process.c. */
  uint32_t* pagedir;            /* Page directory. */
  char process_name[16];        /* Name of the main thread */
  struct thread* main_thread;   /* Pointer to main thread */
  struct list fd_list;          /* A pintos list of fds open to the process. */
  int next_fd;                  /* Next available fd. */
  struct list child_list;       /* List of child nodes. */
  struct pcb_metadata* my_data; /* Metadata for current process */
  struct file*
      executable; /*deny write to this file,store this file when load, then enable write when exit*/
  struct dir* cwd;
};

/* Process States */
enum procstate { PRERUN, LOADFAIL, RUNNING, KILLED, EXITED };

/* Node for child_list in process struct*/
struct child_node {                //
  struct pcb_metadata* child_data; //shared with the parent process
  pid_t child_pid;
  struct list_elem elem;
  bool waited;
};

/* Shared data between parent and child */
struct pcb_metadata {
  struct semaphore exec_sema;
  struct semaphore wait_sema;
  struct lock edit_lock;
  int ref_num; // how many references (parent or child) are referencing this structure
  enum procstate procstate;
  int exit_status;
};

/* Used in start process */
struct startup_pack {
  char* fn_copy;
  struct dir* parent_cwd;
  struct pcb_metadata* my_data;
};

/* Structure to store data for a pintos list of fds (node in the list). */
typedef struct FileDescriptorNode {
  int fdIndex;
  struct file* file;
  struct dir* dir;
  struct list_elem elem;
} fd_node;

void userprog_init(void);

pid_t exec(const char* cmd_line);

pid_t process_execute(const char* file_name);
int process_wait(pid_t);
void process_exit(int);
void process_activate(void);

bool is_main_thread(struct thread*, struct process*);
pid_t get_pid(struct process*);

tid_t pthread_execute(stub_fun, pthread_fun, void*);
tid_t pthread_join(tid_t);
void pthread_exit(void);
void pthread_exit_main(void);

bool pfile_is_dir(struct file*);
struct inode* pget_inode(struct file*);
int pget_inum(struct file*);

#endif /* userprog/process.h */
