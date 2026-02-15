#!/bin/bash
# Launch QEMU for macOS with GDB debugging enabled.
#
# qemu-system-i386  — Emulate an i386 (32-bit x86) machine
# -m 128M           — Allocate 128MB of RAM for the virtual machine
# -s                — Shorthand for -gdb tcp::1234 (start GDB server on port 1234)
# -S                — Freeze CPU at startup, wait for GDB to connect before executing
# -drive            — Attach a virtual disk:
#   file=disk.img   —   Path to the raw disk image
#   index=0         —   First drive (BIOS boots from index 0)
#   media=disk      —   Media type is hard disk (alternative: cdrom)
#   format=raw      —   Image format is raw binary (not qcow2, vmdk, etc.)
#
# Boot flow:
#   1. QEMU starts with CPU paused, GDB server listening on localhost:1234
#   2. VSCode GDB connects to 127.0.0.1:1234
#   3. BIOS loads sector 0 of disk.img (512 bytes) into memory at 0x7C00
#   4. CPU jumps to 0x7C00 and begins execution
qemu-system-i386 -m 128M -s -S -drive file=disk.img,index=0,media=disk,format=raw
