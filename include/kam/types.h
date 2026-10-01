#ifndef KAM_TYPES_H
#define KAM_TYPES_H

/* KAM integer types. No stdint (freestanding). */

typedef unsigned char      kam_u8;
typedef unsigned short     kam_u16;
typedef unsigned int       kam_u32;
typedef unsigned long long kam_u64;

typedef signed char        kam_i8;
typedef signed short       kam_i16;
typedef signed int         kam_i32;
typedef signed long long   kam_i64;

typedef kam_u64 kam_usize;
typedef kam_u64 kam_uintn; /* UEFI UINTN: 64-bit on both targets */

#define KAM_NULL ((void *)0)

#define KAM_STATIC_ASSERT(cond, msg) typedef char kam_assert_##msg[(cond) ? 1 : -1]

KAM_STATIC_ASSERT(sizeof(kam_u8) == 1, u8_size);
KAM_STATIC_ASSERT(sizeof(kam_u16) == 2, u16_size);
KAM_STATIC_ASSERT(sizeof(kam_u32) == 4, u32_size);
KAM_STATIC_ASSERT(sizeof(kam_u64) == 8, u64_size);

#endif
