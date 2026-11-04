/* The x86-64 example (x86_64.arche), loaded by QEMU's multiboot loader at
   1 MiB. One output section holds code and data, so their file offsets are
   their addresses' offsets, as the multiboot header's address fields say. */
ENTRY(_start)
SECTIONS
{
    . = 0x100000;
    __load_start = .;
    .text : {
        KEEP(*(.multiboot))
        KEEP(*(.text.boot))
        *(.text .text.*)
        *(.rodata .rodata.*)
        *(.data .data.*)
    }
    __load_end = .;
    .bss : {
        *(.bss .bss.*)
        *(COMMON)
    }
    __bss_end = .;
    /DISCARD/ : { *(.eh_frame*) *(.note*) *(.comment) }
}
