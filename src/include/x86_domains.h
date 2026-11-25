#ifndef DDOS_X86_DOMAINS_H
#define DDOS_X86_DOMAINS_H

#define DDOS_X86_DOMAIN_PAGES 22

#ifndef __ASSEMBLER__
#include <stdint.h>

void x86_domains_configure(uint32_t kernel_base, uint32_t arena_offset, uint32_t bytes);
void x86_domains_test(void);
#endif

#endif