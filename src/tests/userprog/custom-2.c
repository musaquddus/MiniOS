/* Close one file, should still be able to write*/

#include <syscall.h>
#include "tests/userprog/sample.inc"
#include "tests/lib.h"
#include "tests/main.h"

void test_main(void) {
  int h1 = open("sample.txt");
  int h2 = open("sample.txt");
  close(h1);
  write(h2, sample, sizeof sample - 1);
}
