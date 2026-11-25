#include "ddos.h"

int main(void)
{
    static const char message[] = "Hello from a Ring 2 C program.";
    char *buffer = sbrk(sizeof(message));
    if (buffer == (void *)-1)
        return 1;
    memcpy(buffer, message, sizeof(message));
    const struct iovec output[] = {{buffer, strlen(buffer)}, {"\r\n", 2}};
    return writev(1, output, 2) < 0;
}