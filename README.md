# Soil Kernel Module

Ever wanted to run Soil code inside your Linux kernel? Well, now you can anyway!
This repository contains a kernel module implementing a Soil VM with an IOCTL-based
interface. It also contains an example program, `usoil`, that demonstrates how
to use said interface.

## Building

You will need kernel headers in order to build the kernel module. Running `make all`
builds the kernel module. You can build the `usoil` binary with a C compiler of your
choice.

Load the module with `sudo insmod soil.ko` and run a Soil binary using `sudo ./usoil program.soil`.
Once you are done, unload the module with `sudo rmmod soil`.
