/* This test combines priority-donate-chain and priority-donate-one, to 
test if thread_set_priority and lock_release properly manipulates the execution 
order, i.e. it yields the threads prior to completion if the updated 
effective priority dictates.
We create a lock and two high priority threads, 1 and 2. We see that if
we set priority of 2, the highest, to something between main and 1, it 
takes the effective priority of thread 1. This also means that 1 will begin
executing before 2 finishes, since it releases the lock. 
*/

#include <stdio.h>
#include "tests/threads/tests.h"
#include "threads/init.h"

#include "threads/synch.h"
#include "threads/thread.h"

static thread_func acquire1_thread_func;
static thread_func acquire2_thread_func;

void test_custom_2_1(void) {
  struct lock lock;

  /* This test does not work with the MLFQS. */
  ASSERT(active_sched_policy == SCHED_PRIO);

  /* Make sure our priority is the default. */
  ASSERT(thread_get_priority() == PRI_DEFAULT);

  lock_init(&lock);
  lock_acquire(&lock);
  thread_create("acquire1", PRI_DEFAULT + 2, acquire1_thread_func, &lock);
  msg("This thread should have priority %d.  Actual priority: %d.", PRI_DEFAULT + 2,
      thread_get_priority());
  thread_create("acquire2", PRI_DEFAULT + 4, acquire2_thread_func, &lock);
  msg("This thread should have priority %d.  Actual priority: %d.", PRI_DEFAULT + 4,
      thread_get_priority());
  lock_release(&lock);
  msg("acquire1, acquire2 must already have finished, in that order.");
  msg("This should be the last line before finishing this test.");
}

static void acquire1_thread_func(void* lock_) {
  struct lock* lock = lock_;

  lock_acquire(lock);
  msg("acquire1: got the lock");
  lock_release(lock);
  msg("acquire1: done");
}

static void acquire2_thread_func(void* lock_) {
  struct lock* lock = lock_;

  lock_acquire(lock);
  msg("acquire2: got the lock");
  thread_set_priority(PRI_DEFAULT + 1);
  msg("acquire2: set priority to %d.  Effective priority should be %d. Actual effective priority: "
      "%d.",
      PRI_DEFAULT + 1, PRI_DEFAULT + 2, thread_get_priority());
  lock_release(lock);
  msg("acquire2: My priority should now be %d, my actual priority is %d.", PRI_DEFAULT + 1,
      thread_get_priority());
  msg("acquire2: done");
}
