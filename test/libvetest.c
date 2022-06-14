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
