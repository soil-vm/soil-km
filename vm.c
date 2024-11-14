// #include <stdarg.h>
// #include <stdint.h>
#include "vm.h"
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/string.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Clemens Tiedt");

void eprintf(const char *fmt, ...) {
  va_list args;
  struct va_format vaf = {
      .fmt = fmt,
  };
  va_start(args, fmt);
  vaf.va = &args;
  printk(KERN_INFO "%pV", &vaf);
  va_end(args);
}
void soil_panic(soil_vm_t *vm, int exit_code, const char *fmt, ...) {
  va_list args;
  struct va_format vaf = {
      .fmt = fmt,
  };
  va_start(args, fmt);
  printk(KERN_INFO "%pV", &vaf);
  va_end(args);
  if (vm) {
    vm->status = SOIL_VM_EXITED;
  }
}

#define SP ((vm)->reg[0])
#define ST ((vm)->reg[1])
#define REGA ((vm)->reg[2])
#define REGB ((vm)->reg[3])
#define REGC ((vm)->reg[4])
#define REGD ((vm)->reg[5])
#define REGE ((vm)->reg[6])
#define REGF ((vm)->reg[7])

void (*syscall_handlers[256])(soil_vm_t *);

LabelAndPos find_label(soil_vm_t *vm, Word pos) {
  for (int j = vm->labels.len - 1; j >= 0; j--)
    if (vm->labels.entries[j].pos <= pos)
      return vm->labels.entries[j];
  LabelAndPos lap;
  lap.pos = 0;
  lap.len = 0;
  return lap;
}
void print_stack_entry(soil_vm_t *vm, Word pos) {
  eprintf("%8lx ", pos);
  for (int j = vm->labels.len - 1; j >= 0; j--)
    if (vm->labels.entries[j].pos <= pos) {
      for (int k = 0; k < vm->labels.entries[j].len; k++)
        eprintf("%c", vm->labels.entries[j].label[k]);
      break;
    }
  eprintf("\n");
}
void dump_and_panic(soil_vm_t *vm, char *fmt, ...) {
  va_list args;
  struct va_format vaf = {
      .fmt = fmt,
  };
  va_start(args, fmt);
  printk(KERN_INFO "%pV", &vaf);
  va_end(args);

  eprintf("\n");
  eprintf("Stack:\n");
  for (int i = 0; i < vm->call_stack_len; i++)
    print_stack_entry(vm, vm->call_stack[i] - 1);
  print_stack_entry(vm, vm->ip);
  eprintf("\n");
  eprintf("Registers:\n");
  eprintf("sp = %8ld %8lx\n", SP, SP);
  eprintf("st = %8ld %8lx\n", ST, ST);
  eprintf("a  = %8ld %8lx\n", REGA, REGA);
  eprintf("b  = %8ld %8lx\n", REGB, REGB);
  eprintf("c  = %8ld %8lx\n", REGC, REGC);
  eprintf("d  = %8ld %8lx\n", REGD, REGD);
  eprintf("e  = %8ld %8lx\n", REGE, REGE);
  eprintf("f  = %8ld %8lx\n", REGF, REGF);
  eprintf("\n");
  vm->status = SOIL_VM_EXITED;
  // TODO: Deal with file io...
  // FILE* dump = fopen("crash", "w+");
  // fwrite(mem, 1, MEMORY_SIZE, dump);
  // fclose(dump);
  // eprintf("Memory dumped to crash.\n");
  // exit(1);
}

void init_syscalls(void);

