/**
 * 16-bit and 32-bit mixed startup code
 */
	#include "os.h"

	/* Declare _start as a global symbol so it can be referenced from other files */
	.global _start

	/* Generate 16-bit machine instructions for real mode execution at boot */
  	.code16

	/* Text (code) section */
 	.text
_start: 
	jmp .

	/*
	 * Boot signature — BIOS checks bytes at offset 510-511 of sector 0
	 * for the magic number 0x55AA. Without it, BIOS considers the disk
	 * not bootable and shows "No bootable device found".
	 *
	 * .org 0x1FE, 0x90
	 *   - Advance the current position to offset 0x1FE (510) from the
	 *     start of this section. The offset is relative to the section,
	 *     not the absolute memory address (linker script handles that).
	 *   - Fill the gap with 0x90 (NOP instruction). If omitted, the
	 *     default fill byte is 0x00.
	 *   - If code exceeds 510 bytes, the assembler will report an error,
	 *     which also serves as a guard against overflowing the 512-byte
	 *     boot sector limit.
	 *
	 * .byte 0x55, 0xAA
	 *   - Write two bytes at offset 510-511 (little-endian: 0x55 then 0xAA).
	 *   - Equivalent to: unsigned char sig[2] = {0x55, 0xAA};
	 *
	 * The same result can be achieved externally with dd:
	 *   printf '\x55\xAA' | dd of=disk.img bs=1 seek=510 conv=notrunc
	 * But embedding it here keeps the boot sector structure self-contained.
	 *
	 * Resulting boot sector layout (hexyl -n 512 disk.img):
	 *
	 *   00000000: eb fe 90 90 90 90 90 90  90 90 90 90 90 90 90 90
	 *   00000010: 90 90 90 90 90 90 90 90  90 90 90 90 90 90 90 90
	 *   *         ... (NOP fill) ...
	 *   000001f0: 90 90 90 90 90 90 90 90  90 90 90 90 90 90 55 aa
	 *
	 *   offset 0x000: eb fe        <- jmp . (infinite loop, our boot code)
	 *   offset 0x002: 90 90 ...    <- NOP fill (from .org 0x1FE, 0x90)
	 *   offset 0x1FE: 55 aa        <- boot signature
	 */
	.org 0x1FE, 0x90
	.byte 0x55, 0xAA
