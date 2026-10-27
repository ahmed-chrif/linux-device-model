# linux-device-model

After working on EdgeOS (immutable Yocto distribution, A/B updates, SWUpdate and BSP integration on Jetson Orin Nano), I wanted to go one level deeper into how the kernel actually discovers hardware and binds drivers to it.

This repository contains notes and experimental code focused on the Linux device model:

- Device Tree as the hardware description layer
- Platform devices and driver matching
- Character device registration
- Kernel memory allocation in driver context (GFP flags and the slab allocator)

The practical work consists of writing a custom Device Tree node, a minimal YAML binding, and an out-of-tree driver draft that is intended to bind to that node. The goal is not to produce a production UART driver, but to understand the full path from Device Tree to a usable device in userspace.

This is part of a broader effort to strengthen low-level BSP and kernel skills beyond build systems and system architecture.