void init_vm(soil_vm_t *vm, Byte *bin, int bin_len) {
  for (int i = 0; i < 8; i++)
    vm->reg[i] = 0;
  SP = MEMORY_SIZE;
  vm->byte_code = 0;
  vm->ip = 0;
  vm->call_stack_len = 0;
  vm->status = SOIL_VM_INIT;

  init_syscalls();

  int cursor = 0;
#define EAT_BYTE                                                               \
  ({                                                                           \
    if (cursor >= bin_len)                                                     \
      soil_panic(vm, 1, "binary incomplete");                                  \
    Byte byte = bin[cursor];                                                   \
    cursor++;                                                                  \
    byte;                                                                      \
  })
#define EAT_WORD                                                               \
  ({                                                                           \
    if (cursor > bin_len - 8)                                                  \
      soil_panic(vm, 1, "binary incomplete");                                  \
    Word word;                                                                 \
    for (int i = 7; i >= 0; i--)                                               \
      word = (word << 8) + bin[cursor + i];                                    \
    cursor += 8;                                                               \
    word;                                                                      \
  })
#define CHECK_MAGIC_BYTE(c)                                                    \
  if (EAT_BYTE != c)                                                           \
    soil_panic(vm, 1, "magic bytes don't match");

  CHECK_MAGIC_BYTE('s')
  CHECK_MAGIC_BYTE('o')
  CHECK_MAGIC_BYTE('i')
  CHECK_MAGIC_BYTE('l')

  while (cursor < bin_len) {
    int section_type = EAT_BYTE;
    int section_len = EAT_WORD;
    if (section_type == 0) {
      // byte code
      vm->byte_code = kmalloc(section_len, GFP_KERNEL);
      for (int j = 0; j < section_len; j++)
        vm->byte_code[j] = EAT_BYTE;
    } else if (section_type == 1) {
      // initial memory
      if (section_len >= MEMORY_SIZE)
        soil_panic(vm, 1, "initial memory too big");
      for (int j = 0; j < section_len; j++)
        vm->mem[j] = EAT_BYTE;
    } else if (section_type == 3) {
      // debug info
      vm->labels.len = EAT_WORD;
      vm->labels.entries =
          kmalloc(sizeof(LabelAndPos) * vm->labels.len, GFP_KERNEL);
      for (int i = 0; i < vm->labels.len; i++) {
        vm->labels.entries[i].pos = EAT_WORD;
        vm->labels.entries[i].len = EAT_WORD;
        vm->labels.entries[i].label = bin + cursor;
        cursor += vm->labels.entries[i].len;
      }
    } else {
      cursor += section_len;
    }
  }

  // eprintf("Memory:");
  // for (int i = 0; i < MEMORY_SIZE; i++) eprintf(" %02x", mem[i]);
  // eprintf("\n");
}

void dump_reg(soil_vm_t *vm) {
  eprintf(
      "ip = %lx, sp = %lx, st = %lx, a = %lx, b = %lx, c = %lx, d = %lx, e = "
      "%lx, f = %lx\n",
      vm->ip, SP, ST, REGA, REGB, REGC, REGD, REGE, REGF);
}

typedef union {
  double f;
  int64_t i;
} fi;

