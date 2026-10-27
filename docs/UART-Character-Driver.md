# UART Character Device Driver  
## Out-of-tree Driver Notes – Raspberry Pi Zero 2 W

This document records the design thinking, kernel concepts and implementation status of a custom character device driver intended to bind to the Device Tree node `uart0-from-scratch`.

Primary references:
- Linux Device Drivers, 3rd Edition (LDD3) – https://lwn.net/Kernel/LDD3/
- Linux kernel documentation (memory allocation, device model, character devices)
- sam4k – Linternals: The Slab Allocator  
  https://sam4k.com/linternals-memory-allocators-0x02/
- sam4k – Patching, Instrumenting & Debugging Linux Kernel Modules  
  https://sam4k.com/patching-instrumenting-debugging-linux-kernel-modules/

---

## 1. Purpose of a Kernel Module

A kernel module registers itself so that it can serve future requests. Its initialization function prepares the necessary facilities and then returns. In effect the module announces that it is present and ready to handle calls. The matching exit function runs just before the module is unloaded and withdraws those facilities.

A module is linked only against the kernel. The only functions it may call are those exported by the kernel; there are no user-space libraries. Consequently `printk` is used in place of `printf`. It is similar in spirit but lacks floating-point support and uses kernel log levels.

---

## 2. Kernel Space versus User Space

The kernel runs at the highest privilege level (supervisor mode). User applications run at the lowest level, where the processor restricts direct hardware access and unauthorised memory access.

User processes receive a large virtual address space and a generous stack. Kernel code, by contrast, shares a very small stack (often a single 4 KiB page) with the entire kernel call chain. Large automatic variables are therefore unsafe; memory that must persist or is sizable should be allocated dynamically.

---

## 3. Memory Allocation and GFP_KERNEL

When a driver needs memory it typically calls `kmalloc`, `kzalloc` or related helpers. These functions take a size and a set of GFP (Get Free Page) flags that describe the context and constraints of the allocation.

`GFP_KERNEL` is the most common flag for code that runs in process context and is allowed to sleep. It tells the allocator that it may:

- sleep while reclaiming memory,
- use the normal kernel memory pools,
- perform relatively expensive work if necessary.

Because the flag permits sleeping, it must not be used in atomic context (interrupt handlers, certain spin-lock regions, etc.).

Under the hood most small and medium-sized allocations are satisfied by the slab allocator (normally the SLUB implementation on modern kernels). The slab allocator sits on top of the page/buddy allocator and is optimised for frequent allocation of objects of the same size.

### Key concepts from the slab allocator

- **Caches** – collections of objects of a given size or type (`struct kmem_cache`).
- **Slabs** – contiguous pages that hold a fixed number of objects belonging to one cache.
- **Objects** – the individual allocations returned to the caller.

General-purpose caches (kmalloc-32, kmalloc-64, kmalloc-512, \ldots) are created at boot. A call to `kmalloc(size, GFP_KERNEL)` selects the smallest cache that can satisfy the request. Drivers that allocate many identical objects may also create their own dedicated caches with `kmem_cache_create` and allocate from them with `kmem_cache_alloc`.

Useful introspection interfaces include `/proc/slabinfo`, the `slabtop` utility, and the per-cache directories under `/sys/kernel/slab/`.

Understanding these mechanisms is relevant because the present driver uses `kzalloc` (and later may use managed variants) to obtain its private data structure.

---

## 4. Essential Headers and Module Metadata

Typical headers required by a character / platform driver:

- `<linux/module.h>`
- `<linux/init.h>`
- `<linux/fs.h>`, `<linux/cdev.h>`, `<linux/device.h>`
- `<linux/platform_device.h>` and `<linux/of.h>` once the driver becomes a platform driver
- `<linux/slab.h>` for `kmalloc` / `kzalloc` / `kfree`

A module should declare its license and other metadata:

```c
MODULE_LICENSE("GPL");
MODULE_AUTHOR("...");
MODULE_DESCRIPTION("...");
MODULE_VERSION("...");
```

`MODULE_DEVICE_TABLE` becomes important once the driver matches Device Tree or other device identifiers.

---

## 5. Initialization and Cleanup Pattern

The conventional form is:

```c
static int __init my_init(void)
{
    /* register facilities, allocate resources */
    return 0;
}
module_init(my_init);

static void __exit my_exit(void)
{
    /* reverse every action performed in my_init */
}
module_exit(my_exit);
```

Initialization functions are normally static. Nothing is exported to the rest of the kernel unless the programmer explicitly requests it.

---

## 6. sysfs, Netlink and Device Management

`sysfs` (normally mounted at `/sys`) exports kernel objects, device attributes and driver bindings in a hierarchical textual form. It is intended for inspection and configuration, whereas `/dev` nodes are intended for data transfer.

When hardware appears or disappears the kernel emits `uevent` messages over a Netlink socket. A user-space device manager (`udev`, `mdev`, \ldots) listens for those events, loads modules if necessary, and creates the corresponding device nodes.

In a minimal system that lacks a device manager, modules must be loaded manually (`insmod` / `modprobe`) and device nodes created by hand (`mknod` or `device_create` from inside the driver).

---

## 7. Driver Architecture for this Project

Source location:

```
~/bsp/my-uart-driver/uart_driver.c
```

Intended responsibilities:

- Act as a platform driver that matches the Device Tree compatible string `"pad_scratch,pl011-uart"`.
- In the probe path, allocate private data (currently via `kzalloc` with `GFP_KERNEL`), obtain resources described by the Device Tree, and register a character device.
- Create a device node (planned name `/dev/cuart`).
- Implement the classic file operations: open, release, read, write (ioctl later if required).
- Perform orderly cleanup in the remove path and in the module exit function.

---

## 8. Current Implementation Status

The present source is an early draft that only sketches character-device registration. It is not yet a platform driver and contains several structural problems:

- Incomplete or incorrect type definitions.
- Missing global variables and necessary headers.
- Broken control flow and error-path ordering inside the initialization function.
- Inconsistent names between initialization and cleanup.

These issues must be corrected before the module can be loaded or bound to the Device Tree node.

---

## 9. Planned Evolution

1. Convert the module into a proper `platform_driver` with an `of_device_id` table.
2. Move resource acquisition and character-device registration into the probe callback.
3. Prefer managed resource helpers (`devm_*`) where appropriate.
4. Implement the file-operation methods.
5. Verify that a platform device appears under `/sys/bus/platform/devices` before the driver is loaded, and that probe is invoked after the module is inserted.
6. Document the final user-visible interface.

---

## 10. Design Constraints Specific to this Platform

The UART being re-implemented is the same controller historically used as the serial console. Replacing it with a pure loadable module therefore creates a chicken-and-egg problem at boot. Development therefore relies on an alternative access method (SSH) or on keeping the original console UART alive until the new driver is stable.

---

## 11. Summary of Concepts Applied

- Modules register facilities; they do not run continuously after initialization.
- Kernel code executes with a small shared stack; dynamic allocation is preferred for larger objects.
- `GFP_KERNEL` is the ordinary context for allocations that may sleep; it is backed by the slab allocator (SLUB) for common sizes.
- The slab allocator organises memory into caches, slabs and objects and sits on top of the page/buddy allocator.
- `sysfs` exports configuration and topology; `/dev` exports data paths.
- Netlink carries asynchronous kernel-to-user events that a device manager turns into module loads and device nodes.
- A well-structured driver separates Device Tree matching, resource management and the character-device interface.

This document will be updated as the driver progresses from the current draft to a correct platform character driver.
