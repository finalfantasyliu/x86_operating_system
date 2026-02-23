# 01 - Bare Metal Boot Setup

A comprehensive study of x86 boot sector programming: from power-on to
protected mode, covering the BIOS boot process, real mode assembly, GDT
configuration, and the transition to 32-bit protected mode.

---

## Table of Contents

1. [Overview](#1-overview)
2. [Project Structure](#2-project-structure)
3. [Build System](#3-build-system)
   - 3.1 [CMake vs Makefile](#31-cmake-vs-makefile)
   - 3.2 [Toolchain File](#32-toolchain-file)
   - 3.3 [Linker Script](#33-linker-script)
4. [x86 Boot Process](#4-x86-boot-process)
   - 4.1 [Power-On and CPU Reset](#41-power-on-and-cpu-reset)
   - 4.2 [Real Mode Memory Map](#42-real-mode-memory-map)
   - 4.3 [Boot Signature (0x55AA)](#43-boot-signature-0x55aa)
   - 4.4 [Disk Image (disk.img)](#44-disk-image-diskimg)
5. [Bootloader: From Real Mode to Protected Mode](#5-bootloader-from-real-mode-to-protected-mode)
   - 5.1 [Overview of the Six Steps](#51-overview-of-the-six-steps)
   - 5.2 [Step 1: Disable Interrupts (cli)](#52-step-1-disable-interrupts-cli)
   - 5.3 [Step 2: Setup Segment Registers](#53-step-2-setup-segment-registers)
   - 5.4 [Step 3: Enable A20 Line](#54-step-3-enable-a20-line)
   - 5.5 [Step 4: Setup GDT (Global Descriptor Table)](#55-step-4-setup-gdt-global-descriptor-table)
   - 5.6 [Step 5: Switch to Protected Mode](#56-step-5-switch-to-protected-mode)
   - 5.7 [Step 6: Enter 32-bit and Jump to Kernel](#57-step-6-enter-32-bit-and-jump-to-kernel)
   - 5.8 [Complete Bootloader Source Code (Annotated)](#58-complete-bootloader-source-code-annotated)
   - 5.9 [Cross-Project Comparison](#59-cross-project-comparison)
6. [x86 Assembly Reference](#6-x86-assembly-reference)
   - 6.1 [Registers](#61-registers)
   - 6.2 [FLAGS Register](#62-flags-register)
   - 6.3 [Data Movement](#63-data-movement)
   - 6.4 [Arithmetic and Logic](#64-arithmetic-and-logic)
   - 6.5 [Jumps and Branches](#65-jumps-and-branches)
   - 6.6 [I/O Instructions](#66-io-instructions)
   - 6.7 [Interrupt Instructions](#67-interrupt-instructions)
   - 6.8 [String Instructions](#68-string-instructions)
   - 6.9 [Special Instructions](#69-special-instructions)
   - 6.10 [Data Definition (NASM)](#610-data-definition-nasm)
   - 6.11 [GNU as Directives](#611-gnu-as-directives)
   - 6.12 [.S vs .s File Extensions](#612-s-vs-s-file-extensions)
   - 6.13 [Entry Point (_start)](#613-entry-point-_start)
   - 6.14 [Section Naming Convention](#614-section-naming-convention)
7. [ELF vs Flat Binary](#7-elf-vs-flat-binary)
   - 7.1 [Why objcopy is Needed](#71-why-objcopy-is-needed)
   - 7.2 [Why -Ttext=0x7C00 / Linker Script](#72-why--ttext0x7c00--linker-script)
8. [The dd Command](#8-the-dd-command)
   - 8.1 [Creating a Blank Disk Image](#81-creating-a-blank-disk-image)
   - 8.2 [Writing the Boot Sector](#82-writing-the-boot-sector)
   - 8.3 [Parameter Reference](#83-parameter-reference)
9. [QEMU Debugging](#9-qemu-debugging)
   - 9.1 [QEMU Command Breakdown](#91-qemu-command-breakdown)
   - 9.2 [VSCode Integration](#92-vscode-integration)
10. [Memory Protection](#10-memory-protection)
    - 10.1 [Real Mode (No Protection)](#101-real-mode-no-protection)
    - 10.2 [Protected Mode (Segmentation + Paging)](#102-protected-mode-segmentation--paging)
    - 10.3 [How W^X Works in Modern OS](#103-how-wx-works-in-modern-os)
    - 10.4 [Test: Putting Code in the Wrong Section](#104-test-putting-code-in-the-wrong-section)
11. [Useful Tools](#11-useful-tools)
12. [Notable OS Projects](#12-notable-os-projects)
13. [References](#13-references)

---

## 1. Overview

This is the first lesson: a minimal bare-metal x86 boot sector. The entire
program is a single `jmp .` instruction (an infinite loop) with a boot
signature at the end. The CPU starts in 16-bit real mode with no OS, no
memory protection, and no standard library.

What "bare-metal" means:

- No operating system running
- No standard library (libc) available
- No memory protection (MMU not enabled)
- Direct hardware access
- In CMake, this is declared as `set(CMAKE_SYSTEM_NAME Generic)`

This document serves as a comprehensive reference covering:

1. **The build system** -- how CMake replaces the original Makefile for
   cross-compilation targeting bare-metal x86.
2. **The boot process** -- what happens from the moment power is applied
   until the CPU executes your first instruction at `0x7C00`.
3. **The bootloader** -- the six canonical steps to transition from 16-bit
   real mode to 32-bit protected mode, with annotated source code and
   cross-references to real-world OS projects (xv6, Linux, ToaruOS).
4. **Assembly reference** -- a complete quick-reference for x86 real mode
   instructions, registers, and assembler directives.
5. **Tooling** -- QEMU debugging, binary inspection, and the `dd` command.

---

## 2. Project Structure

```
01_bare_metal_boot_setup/
├── CMakeLists.txt              # Build configuration (replaced Makefile)
├── os.lds                      # Linker script (memory layout)
├── Makefile                    # Original Makefile (kept for reference)
├── cmake/
│   └── x86_64-elf-gcc.cmake   # Cross-compilation toolchain file
├── source/
│   ├── start.S                 # 16-bit real mode entry point
│   ├── os.c                    # 32-bit C code (empty for now)
│   └── os.h                    # Shared header (.S and .c both include this)
├── script/
│   ├── qemu-debug-osx.sh       # QEMU launch script (macOS)
│   ├── qemu-debug-linux.sh     # QEMU launch script (Linux)
│   └── qemu-debug-win.bat      # QEMU launch script (Windows)
├── .vscode/
│   ├── launch.json             # GDB debug configuration
│   └── tasks.json              # Build + QEMU task chain
└── build/                      # CMake build output directory
    ├── os.elf                  # ELF executable (for GDB debugging)
    ├── os.bin                  # Flat binary (for BIOS to load)
    ├── os_dis.txt              # Disassembly output
    ├── os_elf.txt              # ELF section/segment info
    └── disk.img                # Virtual disk image (10MB)
```

---

## 3. Build System

### 3.1 CMake vs Makefile

The original course uses a Makefile. This project replaces it with CMake for
better structure and cross-platform support.

Key differences in the CMake setup:

- **`target_sources()`** instead of `set(SOURCES ...)` -- modern CMake style,
  all properties bound directly to the target.
- **Linker script** (`os.lds`) instead of `-Ttext=0x7c00` -- more control over
  memory layout.
- **`LINKER:` prefix** in `target_link_options()` -- required because CMake
  passes linker flags through the compiler driver (gcc), and `-m elf_i386`
  is an `ld` flag, not a `gcc` flag. `"LINKER:-m,elf_i386"` tells CMake to
  translate it to `-Wl,-m,elf_i386`.

> **[Ref 1]** CMake docs -- `target_link_options`, "Handling Compiler Driver Differences":
>
> *"The `LINKER:` prefix and `,` separator can be used to specify, in a portable way,
> options to pass to the linker tool. `LINKER:` is replaced by the appropriate driver
> option and `,` by the appropriate driver separator."*
>
> *"For example, `"LINKER:-z,defs"` becomes `-Xlinker -z -Xlinker defs` for `Clang`
> and `-Wl,-z,defs` for `GNU GCC`."*
>
> -- https://cmake.org/cmake/help/latest/command/target_link_options.html

- **`add_custom_command(OUTPUT ...)`** for disk.img -- CMake only creates the
  blank image when it doesn't exist, avoiding overwrite on rebuilds.

### 3.2 Toolchain File

`cmake/x86_64-elf-gcc.cmake` tells CMake we're cross-compiling for bare-metal:

```cmake
set(CMAKE_SYSTEM_NAME Generic)     # No OS (bare-metal)
set(CMAKE_SYSTEM_PROCESSOR x86)

set(CMAKE_C_COMPILER   x86_64-elf-gcc)
set(CMAKE_ASM_COMPILER x86_64-elf-gcc)
```

`CMAKE_SYSTEM_NAME = Generic` is CMake's way of saying "the target has no OS."
This skips standard library detection and OS-specific compiler checks.

> **[Ref 2]** CMake docs -- `CMAKE_SYSTEM_NAME`:
>
> *"`Generic` -- Some platforms, e.g. bare metal embedded devices"*
>
> -- https://cmake.org/cmake/help/latest/variable/CMAKE_SYSTEM_NAME.html

The original Makefile achieves the same effect through compiler flags:

```makefile
CFLAGS = -g -c -O0 -m32 -fno-pie -fno-stack-protector -nostdlib -nostdinc
```

| Flag | Meaning |
|------|---------|
| `-nostdlib` | Don't link standard library (no OS provides libc) |
| `-nostdinc` | Don't search standard header paths (no stdio.h) |
| `-fno-pie` | Don't generate position-independent code (no dynamic loader) |
| `-fno-stack-protector` | No stack canary (no `__stack_chk_fail`) |
| `-m32` | Generate 32-bit code |

### 3.3 Linker Script

`os.lds` defines the memory layout. This replaces the `-Ttext=0x7c00` flag
with explicit control:

```
ENTRY(_start)          <-- Entry point symbol

SECTIONS {
    . = 0x7C00;        <-- Starting address (where BIOS loads us)

    .text : { *(.text) }      <-- Collect all .text sections from .o files
    .rodata : { *(.rodata) }  <-- Read-only data
    .data : { *(.data) }      <-- Initialized data
    .bss : { *(.bss) }        <-- Uninitialized data (zero-filled)
}
```

The `*(.text)` syntax is **string matching** -- the linker collects all sections
named `.text` from all input `.o` files. There is no magic: `.text` is just a
conventional name, not a keyword. You could name a section `.banana` and use
`*(.banana)` in the linker script.

> **[Ref 3]** GNU ld manual -- "Simple Linker Script Example":
>
> *"The expression `*(.text)` means all `.text` input sections in all input files."*
> *"The `*` is a wildcard which matches any file name."*
>
> -- https://sourceware.org/binutils/docs/ld/Simple-Example.html

Advantages over `-Ttext=0x7c00`:

- Control section ordering and alignment
- Define boundary symbols (e.g. `_bss_start`, `_bss_end`)
- Discard unnecessary sections (`/DISCARD/`)
- Add boot signature at a precise offset
- Guard against exceeding 512-byte boot sector limit

---

## 4. x86 Boot Process

### 4.1 Power-On and CPU Reset

When power is applied, the x86 CPU performs a hardware reset and begins
executing in 16-bit real mode. The initial state is defined by the processor
architecture:

```
Power On
  |
  +-- CPU resets, CS:IP = 0xF000:0xFFF0 --> physical address 0xFFFF0
  |                        (16-bit Real Mode)
  |
  +-- Jump to BIOS (ROM, mapped to high memory)
  |
  +-- POST (Power-On Self-Test)
  |     +-- Check CPU, RAM, keyboard, display adapter...
  |     +-- Build IVT (Interrupt Vector Table) at 0x00000
  |     +-- Initialize hardware, provide INT services
  |
  +-- Search for boot device
  |     +-- Read sector 0 (first 512 bytes) from the boot disk
  |     +-- Check bytes at offset 510-511 for magic number 0x55AA
  |     |     +-- Not found --> "No bootable device"
  |     |     +-- Found --> continue
  |     +-- Copy 512 bytes to memory address 0x7C00
  |     +-- Jump to 0x7C00
  |
  +-- Bootloader executes (your code starts here)
  |     +-- Stage 1: Setup environment, load Stage 2
  |     +-- Stage 2: Load kernel, switch to Protected Mode
  |
  +-- Operating system kernel begins running
```

> **[Ref 4]** Intel SDM, Vol. 3A, Section 9.1.4 -- "First Instruction Executed":
>
> *"The first instruction that is fetched and executed following a hardware
> reset is located at physical address FFFFFFF0H. This address is 16 bytes
> below the processor's uppermost physical address. The EPROM containing the
> software-initialization code must be located at this address."*
>
> In legacy BIOS mode on 16-bit-compatible systems, this effectively maps to
> `0xF000:0xFFF0` (segment 0xF000, offset 0xFFF0) = physical `0xFFFF0`,
> which is 16 bytes below the 1 MB boundary.
>
> -- Intel 64 and IA-32 Architectures Software Developer's Manual, Vol. 3A,
>    Order Number 253668

BIOS does NOT understand ELF, sections, or any file format. It just copies
raw bytes from disk and jumps to them. That's why we need a flat binary
(`os.bin`), not an ELF.

### 4.2 Real Mode Memory Map

The first 1 MB of memory has a fixed layout defined by the IBM PC
architecture. This layout dates back to the original IBM PC (1981) and is
maintained for backward compatibility:

```
0x00000 +----------------+
        | IVT            |  1 KB (Interrupt Vector Table, 256 entries x 4 bytes)
0x00400 +----------------+
        | BIOS Data Area |  256 bytes
0x00500 +----------------+
        |                |
        | Free Space     |  ~29 KB (usable by bootloader)
        |                |
0x07C00 +================+
        | Bootloader     |  512 bytes (BIOS loads sector 0 here)
0x07E00 +================+
        |                |
        | Free Space     |  ~600 KB (usable for kernel, data, etc.)
        |                |
0xA0000 +----------------+
        | Video RAM      |  128 KB (framebuffer for display)
0xC0000 +----------------+
        | Expansion ROM  |  64 KB (video card BIOS, etc.)
0xF0000 +----------------+
        | BIOS ROM       |  64 KB
0xFFFFF +----------------+  <-- 1 MB boundary (top of real mode addressable memory)
```

The address `0x7C00` was chosen by David Bradley of IBM in 1981. It provides
32 KB of space below for the stack (growing downward from `0x7C00`) and leaves
over 600 KB above for loading the kernel.

> **[Ref 5]** OSDev Wiki -- "Memory Map (x86)":
>
> *"The traditional BIOS boot process loads the first sector (512 bytes) of
> the boot device to address 0x7C00. The choice of 0x7C00 was made by the
> IBM PC 5150 BIOS developer team. It was the highest address that would
> leave enough room for the stack below and still leave 512 bytes for the
> boot sector itself, while also leaving room above for the OS to load."*
>
> -- https://wiki.osdev.org/Memory_Map_(x86)

### 4.3 Boot Signature (0x55AA)

BIOS requires bytes at offset 510-511 of sector 0 to be `0x55 0xAA`.
Without this, the disk is considered not bootable.

In `start.S`, this is implemented with:

```asm
.org 0x1FE, 0x90    /* Advance to offset 510, fill gap with NOP (0x90) */
.byte 0x55, 0xAA    /* Boot signature */
```

- `.org` = **origin** -- advance current position to the specified offset
  (relative to the start of the current section, not absolute address).
- `.org 0x1FE, 0x90` -- jump to offset 510, fill everything in between with
  `0x90` (NOP). Default fill is `0x00` if not specified.
- If code exceeds 510 bytes, the assembler errors out -- this also serves as
  a guard against overflowing the 512-byte limit.

> **[Ref 6]** GNU as manual -- `.org`:
>
> *"Advance the location counter of the current section to new-lc."*
>
> *"`.org` may only increase the location counter, or leave it unchanged;
> you cannot use `.org` to move the location counter backwards."*
>
> *"Beware that the origin is relative to the start of the section, not to
> the start of the subsection."*
>
> *"When the location counter (of the current subsection) is advanced, the
> intervening bytes are filled with fill which should be an absolute expression.
> If the comma and fill are omitted, fill defaults to zero."*
>
> -- https://sourceware.org/binutils/docs/as/Org.html

**Why 0x55 and 0xAA?** These byte values have an alternating bit pattern
(`0x55` = `01010101`, `0xAA` = `10101010`). This pattern was chosen because
it can help detect certain hardware faults -- if any data line is stuck high
or low, the alternating pattern will fail the check.

In NASM syntax (Intel style), the equivalent is:

```asm
times 510 - ($ - $$) db 0     ; Fill to offset 510 with zeros
dw 0xAA55                     ; Boot signature (little-endian: 0x55 at 510, 0xAA at 511)
```

Note: NASM uses little-endian byte ordering for `dw`, so `dw 0xAA55` places
`0x55` at offset 510 and `0xAA` at offset 511, which is correct.

Resulting boot sector layout:

```
hexyl -n 512 build/disk.img

offset 0x000: eb fe 90 90 90 90 ...   <-- jmp . (boot code) + NOP fill
offset 0x002: 90 90 90 90 90 90 ...   <-- NOP fill (.org 0x1FE, 0x90)
              ...
offset 0x1FE: 55 aa                   <-- boot signature
```

The same can be achieved externally (but not recommended):

```bash
printf '\x55\xAA' | dd of=disk.img bs=1 seek=510 conv=notrunc
```

Note: use `printf`, not `print`. In zsh, `print` appends a newline (`0x0A`),
resulting in 3 bytes instead of 2.

### 4.4 Disk Image (disk.img)

The disk image is a raw file filled with zeros, simulating a hard drive:

```bash
dd if=/dev/zero of=disk.img bs=512 count=20160
```

- Size: 512 * 20160 = 10,321,920 bytes (~10MB)
- Geometry: 20 cylinders x 16 heads x 63 sectors/track (standard CHS)
- The sector count must be a multiple of C*H*S, otherwise some BIOS/emulators
  complain about invalid disk geometry.

In CMake, this is generated automatically via `add_custom_command(OUTPUT ...)`,
which only runs when `disk.img` doesn't exist.

---

## 5. Bootloader: From Real Mode to Protected Mode

A bootloader is the first program that runs after BIOS hands off control.
Its job is to prepare the CPU environment and transition from the primitive
16-bit real mode to 32-bit protected mode, where the operating system kernel
can run with full memory protection and a flat address space.

This section walks through the six canonical steps that virtually every x86
bootloader performs, with annotated code and cross-references to real OS
projects.

### 5.1 Overview of the Six Steps

```
+------------------------------------------------------+
| Step 1: Disable Interrupts                           |
|   cli                                                |
|   Prevent hardware interrupts from corrupting the    |
|   mode-switching process                             |
+------------------------------------------------------+
| Step 2: Setup Segment Registers                      |
|   xor ax, ax -> mov ds/es/ss, ax -> mov sp, 0x7C00  |
|   Zero out all segments for simple address math      |
+------------------------------------------------------+
| Step 3: Enable A20 Line                              |
|   Via keyboard controller (8042) I/O ports           |
|   Unlock memory addresses above 1 MB                 |
+------------------------------------------------------+
| Step 4: Setup GDT (Global Descriptor Table)          |
|   lgdt [gdt_descriptor]                              |
|   Tell the CPU where the segment descriptor table is |
+------------------------------------------------------+
| Step 5: Switch to Protected Mode                     |
|   Set CR0.PE = 1, then Far Jump to flush pipeline    |
+------------------------------------------------------+
| Step 6: Enter 32-bit, reload segments, jump to kernel|
|   Use Protected Mode selectors for DS/ES/SS          |
|   Setup stack, call the C kernel entry point         |
+------------------------------------------------------+
```

### 5.2 Step 1: Disable Interrupts (cli)

```asm
start:
    cli                       ; Clear Interrupt Flag
                              ; FLAGS register IF bit = 0
                              ; CPU ignores hardware interrupts (INTR pin)
                              ; Machine code: 0xFA (1 byte)
```

**Why:** The following steps modify segment registers, load the GDT, and
switch CPU modes. If a hardware interrupt fires in the middle of these
operations, the CPU would try to use a half-configured interrupt handler
with incorrect segment registers -- causing a triple fault (crash).

Interrupts are re-enabled later with `sti` (Set Interrupt Flag) after the
protected mode environment is fully configured.

> **[Ref 7]** Intel SDM, Vol. 2B -- `CLI` instruction:
>
> *"If the current privilege level is at or below the IOPL, the IF flag is
> cleared to 0; other flags in the EFLAGS register are unaffected. If the
> current privilege level is greater than the IOPL, a general-protection
> exception (#GP) is generated."*
>
> -- Intel 64 and IA-32 Architectures Software Developer's Manual, Vol. 2B,
>    Order Number 253667

### 5.3 Step 2: Setup Segment Registers

```asm
    xor ax, ax                ; ax = ax XOR ax = 0
                              ;
                              ; XOR truth table:
                              ;   0 XOR 0 = 0
                              ;   1 XOR 1 = 0
                              ; Any value XORed with itself yields 0
                              ;
                              ; Why not "mov ax, 0"?
                              ;   xor ax, ax = 2 bytes (opcode: 31 C0)
                              ;   mov ax, 0  = 3 bytes (opcode: B8 00 00)
                              ; Saves 1 byte -- matters in a 512-byte boot sector

    mov ds, ax                ; DS = 0 (Data Segment)
                              ; Address calculation: physical = 0*16 + offset = offset
                              ;
                              ; Why not "mov ds, 0"?
                              ; x86 does not allow loading an immediate value
                              ; directly into a segment register. Must go through
                              ; a general-purpose register first.

    mov es, ax                ; ES = 0 (Extra Segment)
    mov ss, ax                ; SS = 0 (Stack Segment)

    mov sp, 0x7C00            ; SP = 0x7C00 (Stack Pointer)
                              ; Stack grows downward from 0x7C00
                              ;
                              ; Memory layout:
                              ;   0x00500 +----------+
                              ;           | v Stack  | SP grows down toward here
                              ;   0x07C00 +----------+ <-- SP start
                              ;           |Bootloader| Code is here
                              ;   0x07E00 +----------+
```

**Why zero out segments?** When BIOS jumps to `0x7C00`, the segment register
values are undefined (BIOS-dependent). Some BIOSes set `CS:IP = 0x0000:0x7C00`,
others use `CS:IP = 0x07C0:0x0000` -- both map to the same physical address,
but `DS` might be anything. By zeroing all segment registers, we ensure that
`physical address = segment * 16 + offset = 0 + offset = offset`, making
address calculations trivial.

> **[Ref 8]** Intel SDM, Vol. 3A, Section 20.1 -- "Real-Address Mode Operation":
>
> *"In real-address mode, the processor does not interpret the segment
> selectors as indexes into a descriptor table. Instead, a segment selector
> is used directly to compute a linear address by shifting it left 4 bits
> and adding the effective address."*
>
> -- Intel 64 and IA-32 Architectures Software Developer's Manual, Vol. 3A

### 5.4 Step 3: Enable A20 Line

The **A20 line** (Address line 20) is the 21st address line on the x86 bus.
By default, it is disabled for backward compatibility with the Intel 8086,
which could only address 1 MB of memory (20 address lines, A0-A19).

**Historical context:** The original 8086 had a quirk -- programs could
"wrap around" past the 1 MB boundary, and some software depended on this
behavior. When the 80286 added a 21st address line (A20), IBM disabled it
by default so that legacy 8086 software would still work. The disable
mechanism was wired through the keyboard controller (Intel 8042 chip)
because it happened to have a spare pin available on the motherboard.

```asm
enable_a20:

.wait1:
    in al, 0x64               ; Read keyboard controller (8042) status from port 0x64
                              ;
                              ; "in" = Input from I/O port (not memory read)
                              ; Memory read: Address Bus sends address, M/IO# = 1
                              ; I/O read:    Address Bus sends port number, M/IO# = 0
                              ;
                              ; Status register bits:
                              ;   bit 0 = Output Buffer has data to read
                              ;   bit 1 = Input Buffer is full (controller busy)

    test al, 0x02             ; Check if bit 1 is set (controller busy?)
                              ;
                              ; "test" performs AND but discards result,
                              ; only updates FLAGS:
                              ;   al      = ???? ??X?   (X = bit 1)
                              ;   0x02    = 0000 0010
                              ;   AND     = 0000 00X0
                              ;
                              ;   X = 1 --> result nonzero --> ZF = 0 (busy)
                              ;   X = 0 --> result zero    --> ZF = 1 (ready)

    jnz .wait1                ; If ZF = 0 (busy), loop back and wait
                              ; If ZF = 1 (ready), fall through

    mov al, 0xD1              ; Command 0xD1: "Write to Output Port"
    out 0x64, al              ; Send command to controller command port

.wait2:
    in al, 0x64               ; Wait for controller to be ready again
    test al, 0x02
    jnz .wait2

    mov al, 0xDF              ; 0xDF = 1101 1111 (bit 1 = 1 --> A20 enabled)
    out 0x60, al              ; Write to data port (0x60)
                              ; This value is written to 8042's Output Port
                              ; Bit 1 controls the A20 gate
```

**Alternative methods for enabling A20:**

| Method | Mechanism | Portability | Lines of code |
|--------|-----------|-------------|---------------|
| Keyboard controller (8042) | I/O ports 0x64/0x60 | Most portable, works everywhere | ~12 |
| Fast A20 (Port 0x92) | System Control Port A | Not all hardware supports it | 3 |
| BIOS INT 0x15, AX=0x2401 | BIOS service call | Depends on BIOS implementation | 2 |

The Linux kernel tries all three methods in sequence (see `arch/x86/boot/a20.c`).

> **[Ref 9]** OSDev Wiki -- "A20 Line":
>
> *"The A20 Address Line is the physical representation of the 21st bit
> (number 20, counting from 0) of any memory access. When the IBM AT was
> introduced, the 80286 could address up to 16 MB of system memory. However,
> the CPU was supposed to emulate the 8088's memory map, which only had 20
> address lines. So the 21st address line was simply masked to zero by
> default using a gate. The gate was connected to the Intel 8042 keyboard
> controller because it happened to have a spare pin."*
>
> -- https://wiki.osdev.org/A20_Line

### 5.5 Step 4: Setup GDT (Global Descriptor Table)

In protected mode, segment registers no longer hold direct memory addresses.
Instead, they hold **selectors** -- indices into the GDT, which is a table of
**segment descriptors** that define base address, size limit, and access
permissions for each memory segment.

The GDT must be set up before switching to protected mode. It requires at
least three entries:

| Entry | Index | Offset | Purpose |
|-------|-------|--------|---------|
| 0 | Null Descriptor | 0x00 | Required by CPU, must be all zeros |
| 1 | Code Segment | 0x08 | Executable, readable (Ring 0) |
| 2 | Data Segment | 0x10 | Writable, not executable (Ring 0) |

Each descriptor is 8 bytes with the following structure:

```
Byte:   7        6        5        4        3        2        1        0
     +--------+--------+--------+--------+--------+--------+--------+--------+
     |Base    |Flags   |Access  |Base    |Base bits 0-15   |Limit bits 0-15  |
     |31-24   |+Lim    |Byte    |23-16   |                 |                 |
     |        |19-16   |        |        |                 |                 |
     +--------+--------+--------+--------+--------+--------+--------+--------+
```

**Why are Base and Limit split across non-contiguous bytes?** Historical
backward compatibility. The 80286 descriptor was 6 bytes. When the 80386
expanded it to 8 bytes, the new bits had to be appended at the end to
maintain binary compatibility with 80286 descriptor tables.

The **Access Byte** (byte 5) controls permissions:

```
  bit 7   = P  (Present)        -- 1 = segment is in memory
  bit 6-5 = DPL                 -- Privilege level (0 = kernel, 3 = user)
  bit 4   = S  (Descriptor Type)-- 1 = code/data, 0 = system (TSS, etc.)
  bit 3   = E  (Executable)     -- 1 = code segment, 0 = data segment
  bit 2   = DC (Direction/Conforming)
  bit 1   = RW (Read/Write)     -- Code: readable? Data: writable?
  bit 0   = A  (Accessed)       -- Set by CPU when segment is accessed
```

The **Flags** nibble (upper 4 bits of byte 6):

```
  bit 7 = G (Granularity)       -- 1 = limit in 4KB units, 0 = byte units
  bit 6 = D/B (Default Size)    -- 1 = 32-bit segment, 0 = 16-bit
  bit 5 = L (Long Mode)         -- 1 = 64-bit (only in long mode)
  bit 4 = Reserved              -- Must be 0
```

For a flat memory model (base = 0, limit = 4 GB):

- Limit = `0xFFFFF` (20 bits), Granularity = 1 (4 KB units)
- `0xFFFFF * 4KB = 4 GB` -- the segment covers the entire address space
- Base = `0x00000000` -- no segment offset, physical = linear = offset

```asm
    lgdt [gdt_descriptor]     ; Load GDT Register
                              ; Loads 6 bytes from gdt_descriptor into GDTR:
                              ;   bytes 0-1: GDT size - 1 (16-bit limit)
                              ;   bytes 2-5: GDT base address (32-bit)
```

> **[Ref 10]** Intel SDM, Vol. 3A, Section 3.5.1 -- "Segment Descriptor Tables":
>
> *"The GDT is not a segment itself; instead, it is a data structure in
> linear address space. The base linear address and limit of the GDT must
> be loaded into the GDTR register. [...] The base address should be aligned
> on an eight-byte boundary to yield the best processor performance."*
>
> *"The first descriptor in the GDT is not used by the processor. A segment
> selector to this 'null descriptor' does not generate an exception when
> loaded into a data-segment register (DS, ES, FS, or GS), but it always
> generates a general-protection exception (#GP) when an attempt is made to
> access memory using the descriptor."*
>
> -- Intel 64 and IA-32 Architectures Software Developer's Manual, Vol. 3A

### 5.6 Step 5: Switch to Protected Mode

The actual mode switch is performed by setting the PE (Protection Enable) bit
in the CR0 control register:

```asm
    mov eax, cr0              ; Read CR0 into eax
                              ; CR0 is a 32-bit special register
                              ; Cannot be modified directly, must read-modify-write

    or eax, 0x1               ; Set bit 0 (PE) to 1, leave all other bits unchanged
                              ;
                              ; OR logic:
                              ;   original OR 0 = original (unchanged)
                              ;   original OR 1 = 1 (forced to 1)
                              ;
                              ;   eax:   xxxx xxxx ... xxxx xxx0
                              ;   0x1:   0000 0000 ... 0000 0001
                              ;   result:xxxx xxxx ... xxxx xxx1
                              ;                              ^ PE = 1

    mov cr0, eax              ; Write back to CR0
                              ; From this moment, the CPU is in Protected Mode!
                              ;
                              ; But we're not done yet:
                              ;   1. CS still holds the old Real Mode value
                              ;   2. The instruction pipeline may still contain
                              ;      decoded Real Mode instructions
                              ;   Solution: Far Jump

    jmp CODE_SEG:start_pm     ; Far Jump (long jump)
                              ;
                              ; CODE_SEG = 0x08 = GDT entry 1 (Code Segment selector)
                              ;
                              ; Why 0x08?
                              ;   GDT entry 0 at offset 0x00 (Null)
                              ;   GDT entry 1 at offset 0x08 (Code)  <-- 8 bytes per entry
                              ;   GDT entry 2 at offset 0x10 (Data)
                              ;
                              ; This jump does three things:
                              ;   1. Set CS = 0x08 (Protected Mode Code Selector)
                              ;   2. Set IP = address of start_pm
                              ;   3. Flush the CPU instruction pipeline
                              ;
                              ; A regular "jmp" only changes IP.
                              ; A Far Jump changes both CS and IP.
```

> **[Ref 11]** Intel SDM, Vol. 3A, Section 9.9.1 -- "Switching to Protected Mode":
>
> *"Before switching to protected mode, a minimum set of system data
> structures and code modules must be loaded into memory. [...] The steps
> for switching from real-address mode to protected mode are:
> 1. Disable interrupts. [...]
> 2. Execute the LGDT instruction to load the GDTR register [...]
> 3. Execute a MOV CR0 instruction that sets the PE flag (and optionally
>    the PG flag) in control register CR0.
> 4. Immediately following the MOV CR0 instruction, execute a far JMP or
>    far CALL instruction. [...] This operation is typically a far jump or
>    call to the next instruction in the instruction stream."*
>
> -- Intel 64 and IA-32 Architectures Software Developer's Manual, Vol. 3A

### 5.7 Step 6: Enter 32-bit and Jump to Kernel

After the far jump, the CPU is in 32-bit protected mode. All data segment
registers must be reloaded with protected mode selectors:

```asm
[bits 32]                     ; Tell assembler: generate 32-bit machine code from here

start_pm:
    mov ax, DATA_SEG          ; ax = 0x10 = GDT entry 2 (Data Segment selector)
                              ;
                              ; In Protected Mode, segment registers hold selectors
                              ; (byte offsets into the GDT), not direct addresses.

    mov ds, ax                ; DS = 0x10 --> GDT Data Segment
    mov ss, ax                ; SS = 0x10 --> Stack also uses Data Segment
    mov es, ax                ; ES = 0x10
    mov fs, ax                ; FS = 0x10
    mov gs, ax                ; GS = 0x10
                              ; All data segment registers point to the same descriptor:
                              ; base = 0, limit = 4GB
                              ; --> Flat Memory Model
                              ;     physical address = 0 + offset = offset
                              ;     The entire 4GB space is directly accessible

    mov ebp, 0x90000          ; EBP = 0x90000 (stack base)
    mov esp, 0x90000          ; ESP = 0x90000 (stack top)
                              ; Stack grows downward from 0x90000
                              ; This address is high in the free memory area,
                              ; far from the kernel code

    ;=== From here, C functions can be called ===

    call main                 ; Call the kernel entry point (written in C)

    jmp $                     ; If main() unexpectedly returns, halt here
                              ; $ = current address
                              ; jmp $ = jump to self = infinite loop
```

### 5.8 Complete Bootloader Source Code (Annotated)

The following is a complete, self-contained bootloader in NASM syntax. It
implements all six steps above and can be assembled and tested with:

```bash
nasm -f bin boot.asm -o boot.bin
qemu-system-i386 boot.bin
```

```asm
;=============================================================================
; boot.asm -- Complete Bootloader
;
; This code is loaded by BIOS to memory address 0x7C00, then CPU begins
; executing from here. Goal: transition from 16-bit Real Mode to 32-bit
; Protected Mode.
;=============================================================================

[bits 16]                     ; Assembler directive: generate 16-bit machine code
[org 0x7C00]                  ; Origin: code will be loaded at 0x7C00
                              ; All labels automatically offset by 0x7C00

;=== Step 1: Disable Interrupts =============================================
start:
    cli                       ; Clear Interrupt Flag (IF = 0)

;=== Step 2: Setup Segment Registers ========================================
    xor ax, ax                ; ax = 0 (2 bytes, smaller than mov ax,0)
    mov ds, ax                ; DS = 0
    mov es, ax                ; ES = 0
    mov ss, ax                ; SS = 0
    mov sp, 0x7C00            ; Stack grows down from 0x7C00

;=== Step 3: Enable A20 Line ================================================
enable_a20:
.wait1:
    in al, 0x64               ; Read 8042 status register
    test al, 0x02             ; Check input buffer full (bit 1)
    jnz .wait1                ; Wait until controller is ready

    mov al, 0xD1              ; Command: write to Output Port
    out 0x64, al

.wait2:
    in al, 0x64               ; Wait for controller again
    test al, 0x02
    jnz .wait2

    mov al, 0xDF              ; 0xDF = 1101_1111 (bit 1 = A20 enable)
    out 0x60, al              ; Write to data port

;=== Step 4: Load GDT =======================================================
    lgdt [gdt_descriptor]     ; Load 6-byte GDT pointer into GDTR

;=== Step 5: Switch to Protected Mode =======================================
    mov eax, cr0              ; Read CR0
    or eax, 0x1               ; Set PE bit (bit 0)
    mov cr0, eax              ; Write CR0 -- now in Protected Mode!
    jmp CODE_SEG:start_pm     ; Far Jump: flush pipeline, load CS

;=== Step 6: 32-bit Protected Mode Entry ====================================
[bits 32]
start_pm:
    mov ax, DATA_SEG          ; ax = Data Segment selector (0x10)
    mov ds, ax
    mov ss, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    mov ebp, 0x90000          ; Setup stack at 0x90000
    mov esp, 0x90000

    call main                 ; Jump to C kernel entry point
    jmp $                     ; Infinite loop (safety net)


;=============================================================================
; GDT Data Definition
;=============================================================================

gdt_start:

;--- Entry 0: Null Descriptor (required by CPU) ---
gdt_null:
    dd 0x0                    ; 8 bytes of zeros
    dd 0x0

;--- Entry 1: Code Segment Descriptor ---
gdt_code:
    dw 0xFFFF                 ; Limit bits 0-15
    dw 0x0000                 ; Base bits 0-15
    db 0x00                   ; Base bits 16-23
    db 10011010b              ; Access: P=1, DPL=00, S=1, E=1, DC=0, RW=1, A=0
    db 11001111b              ; Flags: G=1, D=1, L=0, Rsv=0 | Limit bits 16-19 = 0xF
    db 0x00                   ; Base bits 24-31
                              ; Result: base=0x00000000, limit=4GB, code, readable

;--- Entry 2: Data Segment Descriptor ---
gdt_data:
    dw 0xFFFF                 ; Limit bits 0-15
    dw 0x0000                 ; Base bits 0-15
    db 0x00                   ; Base bits 16-23
    db 10010010b              ; Access: P=1, DPL=00, S=1, E=0, DC=0, RW=1, A=0
    db 11001111b              ; Flags: G=1, D=1, L=0, Rsv=0 | Limit bits 16-19 = 0xF
    db 0x00                   ; Base bits 24-31
                              ; Result: base=0x00000000, limit=4GB, data, writable

gdt_end:

;--- GDT Descriptor (6-byte struct for LGDT instruction) ---
gdt_descriptor:
    dw gdt_end - gdt_start - 1   ; Size of GDT minus 1 (= 23 = 0x17)
    dd gdt_start                  ; Linear address of GDT

;--- Selector constants ---
CODE_SEG equ gdt_code - gdt_start  ; = 0x08 (byte offset of code descriptor)
DATA_SEG equ gdt_data - gdt_start  ; = 0x10 (byte offset of data descriptor)


;=============================================================================
; Boot Sector Padding and Magic Number
;=============================================================================

times 510 - ($ - $$) db 0     ; Pad with zeros to byte 510
                              ; $ = current address, $$ = section start
                              ; 510 - bytes_used = padding needed

dw 0xAA55                     ; Boot signature (little-endian)
                              ; Memory: byte 510 = 0x55, byte 511 = 0xAA
```

### 5.9 Cross-Project Comparison

The following section compares how real-world OS projects implement each
bootloader step. This demonstrates that the six steps are universal --
the only differences are in style, syntax (AT&T vs Intel), and complexity.

#### Step 1: Disable Interrupts

**xv6** (MIT teaching OS):
> Source: [mit-pdos/xv6-public/bootasm.S#L12](https://github.com/mit-pdos/xv6-public/blob/master/bootasm.S#L12)

```asm
.code16                       # AT&T syntax: 16-bit code
.globl start                  # Export "start" label for linker
start:
  cli                         # BIOS enabled interrupts; disable
```

**os-tutorial** (30k-star teaching project):
> Source: [cfenollosa/os-tutorial/10-32bit-enter/32bit-switch.asm#L3](https://github.com/cfenollosa/os-tutorial/blob/master/10-32bit-enter/32bit-switch.asm#L3)

```asm
[bits 16]                     ; NASM/Intel syntax
switch_to_pm:
    cli                       ; 1. disable interrupts
```

> **Syntax note:** xv6 uses GNU as (AT&T syntax: `%ax`, `#` comments).
> os-tutorial uses NASM (Intel syntax: `ax`, `;` comments). The instructions
> are identical.

#### Step 2: Setup Segment Registers

**xv6:**
> Source: [mit-pdos/xv6-public/bootasm.S#L14-L18](https://github.com/mit-pdos/xv6-public/blob/master/bootasm.S#L14)

```asm
  xorw    %ax,%ax             # Set %ax to zero
  movw    %ax,%ds             # -> Data Segment
  movw    %ax,%es             # -> Extra Segment
  movw    %ax,%ss             # -> Stack Segment
```

**ToaruOS** (hobby OS with full GUI):
> Source: [klange/toaruos/boot/boot.S](https://github.com/klange/toaruos/blob/master/boot/boot.S)

```asm
    xor %ax, %ax              # ax = 0
    mov %ax, %ds              # DS = 0
    mov %ax, %ss              # SS = 0
    mov %dl, boot_disk        # Save BIOS boot disk number
    mov $0x7c00, %ax
    mov %ax, %sp              # SP = 0x7C00
```

**nanobyte_os** (YouTube teaching OS):
> Source: [nanobyte-dev/nanobyte_os/src/bootloader/stage1/boot.asm](https://github.com/nanobyte-dev/nanobyte_os/blob/master/src/bootloader/stage1/boot.asm)

```asm
        mov ax, 0               ; Uses mov instead of xor -- just a style choice
        mov ds, ax
        mov es, ax
        mov ss, ax
        mov sp, 0x7C00
```

**Linux kernel** (most complex, backward-compatible with decades of bootloaders):
> Source: [torvalds/linux/arch/x86/boot/header.S -- start_of_setup](https://github.com/torvalds/linux/blob/master/arch/x86/boot/header.S)

```asm
start_of_setup:
    movw    %ds, %ax           # ax = DS
    movw    %ax, %es           # ES = DS
    cld                        # String direction = forward

    # Check if SS was already set by an older bootloader
    movw    %ss, %dx           # dx = SS
    cmpw    %ax, %dx           # DS == SS?
    movw    %sp, %dx           # dx = SP (backup, doesn't affect FLAGS)
    je      2f                 # Equal --> stack already configured, jump to label 2

    # SS invalid -- configure a new stack
    movw    $_end, %dx         # dx = end of setup code
    testb   $CAN_USE_HEAP, loadflags  # Did bootloader set heap flag?
    jz      1f                 # No --> jump to label 1
    movw    heap_end_ptr, %dx  # Yes --> use heap end as base
1:  addw    $STACK_SIZE, %dx   # dx += stack size
    jnc     2f                 # No overflow --> jump to label 2
    xorw    %dx, %dx           # Overflow --> dx = 0 (safety fallback)

2:  andw    $~3, %dx           # 4-byte alignment
    jnz     3f                 # Not zero --> jump to label 3
    movw    $0xfffc, %dx       # Zero --> use default value
3:  movw    %ax, %ss           # SS = DS
    movzwl  %dx, %esp          # ESP = dx (zero-extended to 32-bit)
    sti                        # Re-enable interrupts
```

#### Step 3: Enable A20 Line

**xv6** (keyboard controller method):
> Source: [mit-pdos/xv6-public/bootasm.S#L20-L33](https://github.com/mit-pdos/xv6-public/blob/master/bootasm.S#L20)

```asm
seta20.1:
  inb     $0x64,%al               # Read 8042 status
  testb   $0x2,%al                # Input buffer full?
  jnz     seta20.1                # Yes --> keep waiting

  movb    $0xd1,%al               # Command: write to Output Port
  outb    %al,$0x64

seta20.2:
  inb     $0x64,%al               # Wait for controller ready
  testb   $0x2,%al
  jnz     seta20.2

  movb    $0xdf,%al               # 0xDF = enable A20
  outb    %al,$0x60
```

**ToaruOS** (Fast A20 method -- only 3 lines):
> Source: [klange/toaruos/boot/boot.S#L15-L17](https://github.com/klange/toaruos/blob/master/boot/boot.S)

```asm
    in $0x92, %al              # Read System Control Port A
    or $2, %al                 # Set bit 1 (enable A20)
    out %al, $0x92             # Write back
```

**Linux kernel** (tries all methods, written in C):
> Source: [torvalds/linux/arch/x86/boot/a20.c](https://github.com/torvalds/linux/blob/master/arch/x86/boot/a20.c)

```c
int enable_a20(void)
{
    int loops = A20_ENABLE_LOOPS;       // Try up to 255 times

    while (loops--) {
        if (a20_test_short())           // Method 0: already enabled?
            return 0;

        enable_a20_bios();              // Method 1: BIOS INT 0x15
        if (a20_test_short())
            return 0;

        kbc_err = empty_8042();         // Method 2: keyboard controller 8042
        if (!kbc_err) {
            enable_a20_kbc();
            if (a20_test_long())
                return 0;
        }

        enable_a20_fast();              // Method 3: Fast A20 (Port 0x92)
        if (a20_test_long())
            return 0;
    }

    return -1;                          // All methods failed
}
```

#### Step 4: Setup GDT

**os-tutorial** (hand-written bytes, most explicit):
> Source: [cfenollosa/os-tutorial/09-32bit-gdt/32bit-gdt.asm](https://github.com/cfenollosa/os-tutorial/blob/master/09-32bit-gdt/32bit-gdt.asm)

```asm
gdt_start:
    dd 0x0                    ; Null Descriptor (8 bytes of zeros)
    dd 0x0

gdt_code:                     ; Code Segment
    dw 0xffff                 ; Limit bits 0-15
    dw 0x0                    ; Base bits 0-15
    db 0x0                    ; Base bits 16-23
    db 10011010b              ; Access: Present=1, Ring 0, Executable, Readable
    db 11001111b              ; Flags: 4KB granularity, 32-bit + Limit 16-19
    db 0x0                    ; Base bits 24-31

gdt_data:                     ; Data Segment
    dw 0xffff
    dw 0x0
    db 0x0
    db 10010010b              ; Access: Present=1, Ring 0, Writable
    db 11001111b
    db 0x0

gdt_end:

gdt_descriptor:
    dw gdt_end - gdt_start - 1   ; GDT size - 1
    dd gdt_start                  ; GDT base address

CODE_SEG equ gdt_code - gdt_start  ; = 0x08
DATA_SEG equ gdt_data - gdt_start  ; = 0x10
```

**xv6** (uses assembly macros for cleaner syntax):
> Source: [mit-pdos/xv6-public/bootasm.S#L62-L70](https://github.com/mit-pdos/xv6-public/blob/master/bootasm.S#L62)

```asm
gdt:
  SEG_NULLASM                             # Null Descriptor
  SEG_ASM(STA_X|STA_R, 0x0, 0xffffffff)  # Code: Executable + Readable
  SEG_ASM(STA_W, 0x0, 0xffffffff)        # Data: Writable

gdtdesc:
  .word   (gdtdesc - gdt - 1)            # GDT size - 1
  .long   gdt                            # GDT base address
```

#### Step 5: Switch to Protected Mode

**xv6:**
> Source: [mit-pdos/xv6-public/bootasm.S#L36-L46](https://github.com/mit-pdos/xv6-public/blob/master/bootasm.S#L36)

```asm
  lgdt    gdtdesc              # Load GDT
  movl    %cr0, %eax           # Read CR0
  orl     $CR0_PE, %eax        # Set PE bit
  movl    %eax, %cr0           # Write back --> enter Protected Mode
  ljmp    $(SEG_KCODE<<3), $start32  # Far Jump: flush pipeline, load new CS
```

**os-tutorial:**
> Source: [cfenollosa/os-tutorial/10-32bit-enter/32bit-switch.asm](https://github.com/cfenollosa/os-tutorial/blob/master/10-32bit-enter/32bit-switch.asm)

```asm
    lgdt [gdt_descriptor]      ; Load GDT
    mov eax, cr0
    or eax, 0x1                ; Set PE bit
    mov cr0, eax
    jmp CODE_SEG:init_pm       ; Far Jump
```

**ToaruOS:**
> Source: [klange/toaruos/boot/boot.S#L79-L83](https://github.com/klange/toaruos/blob/master/boot/boot.S)

```asm
    mov %cr0, %eax
    or $1, %eax
    mov %eax, %cr0
    ljmp $0x08,$bios_main      ; Jump directly to C entry point
```

#### Step 6: Enter 32-bit and Jump to Kernel

**xv6:**
> Source: [mit-pdos/xv6-public/bootasm.S#L48-L60](https://github.com/mit-pdos/xv6-public/blob/master/bootasm.S#L48)

```asm
.code32
start32:
  movw    $(SEG_KDATA<<3), %ax    # ax = Data Segment Selector
  movw    %ax, %ds
  movw    %ax, %es
  movw    %ax, %ss
  movw    $0, %ax
  movw    %ax, %fs
  movw    %ax, %gs

  movl    $start, %esp            # Setup stack
  call    bootmain                # Call C function bootmain()

spin:
  jmp     spin                    # Safety loop if bootmain returns
```

**os-tutorial:**
> Source: [cfenollosa/os-tutorial/10-32bit-enter/32bit-switch.asm#L10-L20](https://github.com/cfenollosa/os-tutorial/blob/master/10-32bit-enter/32bit-switch.asm)

```asm
[bits 32]
init_pm:
    mov ax, DATA_SEG
    mov ds, ax
    mov ss, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    mov ebp, 0x90000
    mov esp, ebp

    call BEGIN_PM                 ; Jump to main program
```

#### Summary Comparison Table

| Step | xv6 | os-tutorial | Linux | ToaruOS | nanobyte |
|------|-----|-------------|-------|---------|----------|
| 1. cli | `cli` | `cli` | Yes (later `sti` then `cli`) | `cli` | Not in Stage 1 |
| 2. Segments | `xor` + `mov` | `mov` | Complex compat checks | `xor` + `mov` | `mov ax,0` + `mov` |
| 3. A20 | Keyboard (8042) | Not in Stage 1 | Tries all 4 methods | Fast A20 (0x92) | Not in Stage 1 |
| 4. GDT | Macro `SEG_ASM` | Hand-written bytes | Built in C | Hand-written bytes | Stage 2 |
| 5. CR0+ljmp | `orl`+`ljmp` | `or`+`jmp` | Done in C | `or`+`ljmp` | Stage 2 |
| 6. Kernel | `call bootmain` | `call BEGIN_PM` | `calll main` | `ljmp bios_main` | `jmp Stage2` |

---

## 6. x86 Assembly Reference

This section provides a comprehensive quick-reference for x86 real mode (16-bit)
assembly programming. Instructions are shown in Intel/NASM syntax; the AT&T
equivalents used by GNU as reverse the operand order (e.g., `mov ax, 0` becomes
`movw $0, %ax`).

### 6.1 Registers

```
General-Purpose Registers (data storage and arithmetic):
  AX (AH + AL)  Accumulator -- many instructions implicitly use it
  BX (BH + BL)  Base register
  CX (CH + CL)  Counter (commonly used in loops)
  DX (DH + DL)  Data register (commonly used for I/O)

32-bit extensions: EAX, EBX, ECX, EDX (E = Extended)

Pointer Registers:
  SI   Source Index
  DI   Destination Index
  SP   Stack Pointer (top of stack)
  BP   Base Pointer (stack frame base)
  IP   Instruction Pointer -- cannot be modified directly

Segment Registers:
  CS   Code Segment  (where code executes from)
  DS   Data Segment  (default for data access)
  SS   Stack Segment (where the stack lives)
  ES   Extra Segment (extra data segment)
  FS   (additional, no fixed purpose)
  GS   (additional, no fixed purpose)

Special Registers:
  FLAGS   Status flags register
  CR0     Control Register 0 (used for mode switching)
  GDTR    GDT Register (loaded by lgdt instruction)
```

> **[Ref 12]** Intel SDM, Vol. 1, Section 3.4 -- "Basic Program Execution Registers":
>
> *"The 16 general-purpose registers, the 6 segment registers, the EFLAGS
> register, and the EIP (instruction pointer) register make up the basic
> execution environment in which to execute a set of general-purpose
> instructions."*
>
> -- Intel 64 and IA-32 Architectures Software Developer's Manual, Vol. 1,
>    Order Number 253665

### 6.2 FLAGS Register

```
bit:  15 14 13 12 11 10  9  8  7  6  5  4  3  2  1  0
       -  -  -  - OF DF IF TF SF ZF  - AF  - PF  - CF
```

| Bit | Name | Full Name | When 0 | When 1 |
|-----|------|-----------|--------|--------|
| 0 | CF | Carry Flag | No carry/borrow | Carry or borrow occurred |
| 2 | PF | Parity Flag | Odd number of 1-bits | Even number of 1-bits |
| 6 | ZF | Zero Flag | Result is nonzero | Result is zero |
| 7 | SF | Sign Flag | Result is positive | Result is negative |
| 8 | TF | Trap Flag | Normal execution | Single-step mode (debugging) |
| 9 | IF | Interrupt Flag | Hardware interrupts disabled | Hardware interrupts enabled |
| 10 | DF | Direction Flag | String operations go forward (low->high) | String operations go backward |
| 11 | OF | Overflow Flag | No signed overflow | Signed overflow occurred |

### 6.3 Data Movement

| Instruction | Full Name | Operation | Example |
|-------------|-----------|-----------|---------|
| `mov A, B` | Move | Copy B into A | `mov ax, 0` --> ax = 0 |
| `push A` | Push | Push A onto stack, SP decreases | `push ax` --> stack top = ax value |
| `pop A` | Pop | Pop stack top into A, SP increases | `pop bx` --> bx = stack top value |

### 6.4 Arithmetic and Logic

| Instruction | Full Name | Operation | Example |
|-------------|-----------|-----------|---------|
| `xor A, B` | Exclusive OR | A = A XOR B | `xor ax, ax` --> ax = 0 (self-XOR) |
| `or A, B` | OR | A = A OR B | `or eax, 1` --> set bit 0 to 1 |
| `and A, B` | AND | A = A AND B | `and al, 0x02` --> keep only bit 1 |
| `test A, B` | Test | Compute A AND B, discard result, update FLAGS only | `test al, 0x02` --> check bit 1 |
| `cmp A, B` | Compare | Compute A - B, discard result, update FLAGS only | `cmp ax, 0` --> check if ax == 0 |
| `add A, B` | Add | A = A + B | `add ax, 1` --> ax += 1 |
| `sub A, B` | Subtract | A = A - B | `sub ax, 3` --> ax -= 3 |

### 6.5 Jumps and Branches

| Instruction | Full Name | Condition | Description |
|-------------|-----------|-----------|-------------|
| `jmp X` | Jump | Unconditional | Jump to X |
| `je X` | Jump if Equal | ZF = 1 | Jump if last comparison was equal |
| `jne X` | Jump if Not Equal | ZF = 0 | Jump if not equal |
| `jz X` | Jump if Zero | ZF = 1 | Jump if result was zero (same as `je`) |
| `jnz X` | Jump if Not Zero | ZF = 0 | Jump if result was nonzero |
| `jc X` | Jump if Carry | CF = 1 | Jump if carry/borrow occurred |
| `jnc X` | Jump if No Carry | CF = 0 | Jump if no carry/borrow |
| `jmp $` | Jump to self | -- | Infinite loop (`$` = current address) |
| `call X` | Call | -- | Call function X (pushes return address) |
| `ret` | Return | -- | Return from function (pops return address) |

### 6.6 I/O Instructions

| Instruction | Full Name | Operation | Example |
|-------------|-----------|-----------|---------|
| `in al, PORT` | Input | Read 1 byte from I/O port into AL | `in al, 0x64` --> read keyboard controller status |
| `out PORT, al` | Output | Write AL to I/O port | `out 0x64, al` --> send command to keyboard controller |

### 6.7 Interrupt Instructions

| Instruction | Full Name | Operation |
|-------------|-----------|-----------|
| `cli` | Clear Interrupt Flag | Disable hardware interrupts (IF = 0) |
| `sti` | Set Interrupt Flag | Enable hardware interrupts (IF = 1) |
| `int N` | Interrupt | Trigger software interrupt N |
| `iret` | Interrupt Return | Return from interrupt handler |

### 6.8 String Instructions

| Instruction | Full Name | Operation |
|-------------|-----------|-----------|
| `lodsb` | Load String Byte | Read 1 byte from [SI] into AL, then SI++ |
| `cld` | Clear Direction Flag | String operations go forward (low -> high addresses) |

### 6.9 Special Instructions

| Instruction | Full Name | Operation |
|-------------|-----------|-----------|
| `hlt` | Halt | Stop CPU, wait for interrupt to wake up |
| `lgdt X` | Load GDT | Load GDT address and size into GDTR register |
| `nop` | No Operation | Does nothing, occupies 1 byte (opcode 0x90) |

### 6.10 Data Definition (NASM)

| Directive | Full Name | Size | Example |
|-----------|-----------|------|---------|
| `db` | Define Byte | 1 byte | `db 0x55` --> emit 1 byte |
| `dw` | Define Word | 2 bytes | `dw 0xAA55` --> emit 2 bytes (little-endian) |
| `dd` | Define Double word | 4 bytes | `dd 0x0` --> emit 4 bytes |
| `times N db X` | Repeat | N bytes | `times 510 db 0` --> fill 510 bytes with 0 |

### 6.11 GNU as Directives

**Visibility:**
- `.global _start` -- export symbol, making it visible to the linker and other
  `.o` files. Without this, the symbol is local to the file.

**Instruction mode:**
- `.code16` -- generate 16-bit instructions (real mode)
- `.code32` -- generate 32-bit instructions (protected mode)
- `.code64` -- generate 64-bit instructions (long mode)

**Section directives:**
- `.text` -- code section (shortcut for `.section .text`)
- `.data` -- initialized data section
- `.bss` -- uninitialized data section (zero-filled, no file space)
- `.rodata` -- read-only data section
- `.section .name` -- custom-named section

**Data directives:**
- `.byte 0x55, 0xAA` -- emit raw bytes (like `unsigned char s[] = {0x55, 0xAA}`)
- `.asciz "Hello"` -- emit null-terminated string
- `.space 64` -- reserve 64 bytes (filled with 0)
- `.org 0x1FE, 0x90` -- advance position to offset, fill with specified byte

A typical bare-metal assembly file:

```asm
#include "os.h"          /* .S only -- preprocessor handles this */

    .global _start       /* Export _start for linker */
    .code16              /* Real mode: 16-bit instructions */

    .text                /* Code section */
_start:
    jmp .                /* Infinite loop */

    .data                /* Initialized data section */
msg:
    .asciz "Hello"

    .bss                 /* Uninitialized data section */
    .space 64
```

### 6.12 .S vs .s File Extensions

GCC treats assembly files differently based on the extension:

| Extension | Pipeline |
|-----------|----------|
| `.s` (lowercase) | Assembler only: `file.s --> as --> file.o` |
| `.S` (uppercase) | C preprocessor + assembler: `file.S --> cpp --> file.s --> as --> file.o` |

Because `.S` goes through the C preprocessor, you can use:
- `#include "os.h"` -- share constants between assembly and C
- `#define`, `#ifdef`, `#if` -- conditional compilation
- Macro expansion

This is why `start.S` can `#include "os.h"` -- the preprocessor handles it
before the assembler ever sees the file.

> **[Ref 13]** GNU as manual -- "Preprocessing":
>
> *"You can use the GNU C compiler driver to get other 'CPP' style preprocessing
> by giving the input file a `.S` suffix."*
>
> -- https://sourceware.org/binutils/docs/as/Preprocessing.html

### 6.13 Entry Point (_start)

`_start` is the default entry point symbol that `ld` looks for.
It is NOT a keyword -- just a convention. The entry point is determined by
(in order of priority):

| Method | Example |
|--------|---------|
| Linker script `ENTRY()` | `ENTRY(_start)` <-- our os.lds uses this |
| ld command-line `-e` | `ld -e my_entry` |
| Default | ld looks for symbol named `_start` |

You could name it `_boot` and set `ENTRY(_boot)` in the linker script.

> **[Ref 14]** GNU ld manual -- "Setting the Entry Point":
>
> *"There are several ways to set the entry point. The linker will set the
> entry point by trying each of the following methods in order, and stopping
> when one of them succeeds:
> the `-e` entry command-line option;
> the `ENTRY(symbol)` command in a linker script;
> the value of a target-specific symbol, if it is defined;
> the address of the first byte of the code section, if present;
> The address `0`."*
>
> -- https://sourceware.org/binutils/docs/ld/Entry-Point.html

### 6.14 Section Naming Convention

The names `.text`, `.data`, `.bss` are not linker keywords -- they are
**historical conventions** from early Unix and mainframe systems:

| Name | Origin | Era | Meaning |
|------|--------|-----|---------|
| `.text` | GE-635 assembly | 1960s | "The text of the program" (code) |
| `.data` | Unix | 1970s | Initialized global data |
| `.bss` | IBM 704 assembly | 1950s | "Block Started by Symbol" (uninitialized data) |

These names were inherited by Unix, formalized in the **ELF specification**
(System V ABI, 1990s), and implemented in GNU ld's default linker script.

The assembler assigns default attributes based on the name:

| Section name | Default ELF flags | Meaning |
|-------------|-------------------|---------|
| `.text` | `SHF_ALLOC + SHF_EXECINSTR` | Allocate + executable |
| `.data` | `SHF_ALLOC + SHF_WRITE` | Allocate + writable |
| `.bss` | `SHF_ALLOC + SHF_WRITE` + `SHT_NOBITS` | Writable, no file space |
| `.rodata` | `SHF_ALLOC` | Allocate only (read-only) |
| Custom name | None | Must specify attributes manually |

> **[Ref 15]** GNU as manual -- `.section` (ELF Version):
>
> *"If no flags are specified, the default flags depend upon the section name.
> If the section name is not recognized, the default will be for the section
> to have none of the above flags: it will not be allocated in memory, nor
> writable, nor executable."*
>
> Section flags include: `a` (allocatable), `w` (writable), `x` (executable).
>
> -- https://sourceware.org/binutils/docs/as/Section.html

You can view GNU ld's built-in default linker script with:

```bash
x86_64-elf-ld --verbose
```

Most applications never need a custom linker script -- the default handles
everything. Custom scripts are only needed for:
- Bare-metal / OS development (like this project)
- Embedded systems (specifying Flash/RAM addresses)
- Bootloaders and kernels

---

## 7. ELF vs Flat Binary

The build pipeline produces multiple file formats:

| File | Format | ELF Type | Purpose |
|------|--------|----------|---------|
| `start.o` / `os.o` | ELF | `ET_REL` (relocatable) | Addresses unresolved, has relocation entries |
| `os.elf` | ELF | `ET_EXEC` (executable) | Addresses fixed at 0x7C00, symbols resolved |
| `os.bin` | Flat binary | Not ELF | Pure machine code, no headers |

Verify with the `file` command:

```bash
file start.o   # --> ELF 32-bit LSB relocatable, Intel 80386
file os.elf    # --> ELF 32-bit LSB executable, Intel 80386
file os.bin    # --> data (raw bytes, no recognized format)
```

### 7.1 Why objcopy is Needed

BIOS does not understand ELF format. It reads raw bytes from disk and
jumps to them. If you `dd` the ELF file directly, BIOS would try to
execute the ELF magic number (`0x7F 'E' 'L' 'F'`), which is not valid
x86 instructions.

`objcopy -O binary` strips all ELF headers, section tables, symbol tables,
and debug info, leaving only the raw machine code:

```
os.elf (ELF):
+------------+-----------+----------+-----------+
| ELF header | .text     | .data    | sym table |
| metadata   | (code)    | (data)   | debug info|
+------------+-----------+----------+-----------+

          objcopy -O binary (strip everything except code/data)
                    |
                    v
os.bin (flat binary):
+-----------+----------+
| .text     | .data    |
| (code)    | (data)   |
+-----------+----------+
```

Both files are needed:
- `os.elf` -- for GDB debugging (has symbols, source mapping, debug info)
- `os.bin` -- for BIOS to load (pure machine code, dd'd into disk.img)

### 7.2 Why -Ttext=0x7C00 / Linker Script

The linker needs to know where the code will reside in memory, because
**absolute addresses are baked into the machine code** during linking.

```asm
mov $msg, %si    /* Absolute address of msg is encoded in the instruction */
```

- With `. = 0x7C00`: linker calculates `msg = 0x7C00 + offset` --> correct
- Without it: linker defaults to `0x0` --> `msg` points to wrong address --> crash

This affects BOTH the flat binary (os.bin) and GDB:
- `os.bin`: the machine code contains addresses based on 0x7C00
- `os.elf`: GDB reads addresses from ELF to set breakpoints correctly

`objcopy -O binary` only strips headers -- the machine code with its
embedded addresses is copied verbatim.

For the current single `jmp .` instruction, this doesn't matter (relative
jump). It becomes critical once the code uses global variables or function
calls with absolute addresses.

---

## 8. The dd Command

`dd` (data duplicator) is a block-level copy tool from Unix. In OS
development, it's used to manipulate raw disk images at specific offsets.

> **[Ref 16]** dd(1) man page:
>
> *"Copy a file, converting and formatting according to the operands."*
>
> *"`seek=N` (or `oseek=N`) skip N obs-sized output blocks"*
>
> *"`notrunc` -- do not truncate the output file"*
>
> -- https://man7.org/linux/man-pages/man1/dd.1.html

### 8.1 Creating a Blank Disk Image

```bash
dd if=/dev/zero of=disk.img bs=512 count=20160
```

| Parameter | Value | Meaning |
|-----------|-------|---------|
| `if` | `/dev/zero` | Input: Unix special device, always reads `0x00` |
| `of` | `disk.img` | Output: target disk image file |
| `bs` | `512` | Block size: 512 bytes = 1 disk sector |
| `count` | `20160` | Number of blocks to copy |

Result: 512 * 20160 = 10,321,920 bytes (~10MB) of zeros.

### 8.2 Writing the Boot Sector

```bash
dd if=os.bin of=disk.img conv=notrunc
```

| Parameter | Value | Meaning |
|-----------|-------|---------|
| `if` | `os.bin` | Input: the compiled flat binary |
| `of` | `disk.img` | Output: the disk image |
| `conv` | `notrunc` | Conversion option: do NOT truncate output file |

`dd` writes from offset 0 by default. Only the first N bytes (size of
os.bin) are overwritten; the rest of the 10MB image stays intact.

**Why `conv=notrunc` is critical:** `conv` stands for "conversion."
`dd` was designed for full 1:1 block device duplication
(e.g., `dd if=/dev/sda of=backup.img`), where truncating the output to
match the input size is the correct default. Here we're doing a non-typical
partial overwrite -- writing a 512-byte os.bin into a 10MB image. Without
`notrunc`, dd would truncate disk.img to 512 bytes, destroying the rest.

### 8.3 Parameter Reference

| Parameter | Description | Unit |
|-----------|-------------|------|
| `if=FILE` | Input file | -- |
| `of=FILE` | Output file | -- |
| `bs=N` | Block size (bytes per read/write) | bytes |
| `count=N` | Number of blocks to copy | `bs` |
| `seek=N` | Skip N blocks in output before writing | `bs` |
| `skip=N` | Skip N blocks in input before reading | `bs` |
| `conv=X` | Conversion options (comma-separated) | -- |

Common `conv` options:

| Option | Meaning |
|--------|---------|
| `notrunc` | No truncate: preserve output file size |
| `noerror` | Continue on read errors |
| `sync` | Pad input blocks to `bs` size with zeros |

`seek` unit depends on `bs`:

```bash
# Write to sector 0 (default)
dd if=boot.bin of=disk.img bs=512 conv=notrunc

# Write starting at sector 1
dd if=kernel.bin of=disk.img bs=512 seek=1 conv=notrunc

# Write at byte offset 510 (for boot signature)
printf '\x55\xAA' | dd of=disk.img bs=1 seek=510 conv=notrunc
#                                   ^ bs=1 so seek=510 means 510 bytes
```

---

## 9. QEMU Debugging

### 9.1 QEMU Command Breakdown

```bash
qemu-system-i386 -m 128M -s -S -drive file=disk.img,index=0,media=disk,format=raw
```

| Parameter | Meaning |
|-----------|---------|
| `qemu-system-i386` | Emulate an i386 (32-bit x86) machine |
| `-m 128M` | Allocate 128MB RAM for the VM |
| `-s` | Shorthand for `-gdb tcp::1234` (GDB server on port 1234) |
| `-S` | Freeze CPU at startup, wait for GDB to connect |
| `-drive file=disk.img` | Path to disk image |
| `index=0` | First drive (BIOS boots from index 0) |
| `media=disk` | Media type: hard disk (alternative: `cdrom`) |
| `format=raw` | Image format: raw binary (not qcow2, vmdk, etc.) |

Boot flow with debugging:

```
1. QEMU starts, CPU paused, GDB server on localhost:1234
2. VSCode GDB connects to 127.0.0.1:1234
3. BIOS loads sector 0 of disk.img (512 bytes) to memory 0x7C00
4. CPU jumps to 0x7C00 and begins executing
```

QEMU controls:

| Action | Key |
|--------|-----|
| Release mouse | `Ctrl+Alt+G` |
| Quit QEMU | `Ctrl+A` then `X` |
| Quit (menu) | Machine --> Quit |
| Quit (terminal) | `killall qemu-system-i386` |

### 9.2 VSCode Integration

The task chain in `.vscode/tasks.json`:

```
F5 (Start Debugging)
  |
  +-- 1. CMake configure  --> generates build system
  +-- 2. CMake build      --> compile + link + objcopy + dd (all post-build steps)
  +-- 3. Start qemu       --> launches QEMU with GDB server
  +-- 4. GDB attaches     --> connects to localhost:1234, runs until 0x7C00
```

---

## 10. Memory Protection

### 10.1 Real Mode (No Protection)

When the x86 CPU powers on, it starts in **real mode** (16-bit). In this
mode:
- No paging, no page tables
- MMU is not active
- All memory is effectively RWX (read/write/execute)
- Any code can access any memory address
- This is the 1978 Intel 8086 behavior

This is why the boot sector works without any R/W/X permissions -- there is
no hardware enforcing them.

### 10.2 Protected Mode (Segmentation + Paging)

Memory protection is enabled in stages:

```
Real Mode
  |  Set up GDT, enable CR0.PE bit
  v
Protected Mode (without paging)
  |  Segment-level protection (ring 0~3)
  |  But memory still directly accessed, no page table
  |  No per-page R/W/X control yet
  |
  |  Set up page tables, enable CR0.PG bit
  v
Protected Mode (with paging)
  |  MMU active, every memory access checked against page table
  |  Per-page R/W/X permissions (4KB granularity)
  |  This is what modern operating systems use
```

Modern OSes (Linux, macOS, Windows) don't use segment-level protection
(all segments set to flat: base=0, limit=4GB). They rely entirely on
paging for per-page R/W/X control.

### 10.3 How W^X Works in Modern OS

W^X (Write XOR Execute) means a memory page is either writable or
executable, never both. The enforcement involves four layers, but
**none of them "check" your program** -- they just do their job:

| Layer | What it does | When |
|-------|-------------|------|
| **Linker** | Packs sections into segments, marks permissions (R/W/X) in ELF header | Compile time |
| **Loader** | Reads ELF header, sets page table permissions via `mmap()` | Load time |
| **MMU** | Checks page table on every memory access | Every instruction |
| **OS Kernel** | Receives page fault from MMU, sends SIGBUS/SIGSEGV | On violation |

The loader does NOT validate your program. It blindly sets whatever
permissions the ELF header says. If your code is in a RW- segment (like
`__DATA`), the loader maps it as RW- without complaint. The crash only
happens at runtime when the CPU tries to execute from that page and the
MMU blocks it.

### 10.4 Test: Putting Code in the Wrong Section

The following test demonstrates W^X protection on macOS. A simple program
is compiled twice -- once normally, once with code moved to `__DATA` segment:

```c
/* test_crash.c */
#include <stdio.h>
int main(void) {
    printf("Hello from main!\n");
    return 0;
}
```

```bash
# Normal: code stays in __TEXT segment
$ clang -o test_normal test_crash.c

# Broken: move __TEXT,__text into __DATA,__text
$ clang -o test_broken test_crash.c -Wl,-rename_section,__TEXT,__text,__DATA,__text
```

Verify the section placement with `otool`:

```
$ otool -l test_normal | grep -A5 "sectname __text"
  sectname __text
   segname __TEXT           <-- code is in __TEXT (R-X)  [correct]

$ otool -l test_broken | grep -A5 "sectname __text"
  sectname __text
   segname __DATA           <-- code moved to __DATA (RW-)  [wrong]
```

Run both:

```
$ ./test_normal
Hello from main!             <-- works fine (exit 0)

$ ./test_broken
                             <-- SIGBUS (exit 138) -- crashes immediately
```

**Why exit code 138?** -- 128 + 10 = signal 10 = SIGBUS on macOS.

**Why the broken version still "finds" main():**

The crash is NOT because the loader can't find `main()`. The address is
correct -- the permissions are wrong:

```
Memory:
    vaddr            Content           Permission
    0x100008000      main() code       RW- (wrong -- should be R-X)
                     ^
                     CPU jumps here
                     Address correct  [yes]
                     Code present     [yes]
                     MMU says: page not executable  [no] --> SIGBUS
```

The loader is just a delivery worker -- it reads the Mach-O/ELF header,
maps segments to memory with whatever permissions the header says, and
jumps to the entry point. It does NOT check whether code is in the right
segment. The actual enforcement is done by the MMU hardware at the moment
the CPU tries to execute from that page.

If the address were wrong (pointing to unmapped memory), the signal would
be SIGSEGV (segmentation fault), not SIGBUS. Receiving SIGBUS specifically
means the address is valid but the access type (execute) violates the
page's permission bits.

The equivalent in GNU ld linker script terms would be:

```
SECTIONS {
    .data : {
        *(.text)   /* Code in data segment --> RW- --> not executable --> crash */
        *(.data)
    }
}
```

---

## 11. Useful Tools

### Inspecting Binary Files

| Tool | Purpose | Install | Example |
|------|---------|---------|---------|
| `hexyl` | Colorized hex dump | `brew install hexyl` | `hexyl -n 512 disk.img` |
| `radare2` | Interactive reverse engineering TUI | `brew install radare2` | `r2 -b 16 -e asm.arch=x86 disk.img` |
| `ImHex` | GUI hex editor with pattern support | `brew install --cask imhex` | `open -a ImHex disk.img` |
| `xxd` | Basic hex dump (built-in) | -- | `xxd -l 512 disk.img` |
| `objdump` | Disassemble raw binary | -- | `objdump -D -b binary -m i8086 -M intel disk.img` |

### radare2 Quick Reference

```bash
r2 -b 16 -e asm.arch=x86 disk.img
```

| Key | Action |
|-----|--------|
| `V` | Enter visual mode |
| `p` | Cycle through panels (hex / disasm / debug) |
| `s 0x1FE` | Seek to offset |
| `q` | Quit |

### objdump for Raw Binary

```bash
objdump -D -b binary -m i8086 -M intel disk.img
```

| Flag | Meaning |
|------|---------|
| `-D` | Disassemble all sections |
| `-b binary` | Input is flat binary (not ELF) |
| `-m i8086` | Interpret as 16-bit x86 |
| `-M intel` | Use Intel syntax (default is AT&T) |

---

## 12. Notable OS Projects

The following projects are referenced throughout this document or serve as
useful learning resources for OS development.

### Teaching / Tutorial Projects

| # | Project | Stars | Description | Link |
|---|---------|-------|-------------|------|
| 1 | xv6 | 7k+ | MIT teaching OS, cleanest codebase | [github.com/mit-pdos/xv6-public](https://github.com/mit-pdos/xv6-public) |
| 2 | os-tutorial | 30k+ | From zero, 24 incremental steps | [github.com/cfenollosa/os-tutorial](https://github.com/cfenollosa/os-tutorial) |
| 3 | How-to-Make-a-Computer-OS | 22k+ | OS in C++ | [github.com/SamyPesse/How-to-Make-a-Computer-Operating-System](https://github.com/SamyPesse/How-to-Make-a-Computer-Operating-System) |
| 4 | nanobyte_os | -- | YouTube tutorial OS with full videos | [github.com/nanobyte-dev/nanobyte_os](https://github.com/nanobyte-dev/nanobyte_os) |
| 5 | blog_os | 16k+ | OS in Rust, excellent written tutorials | [github.com/phil-opp/blog_os](https://github.com/phil-opp/blog_os) |

### Production / Well-Known Kernels

| # | Project | Stars | Description | Link |
|---|---------|-------|-------------|------|
| 6 | Linux Kernel | 190k+ | The Linux kernel | [github.com/torvalds/linux](https://github.com/torvalds/linux) |
| 7 | SerenityOS | 31k+ | Unix-like desktop OS | [github.com/SerenityOS/serenity](https://github.com/SerenityOS/serenity) |
| 8 | GRUB | -- | Most widely used bootloader | [www.gnu.org/software/grub](https://www.gnu.org/software/grub/) |

### Hobby / Independent Projects

| # | Project | Stars | Description | Link |
|---|---------|-------|-------------|------|
| 9 | ToaruOS | 6k+ | Hobby OS with full GUI | [github.com/klange/toaruos](https://github.com/klange/toaruos) |
| 10 | Cyjon | 450+ | Pure assembly 64-bit OS | [github.com/CorruptedByCPU/Cyjon](https://github.com/CorruptedByCPU/Cyjon) |
| 11 | MikeOS | -- | 16-bit Real Mode OS | [mikeos.sourceforge.net](http://mikeos.sourceforge.net/) |
| 12 | Kolibri OS | -- | Entire OS in assembly, includes GUI | [kolibrios.org](https://kolibrios.org/) |

---

## 13. References

All references cited throughout this document are collected here for
convenience. Each entry includes the quoted excerpt used in the main text.

---

**[Ref 1]** CMake Documentation -- `target_link_options()`

> *"The `LINKER:` prefix and `,` separator can be used to specify, in a portable way,
> options to pass to the linker tool."*

Source: https://cmake.org/cmake/help/latest/command/target_link_options.html

---

**[Ref 2]** CMake Documentation -- `CMAKE_SYSTEM_NAME`

> *"`Generic` -- Some platforms, e.g. bare metal embedded devices"*

Source: https://cmake.org/cmake/help/latest/variable/CMAKE_SYSTEM_NAME.html

---

**[Ref 3]** GNU ld Manual -- "Simple Linker Script Example"

> *"The expression `*(.text)` means all `.text` input sections in all input files."*
> *"The `*` is a wildcard which matches any file name."*

Source: https://sourceware.org/binutils/docs/ld/Simple-Example.html

---

**[Ref 4]** Intel 64 and IA-32 Architectures Software Developer's Manual, Vol. 3A, Section 9.1.4

> *"The first instruction that is fetched and executed following a hardware
> reset is located at physical address FFFFFFF0H."*

Source: Intel SDM, Order Number 253668

---

**[Ref 5]** OSDev Wiki -- "Memory Map (x86)"

> *"The traditional BIOS boot process loads the first sector (512 bytes) of
> the boot device to address 0x7C00."*

Source: https://wiki.osdev.org/Memory_Map_(x86)

---

**[Ref 6]** GNU as Manual -- `.org`

> *"Advance the location counter of the current section to new-lc."*
> *"`.org` may only increase the location counter, or leave it unchanged."*
> *"Beware that the origin is relative to the start of the section, not to
> the start of the subsection."*

Source: https://sourceware.org/binutils/docs/as/Org.html

---

**[Ref 7]** Intel SDM, Vol. 2B -- `CLI` Instruction

> *"If the current privilege level is at or below the IOPL, the IF flag is
> cleared to 0."*

Source: Intel SDM, Order Number 253667

---

**[Ref 8]** Intel SDM, Vol. 3A, Section 20.1 -- "Real-Address Mode Operation"

> *"In real-address mode, the processor does not interpret the segment
> selectors as indexes into a descriptor table. Instead, a segment selector
> is used directly to compute a linear address by shifting it left 4 bits
> and adding the effective address."*

Source: Intel SDM, Order Number 253668

---

**[Ref 9]** OSDev Wiki -- "A20 Line"

> *"The A20 Address Line is the physical representation of the 21st bit
> of any memory access. [...] The gate was connected to the Intel 8042
> keyboard controller because it happened to have a spare pin."*

Source: https://wiki.osdev.org/A20_Line

---

**[Ref 10]** Intel SDM, Vol. 3A, Section 3.5.1 -- "Segment Descriptor Tables"

> *"The GDT is not a segment itself; instead, it is a data structure in
> linear address space. [...] The first descriptor in the GDT is not used
> by the processor."*

Source: Intel SDM, Order Number 253668

---

**[Ref 11]** Intel SDM, Vol. 3A, Section 9.9.1 -- "Switching to Protected Mode"

> *"Execute a MOV CR0 instruction that sets the PE flag [...] Immediately
> following the MOV CR0 instruction, execute a far JMP or far CALL
> instruction."*

Source: Intel SDM, Order Number 253668

---

**[Ref 12]** Intel SDM, Vol. 1, Section 3.4 -- "Basic Program Execution Registers"

> *"The 16 general-purpose registers, the 6 segment registers, the EFLAGS
> register, and the EIP register make up the basic execution environment."*

Source: Intel SDM, Order Number 253665

---

**[Ref 13]** GNU as Manual -- "Preprocessing"

> *"You can use the GNU C compiler driver to get other 'CPP' style preprocessing
> by giving the input file a `.S` suffix."*

Source: https://sourceware.org/binutils/docs/as/Preprocessing.html

---

**[Ref 14]** GNU ld Manual -- "Setting the Entry Point"

> *"There are several ways to set the entry point. The linker will set the
> entry point by trying each of the following methods in order, and stopping
> when one of them succeeds: the `-e` entry command-line option; the
> `ENTRY(symbol)` command in a linker script; [...]"*

Source: https://sourceware.org/binutils/docs/ld/Entry-Point.html

---

**[Ref 15]** GNU as Manual -- `.section` (ELF Version)

> *"If no flags are specified, the default flags depend upon the section name.
> If the section name is not recognized, the default will be for the section
> to have none of the above flags: it will not be allocated in memory, nor
> writable, nor executable."*

Source: https://sourceware.org/binutils/docs/as/Section.html

---

**[Ref 16]** dd(1) Man Page

> *"Copy a file, converting and formatting according to the operands."*
> *"`seek=N` skip N obs-sized output blocks"*
> *"`notrunc` -- do not truncate the output file"*

Source: https://man7.org/linux/man-pages/man1/dd.1.html
