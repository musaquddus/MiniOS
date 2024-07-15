/* Should not error as write should occur after exec finishes */

#include <syscall.h>
#include "tests/userprog/sample.inc"
#include "tests/lib.h"
#include "tests/main.h"

void test_main(void) {
  int fd = open("child-simple");
  pid_t proc1 = exec("child-simple");
  write(fd, sample, sizeof sample - 1);
  wait(proc1);
}
