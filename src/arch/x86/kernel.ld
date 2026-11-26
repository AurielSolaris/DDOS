OUTPUT_FORMAT(elf32-i386)
ENTRY(_start)
SECTIONS
{
    . = 0;
    .text : { *(.entry) *(.text .text.*) }
    .domain_bridge ALIGN(0x1000) : {
        __domain_bridge_start = .;
        *(.domain_bridge)
        __domain_bridge_end = .;
    }
    .domain_state (NOLOAD) : { *(.domain_state) }
    ASSERT(ADDR(.domain_state) + SIZEOF(.domain_state) - __domain_bridge_start <= 0x1000,
           "domain bridge and metadata exceed one page")
    /* Keep all other read-only data out of the domain bridge page. */
    .rodata ALIGN(0x1000) : { *(.rodata .rodata.*) }
    .data : { *(.data .data.*) }
    .bss : {
        __bss_start = .;
        *(.bss .bss.*) *(COMMON)
        __bss_end = .;
    }
    ASSERT(__bss_end <= 0xd000, "kernel leaves insufficient stack space")
    /* One dedicated entry stack, below the ordinary kernel stack. */
    __domain_stack_top = 0xe000;
    /* Shared firmware layout, above the BIOS DOS arena and diagnostic pages. */
    .fs_workspace 0x2a000 (NOLOAD) : { *(.fs_workspace) }
    ASSERT(SIZEOF(.fs_workspace) <= 0x2000, "filesystem workspace exceeds reserved memory")
    /DISCARD/ : { *(.eh_frame*) *(.comment) *(.note*) }
}
