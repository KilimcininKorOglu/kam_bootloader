/* KAM bzImage test fixture. A flat binary speaking enough of the x86
 * Linux boot protocol for our loader to prove load + params + jump:
 * boot flag, HdrS magic, version with handover, handover offset to a
 * 64-bit C entry. BSS-free by construction (raw copy has no loader). */

#include "kam/types.h"
#include "kam/memmap.h"
#include "kam/raw_serial.h"
#include "kam/bzimage.h"

/* Setup area: boot sector (AA55 at 510) plus the header sector carrying
 * HdrS/version/handover at their documented file offsets. */
__attribute__((section(".setup"), used)) static const kam_u8 vmlinuz_setup[1024] = {
    [510] = 0x55,
    [511] = 0xAA,
    [0x202] = 'H',
    [0x203] = 'd',
    [0x204] = 'r',
    [0x205] = 'S',
    [0x206] = 0x0C,
    [0x207] = 0x02,
    /* handover_offset = 0x1000 (.text base): patched here as bytes. */
    [0x264] = 0x00,
    [0x265] = 0x10,
    [0x266] = 0x00,
    [0x267] = 0x00,
};

static int kam_streq16(const kam_u8 *a, const char *b) {
    kam_usize i = 0;
    while (b[i]) {
        if (a[i] != (kam_u8)b[i])
            return 0;
        i++;
    }
    return 1;
}

/* Entered via the handover jump. The params address arrives through a
 * fixed mailbox (robust across toolchains); register-level handover ABI
 * conformance is verified against real kernels as a follow-up. */
#define KAM_BZ_MBOX_PTR ((volatile kam_u64 *)(kam_usize)KAM_BZ_MBOX)

void vmlinuz_entry(void) {
    const kam_u8 *params = (const kam_u8 *)(kam_usize)*KAM_BZ_MBOX_PTR;
    kam_u8 n;
    kam_u32 cmd_ptr;
    kam_u64 i;

    kam_raw_serial_init();
    if (!params)
        kam_raw_halt();
    n = params[0x1E8];
    cmd_ptr = (kam_u32)params[0x228] | ((kam_u32)params[0x229] << 8) |
              ((kam_u32)params[0x22A] << 16) | ((kam_u32)params[0x22B] << 24);
    if (n == 0 || cmd_ptr == 0)
        kam_raw_halt();
    if (!kam_streq16((const kam_u8 *)(kam_usize)cmd_ptr, "kam-test"))
        kam_raw_halt();
    kam_raw_puts("KAM-BZIMAGE e820=");
    kam_raw_put_u64(n);
    kam_raw_puts(" cmd=");
    for (i = 0; i < 32; i++) {
        char c = ((const char *)(kam_usize)cmd_ptr)[i];
        if (!c)
            break;
        kam_raw_putc(c);
    }
    kam_raw_puts("\nKAM-BZIMAGE halting.\n");
    kam_raw_halt();
}
