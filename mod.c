#include "vm.h"
#include <asm/ioctl.h>
#include <linux/cdev.h>
#include <linux/fs.h>
#include <linux/hashtable.h>
#include <linux/init.h>
#include <linux/ioctl.h>
#include <linux/kernel.h>
#include <linux/kthread.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/uaccess.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Clemens Tiedt");
MODULE_AUTHOR("Marcel Garus");

struct bintable_entry {
  char binary[1024];
  u64 len;
};

struct bintable_entry bintable[1024];
u64 bintable_len = 0;

soil_vm_t *vmtable[1024];
u64 vmtable_len = 0;

static int handle_open(struct inode *inode, struct file *file) { return 0; }

static int handle_release(struct inode *inode, struct file *file) { return 0; }

int validate_vm_args(struct soil_vm_run_args *args) {
  struct bintable_entry program = bintable[args->program];
  if (program.len == 0) {
    return -1;
  }
  soil_vm_t *vm = vmtable[args->vm];
  if (vm == NULL) {
    return -2;
  }
  return 0;
}

int start_soil_vm(void *data) {
  struct soil_vm_run_args *args = (struct soil_vm_run_args *)data;

  soil_vm_t *vm = vmtable[args->vm];
  struct bintable_entry program = bintable[args->program];
  init_vm(vm, (Byte *)program.binary, program.len);
  run(vm);
  return 0;
}

static long handle_ioctl(struct file *filp, unsigned int cmd,
                         unsigned long arg) {
  printk(KERN_INFO "cmd = %d, arg = %p\n", cmd, (char *)arg);
  if (cmd == SOIL_IOCTL_LOAD_BINARY) {
    struct soil_program prog;
    int res = copy_from_user(&prog, (struct soil_program *)arg,
                             sizeof(struct soil_program));
    if (res != 0) {
      printk("Failed to copy param from user\n");
      return 1;
    }
    printk(KERN_INFO "%d\n", prog.len);
    struct bintable_entry entry;
    copy_from_user(entry.binary, prog.program, prog.len);
    entry.len = prog.len;

    bintable[bintable_len] = entry;
    copy_to_user(prog.idx, &bintable_len, sizeof(bintable_len));
    bintable_len++;

    return 0;

  } else if (cmd == SOIL_IOCTL_CREATE_VM) {
    soil_vm_t *vm = kmalloc(sizeof(soil_vm_t), GFP_KERNEL);
    vmtable[vmtable_len] = vm;
    copy_to_user((soil_vm_idx *)arg, &vmtable_len, sizeof(vmtable_len));
    vmtable_len++;

    return 0;
  } else if (cmd == SOIL_IOCTL_RUN) {
    struct soil_vm_run_args args;
    int res = copy_from_user(&args, (struct soil_vm_run_args *)arg,
                             sizeof(struct soil_vm_run_args));

    if (res != 0) {
      printk("Failed to copy param from user\n");
      return 1;
    }

    res = validate_vm_args(&args);
    if (res != 0) {
      return res;
    }

    if (args.flags & SOIL_EXEC_ASYNC) {
      struct task_struct *thread = kthread_run(start_soil_vm, &args, "soil_vm");
    } else {
      start_soil_vm(&args);
    }

    return 0;
  } else if (cmd == SOIL_IOCTL_VM_STATUS) {
    struct soil_vm_status_args args;
    copy_from_user(&args, (struct soil_vm_status_args *)arg,
                   sizeof(struct soil_vm_status_args));

    soil_vm_t *vm = vmtable[args.vm];
    copy_to_user(args.status, &vm->status, sizeof(soil_vm_status_t));

    return 0;
  } else if (cmd == SOIL_IOCTL_UNLOAD_BINARY) {
    struct bintable_entry *entry = &bintable[arg];
    entry->len = 0;
    return 0;
  } else if (cmd == SOIL_IOCTL_DELETE_VM) {
    soil_vm_t *vm = vmtable[arg];
    kfree(vm->byte_code);
    if (vm->labels.len != 0) {
      kfree(vm->labels.entries);
    }
    kfree(vm);
    return 0;
  }
  return -ENOTTY;
}

struct file_operations soil_fops = {
    .open = handle_open,
    .release = handle_release,
    .unlocked_ioctl = handle_ioctl,
};

struct device *dev_file;
struct class *cls;

static int __init init_soil_km(void) {
  printk(KERN_INFO "Hello, soil!\n");
  int res = register_chrdev(IOC_MAGIC, "soil", &soil_fops);
  if (res != 0) {
    pr_alert("Failed to register character device %d\n", IOC_MAGIC);
    return -1;
  }
  cls = class_create("soil");
  dev_file = device_create(cls, NULL, MKDEV(IOC_MAGIC, 0), NULL, "soil");
  return 0;
}

static void __exit exit_soil_km(void) {
  printk(KERN_INFO "Goodbye, soil!\n");
  device_destroy(cls, MKDEV(IOC_MAGIC, 0));
  class_destroy(cls);
  unregister_chrdev(IOC_MAGIC, "soil");
}

module_init(init_soil_km);
module_exit(exit_soil_km);
