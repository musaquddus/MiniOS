#include <random.h>
#include <syscall.h>
#include "tests/lib.h"
#include "tests/main.h"

void test_main(void) {
  int fd;
  char* buf = "a";
  char* file_name = "benZ2\0";

  CHECK(create(file_name, 0), "create \"%s\"", file_name);
  CHECK((fd = open(file_name)) > 1, "open \"%s\"", file_name);
  int bytes_wrote = 0;
  int block_writes1 = get_block_writes();
  for (int ofs = 0; ofs < 512 * 128; ofs++)
    bytes_wrote += write(fd, buf, 1);

  CHECK(bytes_wrote == 512 * 128, "wrote 64 kiB to \"%s\"", file_name);

  int block_writes2 = get_block_writes();
  CHECK(block_writes2 - block_writes1 <= 128, "block writes less than 129!");
}
