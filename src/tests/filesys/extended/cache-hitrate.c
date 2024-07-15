#include <random.h>
#include <syscall.h>
#include "tests/lib.h"
#include "tests/main.h"

void test_main(void) {
  int fd;
  char buf[512];
  char* file_name = "benZ\0";
  int hitrate;

  random_bytes(buf, sizeof buf);

  CHECK(create(file_name, 0), "create \"%s\"", file_name);
  CHECK((fd = open(file_name)) > 1, "open \"%s\"", file_name);
  CHECK(write(fd, buf, 512) > 0, "write random bytes to \"%s\"", file_name);

  cache_reset();
  hitrate = cache_hitrate();
  CHECK(hitrate == 0, "reset cache, hitrate is %d percent", hitrate);

  seek(fd, 0);
  CHECK(read(fd, buf, 512) > 0, "reading from \"%s\"", file_name);
  int f_hitrate = cache_hitrate();
  seek(fd, 0);
  CHECK(read(fd, buf, 512) > 0, "reading from \"%s\"", file_name);
  int s_hitrate = cache_hitrate();
  CHECK(s_hitrate > f_hitrate, "cache hitrate improved!");
}
