# Device Tree Guide  
## Raspberry Pi Zero 2 W (BCM2837)

This document records a professional-level understanding of Device Tree usage on the Raspberry Pi Zero 2 W. It focuses on the design decisions made while introducing a custom UART node and preparing the ground for an out-of-tree platform driver.

Primary references:
- Broadcom BCM2835/BCM2836/BCM2837 Peripherals Datasheet (UART chapter, register map, GPIO alternate functions)
- Linux Device Tree specification and kernel documentation
- Professional Yocto & BSP Developer Handbook – Device Tree chapters
- Linux Device Drivers, 3rd Edition (for the later driver-binding discussion)

---

## 1. Why the Device Tree Exists

The Linux kernel is deliberately kept free of board-specific details. Hardware topology, register addresses, interrupt lines, clocks and pin configurations are described externally in a Device Tree. The same kernel binary can therefore support many boards; only the Device Tree Blob changes.

At boot the bootloader loads the `.dtb`. The kernel unflattens it into a runtime tree. Platform devices are created from the nodes, and drivers match against the `compatible` property.

---

## 2. File Organisation on this Platform

Typical files involved:

- SoC-level description (included files such as `bcm283x.dtsi`, `bcm2836.dtsi`, `bcm2837.dtsi`)
- Board-level description (`bcm2837-rpi-zero-2-w.dts`)
- Custom board file created for this work (`bcm2837-rpi-zero-2-uart.dts`)

The custom file does not duplicate the upstream content. It includes the original board file and then applies targeted overrides. This is the maintainable approach.

---

## 3. System Baseline

| Item                         | Value / Notes                                      |
|-----------------------------|----------------------------------------------------|
| SoC                         | Broadcom BCM2837                                   |
| Board                       | Raspberry Pi Zero 2 W                              |
| Kernel                      | Linux 6.12 (custom configuration)                  |
| Architecture                | ARM 32-bit                                         |
| Upstream board DTB          | bcm2837-rpi-zero-2-w.dtb                           |
| Custom DTB under development| bcm2837-rpi-zero-2-uart.dtb                        |
| Root filesystem             | Custom BusyBox-based                               |
| Current console             | On-board UART via picocom                          |

A frozen “known-good” baseline (exact `.config`, DTB, `config.txt`, kernel command line and rootfs) must be recorded before experimental changes are considered permanent.

---

## 4. UART Hardware Facts (from the Datasheet)

According to the Broadcom peripherals datasheet:

- The PL011 UART is mapped at bus address 0x7E201000.
- The primary signals are TXD, RXD, nCTS and nRTS. Other modem signals are not implemented.
- GPIO alternate-function mappings relevant to this board include:
  - GPIO 32 / 33 – TXD0 / RXD0 (ALT3)
  - GPIO 30 / 31 – CTS0 / RTS0 (ALT3)
  - Other possible mappings exist on GPIO 14/15, 36/37, etc.
- The UART generates a single combined interrupt (UARTINTR) that is the logical OR of several internal sources (RX, TX, timeout, modem status, error).
- Important registers include DR (data), FR (flag), IBRD/FBRD (baud rate), LCRH (line control), CR (control), IMSC/RIS/MIS/ICR (interrupt control).

These facts determine the values that appear in the Device Tree node.

---

## 5. Upstream UART Node

The original node supplied by the board support package is essentially:

```dts
uart0: serial@7e201000 {
    compatible = "arm,pl011", "arm,primecell";
    reg = <0x7e201000 0x200>;
    interrupts = <2 25>;
    clocks = <&clocks BCM2835_CLOCK_UART>,
             <&clocks BCM2835_CLOCK_VPU>;
    clock-names = "uartclk", "apb_pclk";
    arm,primecell-periphid = <0x00241011>;
};
```

On the Zero 2 W the board file additionally selects the pin groups:

```dts
pinctrl-0 = <&uart0_gpio32 &uart0_ctsrts_gpio30>;
```

