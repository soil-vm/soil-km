#ifndef SOIL_COMMON_H
#define SOIL_COMMON_H

#include <stddef.h>
#include <stdint.h>

typedef uint8_t Byte;
typedef int64_t Word;

typedef size_t soil_program_idx;
typedef size_t soil_vm_idx;

typedef enum {
  SOIL_VM_INIT,
  SOIL_VM_RUNNING,
  SOIL_VM_EXITED,
} soil_vm_status_t;

struct soil_program {
  Byte *program;
  int len;
  soil_program_idx *idx;
};

#define SOIL_EXEC_ASYNC 1

struct soil_vm_run_args {
  soil_program_idx program;
  soil_vm_idx vm;
  uint8_t flags;
};

struct soil_vm_status_args {
  soil_vm_idx vm;
  soil_vm_status_t *status;
};

#define IOC_MAGIC 100
#define SOIL_IOCTL_LOAD_BINARY _IOWR(IOC_MAGIC, 0, struct soil_program*)
#define SOIL_IOCTL_CREATE_VM _IOWR(IOC_MAGIC, 1, soil_vm_idx*)
#define SOIL_IOCTL_RUN _IOW(IOC_MAGIC, 2, struct soil_vm_run_args*)
#define SOIL_IOCTL_VM_STATUS _IOWR(IOC_MAGIC, 3, struct soil_vm_status_args*)
#define SOIL_IOCTL_UNLOAD_BINARY _IOW(IOC_MAGIC, 4, soil_program_idx)
#define SOIL_IOCTL_DELETE_VM _IOW(IOC_MAGIC, 5, soil_vm_idx)

#endif
