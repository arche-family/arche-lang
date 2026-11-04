/* The RISC-V example (riscv64.arche): QEMU's virt machine starts the hart at
   0x80000000, where the boot code goes first. */
ENTRY(_start)
SECTIONS
{
    . = 0x80000000;
    .text : {
        KEEP(*(.text.boot))
        *(.text .text.*)
    }
    .rodata : { *(.rodata .rodata.* .srodata .srodata.*) }
    .data : { *(.data .data.* .sdata .sdata.*) }
    .bss : { *(.bss .bss.* .sbss .sbss.*) *(COMMON) }
    /DISCARD/ : { *(.eh_frame*) *(.note*) *(.comment) *(.riscv.attributes) }
}
