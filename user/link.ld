OUTPUT_FORMAT(elf32-i386)
ENTRY(_start)
PHDRS
{
    text PT_LOAD FLAGS(5);
    data PT_LOAD FLAGS(6);
}
SECTIONS
{
    . = 0;
    .text : { *(.text.start) *(.text .text.*) } :text
    ASSERT(. <= 0x1000, "native program exceeds the code page")
    . = 0x1000;
    .data : { *(.rodata .rodata.*) *(.data .data.*) } :data
    .bss : { *(.bss .bss.*) *(COMMON) } :data
    ASSERT(. <= 0x2000, "native program exceeds the data page")
    /DISCARD/ : { *(.eh_frame*) *(.comment) *(.note*) }
}