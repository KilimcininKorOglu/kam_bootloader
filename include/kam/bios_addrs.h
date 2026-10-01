#ifndef KAM_BIOS_ADDRS_H
#define KAM_BIOS_ADDRS_H

/* Fixed physical addresses of the BIOS boot path.
 * stage2.asm mirrors these with %define (kept in sync by hand, checked
 * by the size asserts in the Makefile). */

#define KAM_STAGE2_LOAD   0x8000u  /* MBR loads 16 sectors (8KB) here */
#define KAM_TRAMP_SIZE    2048u    /* nasm trampoline, padded with TIMES */
#define KAM_PAYLOAD_BASE  0x8800u  /* C payload linked exactly here */
#define KAM_PAYLOAD_MAX   6144u    /* payload.bin must fit in the rest */
#define KAM_E820_BASE     0x5000u  /* kam_memmap_t filled by stage2.asm */
#define KAM_PML4_BASE     0x10000u /* 3 pages: PML4 + PDPT + PD (2MB pages) */
#define KAM_STACK64       0x90000u /* 64-bit stack top (grows down) */

#endif
