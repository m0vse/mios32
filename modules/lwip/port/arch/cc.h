#ifndef LWIP_ARCH_CC_H
#define LWIP_ARCH_CC_H

#include <mios32.h>
#include <stdint.h>
#include <stdio.h>

#define LWIP_PLATFORM_DIAG(x) do { printf x; } while (0)
#define LWIP_PLATFORM_ASSERT(x) do { printf("lwIP assertion failed: %s\n", x); } while (0)
#define BYTE_ORDER LITTLE_ENDIAN

#define LWIP_NO_STDINT_H 1
#define LWIP_NO_INTTYPES_H 1

typedef s8  s8_t;
typedef u8  u8_t;
typedef s16 s16_t;
typedef u16 u16_t;
typedef s32 s32_t;
typedef u32 u32_t;
typedef uintptr_t mem_ptr_t;

#define X8_F  "02x"
#define U16_F "u"
#define S16_F "d"
#define X16_F "x"
#define U32_F "u"
#define S32_F "d"
#define X32_F "x"
#define SZT_F "u"

#endif
