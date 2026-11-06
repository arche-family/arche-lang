/* The ARM64 example (aarch64.arche): QEMU's virt machine has its RAM at
   0x40000000, and -kernel starts an ELF at its entry point there. */
ENTRY(_start)
SECTIONS
{
    . = 0x40080000;
    .text : {
        KEEP(*(.text.boot))
        *(.text .text.*)
    }
    .rodata : { *(.rodata .rodata.*) }
    .data : { *(.data .data.*) }
    .bss : { *(.bss .bss.*) *(COMMON) }
    /DISCARD/ : { *(.eh_frame*) *(.note*) *(.comment) }
}
