#ifndef DDOS_DOS_H
#define DDOS_DOS_H

#include <stdbool.h>
#include <stdint.h>

bool dos_run(const char *name, const char *arguments, uint32_t directory);

#endif