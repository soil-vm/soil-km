#ifndef VM_H
#define VM_H

#include <linux/types.h>

typedef u8 Byte;
typedef s64 Word;

typedef u64 soil_program_idx;
typedef u64 soil_vm_idx;

typedef enum {
  SOIL_VM_INIT,
  SOIL_VM_RUNNING,
  SOIL_VM_EXITED,
} soil_vm_status_t;

struct soil_program
{
  Byte *program;
  int len;
  soil_program_idx *idx;
};

#define SOIL_EXEC_ASYNC 1

struct soil_vm_run_args {
  soil_program_idx program;
  soil_vm_idx vm;
  u8 flags;
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


#define MEMORY_SIZE 1000000
#define TRACE_INSTRUCTIONS 1
#define TRACE_CALLS 0
#define TRACE_CALL_ARGS 0
#define TRACE_SYSCALLS 1
#define CALL_STACK_SIZE 1024
#define TRY_STACK_SIZE 1024

typedef struct { int pos; char* label; int len; } LabelAndPos;
typedef struct { LabelAndPos* entries; int len; } Labels;

typedef struct { Word catch; Word call_stack_len; Word sp; } Try;


typedef struct soil_vm {
  Byte *byte_code;
  Word ip;
  Word reg[8];
  Byte mem[MEMORY_SIZE];
  Word call_stack[CALL_STACK_SIZE];
  Word call_stack_len;
  Try try_stack[TRY_STACK_SIZE];
  Word try_stack_len;
  Labels labels;
  soil_vm_status_t status;
} soil_vm_t;

void init_vm(soil_vm_t *vm, Byte* bin, int bin_len);
void run(soil_vm_t *vm);

#endif
