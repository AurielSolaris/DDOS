#include "ddos.h"
#include <stdarg.h>

int errno;

static long checked(long result)
{
    if (result < 0) {
        errno = -result;
        return -1;
    }
    return result;
}

_Noreturn void _exit(int status)
{
    ddos_syscall(1, status, 0, 0, 0, 0, 0);
    __builtin_trap();
}

long read(int fd, void *buffer, size_t count)
{
    return checked(ddos_syscall(3, fd, (uintptr_t)buffer, count, 0, 0, 0));
}

long write(int fd, const void *buffer, size_t count)
{
    return checked(ddos_syscall(4, fd, (uintptr_t)buffer, count, 0, 0, 0));
}

long writev(int fd, const struct iovec *entries, unsigned count)
{
    return checked(ddos_syscall(146, fd, (uintptr_t)entries, count, 0, 0, 0));
}

int open(const char *name, int flags, ...)
{
    unsigned mode = 0;
    if (flags & O_CREAT) {
        va_list arguments;
        va_start(arguments, flags);
        mode = va_arg(arguments, int);
        va_end(arguments);
    }
    return checked(ddos_syscall(295, -100, (uintptr_t)name, flags, mode, 0, 0));
}

int close(int fd)
{
    return checked(ddos_syscall(6, fd, 0, 0, 0, 0, 0));
}

int64_t lseek(int fd, int64_t offset, int origin)
{
    int64_t result;
    if (checked(ddos_syscall(140, fd, (uint64_t)offset >> 32, (uint32_t)offset,
                             (uintptr_t)&result, origin, 0)) < 0)
        return -1;
    return result;
}

int ddos_tls(void *base, size_t bytes)
{
    if (!bytes) {
        errno = 22;
        return -1;
    }
    uint32_t descriptor[] = {11, (uintptr_t)base, bytes - 1, 0x41};
    if (checked(ddos_syscall(243, (uintptr_t)descriptor, 0, 0, 0, 0, 0)) < 0)
        return -1;
    uint16_t selector = (descriptor[0] << 3) | 2;
    __asm__ volatile ("movw %0, %%gs" :: "rm"(selector) : "memory");
    return 0;
}

void *sbrk(intptr_t increment)
{
    uintptr_t previous = ddos_syscall(45, 0, 0, 0, 0, 0, 0);
    uintptr_t next = previous + (uintptr_t)increment;
    if (!next || (uintptr_t)ddos_syscall(45, next, 0, 0, 0, 0, 0) != next) {
        errno = 12;
        return (void *)-1;
    }
    return (void *)previous;
}

void *ddos_map(size_t length)
{
    return (void *)(uintptr_t)checked(ddos_syscall(192, 0, length, 3, 0x22, -1, 0));
}

int munmap(void *address, size_t length)
{
    return checked(ddos_syscall(91, (uintptr_t)address, length, 0, 0, 0, 0));
}

size_t strlen(const char *text)
{
    size_t length = 0;
    while (text[length])
        ++length;
    return length;
}

void *memcpy(void *to, const void *from, size_t count)
{
    unsigned char *dest = to;
    const unsigned char *source = from;
    for (size_t i = 0; i < count; ++i)
        dest[i] = source[i];
    return to;
}

void *memmove(void *to, const void *from, size_t count)
{
    if ((uintptr_t)to < (uintptr_t)from)
        return memcpy(to, from, count);
    unsigned char *dest = to;
    const unsigned char *source = from;
    while (count) {
        --count;
        dest[count] = source[count];
    }
    return to;
}

void *memset(void *to, int value, size_t count)
{
    unsigned char *dest = to;
    for (size_t i = 0; i < count; ++i)
        dest[i] = value;
    return to;
}