void run_single(soil_vm_t *vm) {
#define REG1 vm->reg[vm->byte_code[vm->ip + 1] & 0x0f]
#define REG2 vm->reg[vm->byte_code[vm->ip + 1] >> 4]

  Byte opcode = vm->byte_code[vm->ip];
  switch (opcode) {
  case 0x00:
    vm->ip += 1;
    break;     // nop
  case 0xe0: { // panic
    if (vm->try_stack_len > 0) {
      vm->try_stack_len--;
      vm->call_stack_len = vm->try_stack[vm->try_stack_len].call_stack_len;
      vm->ip = vm->try_stack[vm->try_stack_len].catch;
      break;
    } else {
      dump_and_panic(vm, "panicked");
      return;
    }
  }
  case 0xe1: { // trystart
    Word catch = *(Word *)(vm->byte_code + vm->ip + 1);
    vm->try_stack[vm->try_stack_len].catch = catch;
    vm->try_stack[vm->try_stack_len].call_stack_len = vm->call_stack_len;
    vm->try_stack[vm->try_stack_len].sp = SP;
    vm->try_stack_len++;
    vm->ip += 9;
    break;
  }
  case 0xe2:
    vm->try_stack_len--;
    vm->ip += 1;
    break; // tryend
  case 0xd0:
    REG1 = REG2;
    vm->ip += 2;
    break; // move
  case 0xd1:
    REG1 = *(Word *)(vm->byte_code + vm->ip + 2);
    vm->ip += 10;
    break; // movei
  case 0xd2:
    REG1 = vm->byte_code[vm->ip + 2];
    vm->ip += 3;
    break;     // moveib
  case 0xd3: { // load
    if (REG2 >= MEMORY_SIZE)
      dump_and_panic(vm, "invalid load");
    REG1 = *(Word *)(vm->mem + REG2);
    vm->ip += 2;
    break;
  }
  case 0xd4: { // loadb
    if (REG2 >= MEMORY_SIZE)
      dump_and_panic(vm, "invalid loadb");
    REG1 = vm->mem[REG2];
    vm->ip += 2;
    break;
  }
  case 0xd5: { // store
    if (REG1 >= MEMORY_SIZE)
      dump_and_panic(vm, "invalid store");
    *(Word *)(vm->mem + REG1) = REG2;
    vm->ip += 2;
    break;
  }
  case 0xd6: { // storeb
    if (REG1 >= MEMORY_SIZE)
      dump_and_panic(vm, "invalid storeb");
    vm->mem[REG1] = REG2;
    vm->ip += 2;
    break;
  }
  case 0xd7:
    SP -= 8;
    *(Word *)(vm->mem + SP) = REG1;
    vm->ip += 2;
    break; // push
  case 0xd8:
    REG1 = *(Word *)(vm->mem + SP);
    SP += 8;
    vm->ip += 2;
    break; // pop
  case 0xf0:
    vm->ip = *(Word *)(vm->byte_code + vm->ip + 1);
    break;     // jump
  case 0xf1: { // cjump
    if (ST != 0)
      vm->ip = *(Word *)(vm->byte_code + vm->ip + 1);
    else
      vm->ip += 9;
    break;
  }
  case 0xf2: { // call
    if (TRACE_CALLS) {
      for (int i = 0; i < vm->call_stack_len; i++)
        eprintf(" ");
      LabelAndPos lap = find_label(vm, *(Word *)(vm->byte_code + vm->ip + 1));
      for (int i = 0; i < lap.len; i++)
        eprintf("%c", lap.label[i]);
      if (TRACE_CALL_ARGS) {
        for (int i = vm->call_stack_len + lap.len; i < 50; i++)
          eprintf(" ");
        for (int i = SP; i < MEMORY_SIZE && i < SP + 40; i++) {
          if (i % 8 == 0)
            eprintf(" |");
          eprintf(" %02x", vm->mem[i]);
        }
      }
      eprintf("\n");
    }

    Word return_target = vm->ip + 9;
    vm->call_stack[vm->call_stack_len] = return_target;
    vm->call_stack_len++;
    vm->ip = *(Word *)(vm->byte_code + vm->ip + 1);
    break;
  }
  case 0xf3: { // ret
    vm->call_stack_len--;
    vm->ip = vm->call_stack[vm->call_stack_len];
    break;
  }
  case 0xf4:
    vm->ip += 2;
    syscall_handlers[vm->byte_code[vm->ip - 1]](vm);
    break; // syscall
  case 0xc0:
    ST = REG1 - REG2;
    vm->ip += 2;
    break; // cmp
  case 0xc1:
    ST = ST == 0 ? 1 : 0;
    vm->ip += 1;
    break; // isequal
  case 0xc2:
    ST = ST < 0 ? 1 : 0;
    vm->ip += 1;
    break; // isless
  case 0xc3:
    ST = ST > 0 ? 1 : 0;
    vm->ip += 1;
    break; // isgreater
  case 0xc4:
    ST = ST <= 0 ? 1 : 0;
    vm->ip += 1;
    break; // islessequal
  case 0xc5:
    ST = ST >= 0 ? 1 : 0;
    vm->ip += 1;
    break; // isgreaterequal
  case 0xc6:
    ST = ST != 0 ? 1 : 0;
    vm->ip += 1;
    break;     // isnotequal
  case 0xc7: { // fcmp
    // fi fi1 = {.i = REG1};
    // fi fi2 = {.i = REG2};
    // fi res = {.f = fi1.f - fi2.f};
    // ST = res.i; ip += 2; break;
    dump_and_panic(vm, "floating point arithmetic is not supported in ksoil");
    break;
  }
  case 0xc8: { // fisequal
    // fi fi = {.i = ST};
    // ST = fi.f == 0.0 ? 1 : 0; ip += 1; break;
    dump_and_panic(vm, "floating point arithmetic is not supported in ksoil");
    break;
  }
  case 0xc9: { // fisless
    // fi fi = {.i = ST};
    // ST = fi.f < 0.0 ? 1 : 0; ip += 1; break;
    dump_and_panic(vm, "floating point arithmetic is not supported in ksoil");
    break;
  }
  case 0xca: { // fisgreater
    // fi fi = {.i = ST};
    // ST = fi.f > 0.0 ? 1 : 0; ip += 1; break;
    dump_and_panic(vm, "floating point arithmetic is not supported in ksoil");
    break;
  }
  case 0xcb: { // fislessqual
    // fi fi = {.i = ST};
    // ST = fi.f <= 0.0 ? 1 : 0; ip += 1; break;
    dump_and_panic(vm, "floating point arithmetic is not supported in ksoil");
    break;
  }
  case 0xcc: { // fisgreaterequal
    // fi fi = {.i = ST};
    // ST = fi.f >= 0.0 ? 1 : 0; ip += 1; break;
    dump_and_panic(vm, "floating point arithmetic is not supported in ksoil");
    break;
  }
  case 0xcd: { // fisnotequal
    // fi fi = {.i = ST};
    // ST = fi.f != 0.0 ? 1 : 0; ip += 1; break;
    dump_and_panic(vm, "floating point arithmetic is not supported in ksoil");
    break;
  }
  case 0xce: { // inttofloat
    // fi fi = {.f = (double)REG1};
    // REG1 = fi.i; ip += 2; break;
    dump_and_panic(vm, "floating point arithmetic is not supported in ksoil");
    break;
  }
  case 0xcf: { // floattoint
    // fi fi = {.i = REG1};
    // REG1 = (int64_t)fi.f; ip += 2; break;
    dump_and_panic(vm, "floating point arithmetic is not supported in ksoil");
    break;
  }
  case 0xa0:
    REG1 += REG2;
    vm->ip += 2;
    break; // add
  case 0xa1:
    REG1 -= REG2;
    vm->ip += 2;
    break; // sub
  case 0xa2:
    REG1 *= REG2;
    vm->ip += 2;
    break;     // mul
  case 0xa3: { // div
    if (REG2 == 0)
      dump_and_panic(vm, "div by zero");
    REG1 /= REG2;
    vm->ip += 2;
    break;
  }
  case 0xa4: { // rem
    if (REG2 == 0)
      dump_and_panic(vm, "rem by zero");
    REG1 %= REG2;
    vm->ip += 2;
    break;
  }
  case 0xa5: { // fadd
    // fi fi1 = {.i = REG1};
    // fi fi2 = {.i = REG2};
    // fi res = {.f = fi1.f + fi2.f};
    // REG1 = res.i; ip += 2; break;
    dump_and_panic(vm, "floating point arithmetic is not supported in ksoil");
    break;
  }
  case 0xa6: { // fsub
    // fi fi1 = {.i = REG1};
    // fi fi2 = {.i = REG2};
    // fi res = {.f = fi1.f - fi2.f};
    // REG1 = res.i; ip += 2; break;
    dump_and_panic(vm, "floating point arithmetic is not supported in ksoil");
    break;
  }
  case 0xa7: { // fmul
    // fi fi1 = {.i = REG1};
    // fi fi2 = {.i = REG2};
    // fi res = {.f = fi1.f * fi2.f};
    // REG1 = res.i; ip += 2; break;
    dump_and_panic(vm, "floating point arithmetic is not supported in ksoil");
    break;
  }
  case 0xa8: { // fdiv
    // fi fi1 = {.i = REG1};
    // fi fi2 = {.i = REG2};
    // if (fi2.f == 0.0) dump_and_panic("fdiv by zero");
    // fi res = {.f = fi1.f / fi2.f};
    // REG1 = res.i; ip += 2; break;
    dump_and_panic(vm, "floating point arithmetic is not supported in ksoil");
    break;
  }
  case 0xb0:
    REG1 &= REG2;
    vm->ip += 2;
    break; // and
  case 0xb1:
    REG1 |= REG2;
    vm->ip += 2;
    break; // or
  case 0xb2:
    REG1 ^= REG2;
    vm->ip += 2;
    break; // xor
  case 0xb3:
    REG1 = ~REG1;
    vm->ip += 2;
    break; // not
  default:
    dump_and_panic(vm, "invalid instruction %dx", opcode);
    return;
  }
  if (TRACE_INSTRUCTIONS) {
    eprintf("ran %x -> ", opcode);
    dump_reg(vm);
  }
}

