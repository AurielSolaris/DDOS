#ifndef DDOS_H
#define DDOS_H

#include <stddef.h>
#include <stdint.h>

enum {
    O_RDONLY = 0, O_WRONLY = 1, O_RDWR = 2, O_CREAT = 0x40, O_EXCL = 0x80,
    O_TRUNC = 0x200, O_APPEND = 0x400, SEEK_SET = 0, SEEK_CUR = 1, SEEK_END = 2,
};

struct iovec {
    const void *iov_base;
    size_t iov_len;
};

extern int errno;
long ddos_syscall(unsigned, uintptr_t, uintptr_t, uintptr_t,
                  uintptr_t, uintptr_t, uintptr_t);
_Noreturn void _exit(int status);
long read(int fd, void *buffer, size_t count);
long write(int fd, const void *buffer, size_t count);
long writev(int fd, const struct iovec *entries, unsigned count);
int open(const char *name, int flags, ...);
int close(int fd);
int64_t lseek(int fd, int64_t offset, int origin);
void *sbrk(intptr_t increment);
int ddos_tls(void *base, size_t bytes);
void *ddos_map(size_t length);
int munmap(void *address, size_t length);
size_t strlen(const char *text);
void *memcpy(void *to, const void *from, size_t count);
void *memmove(void *to, const void *from, size_t count);
void *memset(void *to, int value, size_t count);

#endif