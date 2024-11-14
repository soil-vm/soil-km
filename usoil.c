#include "soil_common.h"
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

int main(int argc, char **argv) {
  if (argc < 2) {
    return -1;
  }
  char *inpath = argv[1];

  char buf[1024];
  FILE *file = fopen(argv[1], "rb");
  if (file == NULL) {
    perror("fopen");
    return -1;
  }
  fseek(file, 0L, SEEK_END);
  size_t len = ftell(file);
  rewind(file);

  fread(buf, sizeof(char), len, file);

  // int infile = open(inpath, O_RDONLY);
  // char buf[1024];
  // int res = read(infile, buf, 1024);
  // if (res < 0)
  // {
  //   perror("read");
  //   return -1;
  // }

  int fd = open("/dev/soil", O_RDONLY);
  if (fd < 0) {
    perror("open");
    return -1;
  }
  // printf("%d %d\n", fd, SOIL_IOCTL_LOAD);
  printf("Soil binary `%s` is %zu bytes long.\n", inpath, len);
  soil_program_idx idx;
  struct soil_program prog = {
      .program = buf,
      .len = len,
      .idx = &idx,
  };
  int res = ioctl(fd, SOIL_IOCTL_LOAD_BINARY, &prog);
  if (res < 0) {
    perror("ioctl");
    return -1;
  }

  printf("prid = %zu\n", idx);

  soil_vm_idx vm;
  res = ioctl(fd, SOIL_IOCTL_CREATE_VM, &vm);
  if (res < 0) {
    perror("ioctl");
    return -1;
  }
  
  printf("%d vm = %zu\n", SOIL_IOCTL_CREATE_VM, vm);

  struct soil_vm_run_args args = {
      .program = idx,
      .vm = vm,
      .flags = SOIL_EXEC_ASYNC,
      // .flags = 0,
  };
  res = ioctl(fd, SOIL_IOCTL_RUN, &args);
  if (res < 0) {
    perror("ioctl");
  }

  printf("%zu\n", *(prog.idx));

  soil_vm_status_t status;
  struct soil_vm_status_args status_args = {
      .vm = vm,
      .status = &status,
  };

  do {
    res = ioctl(fd, SOIL_IOCTL_VM_STATUS, &status_args);
    if (res < 0) {
      perror("ioctl");
    }

    printf("VM status: %d\n", *(status_args.status));
    sleep(5);
  } while (*(status_args.status) != SOIL_VM_EXITED);

  close(fd);
  return 0;
}
