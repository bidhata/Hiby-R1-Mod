#ifndef GB_TYPES_H
#define GB_TYPES_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef int8_t s8;
typedef int16_t s16;
typedef int32_t s32;

#define GB_WIDTH  160
#define GB_HEIGHT 144

#define CYCLES_PER_FRAME 70224
#define T_CYCLES_PER_FRAME (CYCLES_PER_FRAME / 4)

#endif