The standard kernel driver that matches `"arm,pl011"` is `drivers/tty/serial/amba-pl011.c`.

---

## 6. Custom Board Device Tree Strategy

A new top-level file was introduced that includes the upstream board DTS and then:

1. Disables the original `uart0` node so that the standard PL011 driver does not claim the hardware.
2. Instantiates a new node with a project-owned compatible string.

The custom node currently looks like this:

```dts
uart0-from-scratch {
    compatible = "pad_scratch,pl011-uart";
    reg = <0x7e201000 0x200>;
    interrupts = <2 25>;
    clocks = <&clocks BCM2835_CLOCK_UART>,
             <&clocks BCM2835_CLOCK_VPU>;
    clock-names = "uartclk", "apb_pclk";
    pinctrl-names = "default";
    pinctrl-0 = <&uart0_gpio32 &uart0_ctsrts_gpio30>;
    status = "okay";
};
```

Together with:

```dts
&uart0 {
    status = "disabled";
};
```

### Rationale for each property

- **compatible** – the sole key used by the driver matching machinery. Because a custom driver is being written, a new string under project control is used.
- **reg** – taken directly from the datasheet (base 0x7E201000). The size 0x200 matches the range used by the upstream binding.
- **interrupts** – copied from the original node; the datasheet describes the internal interrupt sources but the routing number is platform-specific.
- **clocks / clock-names** – the PL011 needs a baud-rate reference clock and an APB/bus clock. The names `"uartclk"` and `"apb_pclk"` are the conventional ones expected by PL011-style drivers.
- **pinctrl-*** – re-uses the exact pin configuration groups already defined for the Zero 2 W so that GPIO 32/33 and 30/31 are placed in the correct alternate function.
- **status** – explicitly enables the new node.

---

## 7. Compilation and Runtime Verification

The custom DTS is compiled with the kernel’s Device Tree build system (or directly with `dtc` after preprocessing). The resulting `.dtb` is placed on the boot medium.

After boot the following checks confirm correctness:

- The new node is visible under `/proc/device-tree`.
- The original `serial@7e201000` node reports `status = "disabled"`.
- A corresponding platform device appears under `/sys/bus/platform/devices` even before any custom driver is loaded.

These observations prove that the Device Tree is being consumed by the kernel and that platform devices are being instantiated from the custom node.

---

## 8. Binding Model (Link to the Driver)

The Device Tree node is only half of the picture. The other half is a platform driver whose `of_device_id` table contains the same compatible string. When the driver is loaded, the kernel calls its probe function, giving it a `struct platform_device` that already carries the resources described in the Device Tree (memory, interrupt, clocks, etc.).

The probe routine is therefore the natural place to:

- ioremap the register range,
- enable the clocks,
- request the interrupt if needed,
- and register the character device that will appear under `/dev`.

This separation of concerns—hardware description in the Device Tree, behaviour in the driver—is the central idea of the modern Linux device model.

---

## 9. Open Points

- Exact origin of earlycon parameters on this platform still needs a full source-level trace.
- The custom driver is not yet a platform driver; the current draft is only a partial character-device registration sequence.
- A formal YAML binding document for `"pad_scratch,pl011-uart"` has not yet been written or checked with `dt_binding_check`.
- A Device Tree overlay version of the node remains future work.
- Because the UART is also the serial console, development requires either a second access method (SSH) or careful staging so that the console is not lost.

---

## 10. Engineering Principles Observed

- Prefer inclusion and override over copying large upstream DTS files.
- Own the compatible string when a custom driver is written.
- Keep board description (Device Tree) cleanly separated from driver implementation.
- Treat the datasheet as the authoritative source for register addresses, pin functions and interrupt behaviour.
- Record a known-good baseline before making experimental changes permanent.
- Verify every Device Tree change on the running target, not only by decompiling the DTB.

This document will be updated as the custom UART node and its accompanying driver reach a stable, bootable state.
