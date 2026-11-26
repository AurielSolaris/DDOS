#ifndef DDOS_PLATFORM_H
#define DDOS_PLATFORM_H

#include <stdbool.h>
#include <stdint.h>

void platform_putc(unsigned character);
unsigned platform_getc(void);
unsigned platform_try_getc(void);
void platform_clear(void);
void platform_shutdown(void);
void platform_reboot(void);
bool platform_has_mouse(void);
bool platform_mouse_pending(void);
bool platform_mouse_read(uint8_t packet[3]);
bool platform_read(unsigned sector, void *buffer);
bool platform_can_run(void);
bool platform_calculator_boot(void);
uint32_t platform_ram_kib(void);
void copy_to_program(unsigned offset, const void *source, unsigned length);
void copy_from_program(void *target, unsigned offset, unsigned length);
unsigned run_program(uint32_t directory, uint32_t entry, uint32_t stack);

#endif