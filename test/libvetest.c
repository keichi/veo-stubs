#include <signal.h>
#include <stdio.h>

#include "crc32.h"

uint64_t increment(uint64_t i) { return i + 1; }

uint64_t checksum(uint64_t ptr, uint64_t size)
{
    return crc32((uint8_t *)ptr, size);
}

uint64_t iota(uint64_t ptr, uint64_t size)
{
    uint8_t *arr = (uint8_t *)ptr;
    uint8_t x = 0;

    for (int64_t i = 0; i < size; i++) {
        *(arr++) = x++;
    }

    return 0;
}

uint64_t raise_sigabrt()
{
    raise(SIGABRT);
    return 0;
}

uint64_t add1(const int32_t *a, const int32_t *b) { return *a + *b; }

uint64_t add2(int32_t *sum, const int32_t *a, const int32_t *b)
{
    *sum = *a + *b;

    return 0;
}

uint64_t add3(int32_t *sum, const int32_t *a)
{
    printf("a: %p *a: %d\n", a, *a);
    printf("sum: %p *sum: %d\n", sum, *sum);

    *sum += *a;

    return 0;
}
