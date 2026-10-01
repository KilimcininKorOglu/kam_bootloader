#ifndef KAM_CPU_H
#define KAM_CPU_H

/* CPU primitives behind one roof: port I/O and halt.
 * GNU/Clang (including clang-cl) use inline asm; real MSVC on x64 uses
 * intrinsics. Real MSVC on ARM64 has no verified path yet and stops here
 * with a message instead of a guess. */

#include "types.h"

#if defined(_MSC_VER) && !defined(__clang__) && defined(_M_X64)
#include <intrin.h>
#define KAM_GNU_ASM 0
static inline void kam_cpu_outb(kam_u16 port, kam_u8 v) {
    __outbyte((unsigned short)port, v);
}
static inline kam_u8 kam_cpu_inb(kam_u16 port) {
    return (kam_u8)__inbyte((unsigned short)port);
}
static inline void kam_cpu_halt(void) {
    __halt();
}
#elif defined(_MSC_VER) && !defined(__clang__)
#error "KAM: this MSVC target needs porting (ARM64 likely wants __wfi)"
#else
#define KAM_GNU_ASM 1
#if defined(__x86_64__)
static inline void kam_cpu_outb(kam_u16 port, kam_u8 v) {
    __asm__ volatile("outb %0, %1" : : "a"(v), "Nd"(port));
}
static inline kam_u8 kam_cpu_inb(kam_u16 port) {
    kam_u8 v;
    __asm__ volatile("inb %1, %0" : "=a"(v) : "Nd"(port));
    return v;
}
static inline void kam_cpu_halt(void) {
    __asm__ volatile("hlt");
}
/* No port I/O on AArch64 by design; PL011 access is plain MMIO. */
#elif defined(__aarch64__)
static inline void kam_cpu_halt(void) {
    __asm__ volatile("wfi");
}
#else
#error "KAM: unknown arch for cpu primitives"
#endif
#endif

#endif
