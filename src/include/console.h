#ifndef DDOS_CONSOLE_H
#define DDOS_CONSOLE_H

#include <stdint.h>

void print(const char *text);
void number(uint32_t value);
void print_bytes(const uint8_t *data, unsigned size);

#endif