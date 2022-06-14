#ifndef __CRC32_H__
#define __CRC32_H__

#include <stddef.h>
#include <stdint.h>

uint32_t crc32_for_byte(uint32_t byte)
{
    const uint32_t polynomial = 0xEDB88320L;
    uint32_t result = byte;

    for (size_t i = 0; i < 8; i++) {
        result = (result >> 1) ^ (result & 1) * polynomial;
    }
    return result;
}

uint32_t crc32(const uint8_t *input, size_t size)
{
    const uint8_t *current = input;
    uint32_t result = 0xffffffff;

    for (size_t i = 0; i < size; i++) {
        result ^= current[i];
        result = crc32_for_byte(result);
    }

    return ~result;
}

#endif
