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
    ASSERT(__domain_bridge_end - __domain_bridge_start <= 0x1000, "domain bridge exceeds one page")
    /* Keep all other read-only data out of the domain bridge's page. */
    .rodata ALIGN(0x1000) : { *(.rodata .rodata.*) }
    .data : { *(.data .data.*) }
    .bss : {
        __bss_start = .;
        *(.bss .bss.*) *(COMMON)
        __bss_end = .;
    }
    ASSERT(__bss_end <= 0xc000, "kernel leaves insufficient stack space")
    .domain_state 0xc000 (NOLOAD) : {
        __domain_state_start = .;
        *(.domain_state)
    }
    ASSERT(SIZEOF(.domain_state) <= 0x1000, "domain bridge metadata exceeds one page")
    /* One dedicated entry stack, below the ordinary kernel stack. */
    __domain_stack_top = 0xe000;
    /DISCARD/ : { *(.eh_frame*) *(.comment) *(.note*) }
}