void run(soil_vm_t *vm) {
  vm->status = SOIL_VM_RUNNING;
  for (int i = 0; vm->status != SOIL_VM_EXITED; i++) {
    // dump_reg();
    // eprintf("Memory:");
    // for (int i = 0x18650; i < MEMORY_SIZE; i++)
    //   eprintf("%c%02x", i == SP ? '|' : ' ', mem[i]);
    // eprintf("\n");
    run_single(vm);
  }
}

void syscall_none(soil_vm_t *vm) {
  dump_and_panic(vm, "invalid syscall number");
}
void syscall_exit(soil_vm_t *vm) {
  if (TRACE_SYSCALLS)
    eprintf("syscall exit(%ld)\n", REGA);
  eprintf("exited with %ld\n", REGA);
  // exit(REGA);
  vm->status = SOIL_VM_EXITED;
}
void syscall_print(soil_vm_t *vm) {
  if (TRACE_SYSCALLS)
    eprintf("syscall print(%lx, %ld)\n", REGA, REGB);
  for (int i = 0; i < REGB; i++)
    printk(KERN_INFO "%c", vm->mem[REGA + i]);
  if (TRACE_CALLS || TRACE_SYSCALLS)
    eprintf("\n");
}
void syscall_log(soil_vm_t *vm) {
  if (TRACE_SYSCALLS)
    eprintf("syscall log(%lx, %ld)\n", REGA, REGB);
  for (int i = 0; i < REGB; i++)
    eprintf("%c", vm->mem[REGA + i]);
  if (TRACE_CALLS || TRACE_SYSCALLS)
    eprintf("\n");
}
void syscall_create(soil_vm_t *vm) {
  if (TRACE_SYSCALLS)
    eprintf("syscall create(%lx, %ld)\n", REGA, REGB);
  char filename[REGB + 1];
  for (int i = 0; i < REGB; i++)
    filename[i] = vm->mem[REGA + i];
  filename[REGB] = 0;
  // REGA = (Word)fopen(filename, "w+");
  // TODO: Replace with kernel file IO
}
void syscall_open_reading(soil_vm_t *vm) {
  if (TRACE_SYSCALLS)
    eprintf("syscall open_reading(%lx, %ld)\n", REGA, REGB);
  char filename[REGB + 1];
  for (int i = 0; i < REGB; i++)
    filename[i] = vm->mem[REGA + i];
  filename[REGB] = 0;
  // REGA = (Word)fopen(filename, "r");
  // TODO: Replace with kernel file IO
}
void syscall_open_writing(soil_vm_t *vm) {
  if (TRACE_SYSCALLS)
    eprintf("syscall open_writing(%lx, %ld)\n", REGA, REGB);
  char filename[REGB + 1];
  for (int i = 0; i < REGB; i++)
    filename[i] = vm->mem[REGA + i];
  filename[REGB] = 0;
  // REGA = (Word)fopen(filename, "w+");
  // TODO: Replace with kernel file IO
}
void syscall_read(soil_vm_t *vm) {
  if (TRACE_SYSCALLS)
    eprintf("syscall read(%ld, %lx, %ld)\n", REGA, REGB, REGC);
  // REGA = fread(mem + REGB, 1, REGC, (FILE*)REGA);
  // TODO: Replace with kernel file IO
}
void syscall_write(soil_vm_t *vm) {
  if (TRACE_SYSCALLS)
    eprintf("syscall write(%ld, %lx, %ld)\n", REGA, REGB, REGC);
  // TODO: assert that this worked
  // fwrite(mem + REGB, 1, REGC, (FILE*)REGA);
  // TODO: Replace with kernel file IO
}
void syscall_close(soil_vm_t *vm) {
  if (TRACE_SYSCALLS)
    eprintf("syscall close(%ld)\n", REGA);
  // TODO: assert that this worked
  // fclose((FILE*)REGA);
  // TODO: Replace with kernel file IO
}
int global_argc;
void syscall_argc(soil_vm_t *vm) {
  if (TRACE_SYSCALLS)
    eprintf("syscall argc()\n");
  REGA = global_argc - 1;
}
char **global_argv;
void syscall_arg(soil_vm_t *vm) {
  if (TRACE_SYSCALLS)
    eprintf("syscall arg(%ld, %lx, %ld)\n", REGA, REGB, REGC);
  if (REGA < 0 || REGA >= global_argc)
    dump_and_panic(vm, "arg index out of bounds");
  char *arg = REGA == 0 ? global_argv[0] : global_argv[REGA + 1];
  int len = strlen(arg);
  int written = len > REGC ? REGC : len;
  for (int i = 0; i < written; i++)
    vm->mem[REGB + i] = arg[i];
  REGA = written;
}
void syscall_read_input(soil_vm_t *vm) {
  if (TRACE_SYSCALLS)
    eprintf("syscall read_input(%lx, %ld)\n", REGA, REGB);
  // REGA = read(0, mem + REGA, REGB);
  dump_and_panic(vm, "Input is not supported in kernel mode");
}
void syscall_execute(soil_vm_t *vm) {
  if (TRACE_SYSCALLS)
    eprintf("syscall execute(%lx, %ld)\n", REGA, REGB);
  int len = REGB;
  Byte *bin = (Byte *)kmalloc(len, GFP_KERNEL);
  if (bin == NULL)
    soil_panic(vm, 2, "out of memory");
  memcpy(bin, vm->mem + REGA, len);
  init_vm(vm, bin, len);
}
void syscall_instant_now(soil_vm_t *vm) {
  if (TRACE_SYSCALLS)
    eprintf("syscall instant_now()\n");
  REGA = 0;
}

void init_syscalls(void) {
  for (int i = 0; i < 256; i++)
    syscall_handlers[i] = syscall_none;
  syscall_handlers[0] = syscall_exit;
  syscall_handlers[1] = syscall_print;
  syscall_handlers[2] = syscall_log;
  syscall_handlers[3] = syscall_create;
  syscall_handlers[4] = syscall_open_reading;
  syscall_handlers[5] = syscall_open_writing;
  syscall_handlers[6] = syscall_read;
  syscall_handlers[7] = syscall_write;
  syscall_handlers[8] = syscall_close;
  syscall_handlers[9] = syscall_argc;
  syscall_handlers[10] = syscall_arg;
  syscall_handlers[11] = syscall_read_input;
  syscall_handlers[12] = syscall_execute;
  syscall_handlers[16] = syscall_instant_now;
}
