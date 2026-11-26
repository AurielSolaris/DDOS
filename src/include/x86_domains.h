#ifndef DDOS_X86_DOMAINS_H
#define DDOS_X86_DOMAINS_H

#define DDOS_X86_DOMAIN_PAGES 12
#define DDOS_X86_NATIVE_TASKS 24
#define DDOS_X86_NATIVE_METADATA_PAGES 18
#define DDOS_X86_NATIVE_PAGES ((DDOS_X86_NATIVE_TASKS + 1) * 256 + DDOS_X86_NATIVE_METADATA_PAGES)

#ifndef __ASSEMBLER__
#include <stdint.h>

void x86_domains_configure(uint32_t kernel_base, uint32_t arena_offset, uint32_t bytes);
uint32_t x86_physical_address(const void *offset);
void x86_native_configure(uint32_t arena_offset);
void x86_domains_test(void);
#endif

#endif