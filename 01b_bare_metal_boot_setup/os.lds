/* Linker script for bare-metal x86 boot sector */

ENTRY(_start)

SECTIONS
{
    /* BIOS loads boot sector to 0x7C00 */
    . = 0x7C00;

    .text : {
        *(.text)
    }

    .rodata : {
        *(.rodata)
    }

    .data : {
        *(.data)
    }

    .bss : {
        *(.bss)
    }

    /* Discard unnecessary sections */
    /DISCARD/ : {
        *(.note*)
        *(.comment*)
        *(.eh_frame*)
    }
}
