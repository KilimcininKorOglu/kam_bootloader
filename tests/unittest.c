/* KAM host unit tests. Compiles the freestanding sources with the host
 * toolchain and checks pure logic: SHA-256 vectors, ELF prepare/commit,
 * config parsing, ISO probing. Firmware-bound code (menu, boot services)
 * is covered by the QEMU matrix instead. */

#include <stdio.h>
#include <string.h>

#include "kam/elf.h"
#include "kam/config.h"
#include "kam/iso.h"
#include "kam/sha256.h"

static int fails;

#define CHECK(c)                                                           \
    do {                                                                   \
        if (!(c)) {                                                        \
            printf("FAIL %d: %s\n", __LINE__, #c);                         \
            fails++;                                                       \
        }                                                                  \
    } while (0)

static void put_noop(const char *s) {
    (void)s;
}

static void putc_noop(char c) {
    (void)c;
}

static void putu_noop(kam_u64 v) {
    (void)v;
}

static void test_sha(void) {
    static const kam_u8 v112[] =
        "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"
        "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
    static const kam_u8 e112[32] = {
        0x59, 0xF1, 0x09, 0xD9, 0x53, 0x3B, 0x2B, 0x70, 0xE7, 0xC3,
        0xB8, 0x14, 0xA2, 0xBD, 0x21, 0x8F, 0x78, 0xEA, 0x5D, 0x37,
        0x14, 0x45, 0x5B, 0xC6, 0x79, 0x87, 0xCF, 0x0D, 0x66, 0x43,
        0x99, 0xCF};
    kam_u8 out[32];
    kam_sha256_once((const kam_u8 *)"", 0, out);
    CHECK(out[0] == 0xE3 && out[31] == 0x55);
    kam_sha256_once((const kam_u8 *)"abc", 3, out);
    CHECK(out[0] == 0xBA && out[31] == 0xAD);
    kam_sha256_once(v112, sizeof(v112) - 1, out);
    CHECK(memcmp(out, e112, 32) == 0);
    CHECK(kam_ct_eq(e112, e112, 32) == 1);
    CHECK(kam_ct_eq(e112, out, 0) == 1);
    out[0] ^= 1;
    CHECK(kam_ct_eq(e112, out, 32) == 0);
}

static void elf_img(kam_u8 *b, kam_usize n, int good) {
    kam_usize i;
    for (i = 0; i < n; i++)
        b[i] = 0;
    b[0] = 0x7F;
    b[1] = 'E';
    b[2] = 'L';
    b[3] = good ? 'F' : 'X';
    b[4] = 2;
    b[5] = 1;
    b[16] = 2; /* ET_EXEC */
    b[24] = 0x08; /* entry 0x100008: inside the LOAD segment */
    b[26] = 0x10;
    b[32] = 0x40; /* phoff */
    b[54] = 56;   /* phentsize */
    b[56] = 1;    /* phnum */
    /* phdr at 64: LOAD, offset 128, vaddr+paddr 0x100000, file 16, mem 32 */
    b[64] = 1;
    b[72] = 128;
    b[80] = 0x00;
    b[81] = 0x00;
    b[82] = 0x10;
    b[88] = 0x00;
    b[89] = 0x00;
    b[90] = 0x10;
    b[96] = 16;
    b[104] = 32;
    b[128] = 'K';
}

static void test_elf(void) {
    kam_u8 img[256];
    kam_seg_t segs[KAM_ELF_MAXSEG];
    kam_usize nseg = 0;
    kam_u64 e;
    kam_u8 mem[0x100020];

    elf_img(img, sizeof(img), 1);
    e = kam_elf_prepare(img, sizeof(img), segs, &nseg);
    CHECK(e == 0x100008u);
    CHECK(nseg == 1);
    CHECK(segs[0].paddr == 0x100000u);
    CHECK(segs[0].filesz == 16 && segs[0].memsz == 32);
    for (nseg = 0; nseg < sizeof(mem); nseg++)
        mem[nseg] = 0xCC;
    nseg = 0;
    e = kam_elf_prepare(img, sizeof(img), segs, &nseg);
    /* commit into a scratch window (low offsets stand in for paddrs) */
    {
        kam_seg_t fake[1];
        fake[0].paddr = (kam_u64)(kam_usize)(mem + 0x20);
        fake[0].filesz = 16;
        fake[0].memsz = 32;
        fake[0].offset = 128;
        kam_elf_commit(img, fake, 1);
        CHECK(mem[0x20] == 'K');
        CHECK(mem[0x20 + 16] == 0 && mem[0x20 + 31] == 0);
    }
    elf_img(img, sizeof(img), 0);
    nseg = 0;
    CHECK(kam_elf_prepare(img, sizeof(img), segs, &nseg) == 0);
    elf_img(img, sizeof(img), 1);
    img[96] = 200; /* filesz 200 > memsz 32 */
    nseg = 0;
    CHECK(kam_elf_prepare(img, sizeof(img), segs, &nseg) == 0);
    elf_img(img, sizeof(img), 1);
    CHECK(kam_elf_prepare(img, 64, segs, &nseg) == 0); /* truncated */
    elf_img(img, sizeof(img), 1);
    img[24] = 0x40; /* entry outside any segment */
    img[26] = 0x00;
    nseg = 0;
    CHECK(kam_elf_prepare(img, sizeof(img), segs, &nseg) == 0);
    elf_img(img, sizeof(img), 1);
    img[56] = 2; /* phnum: second LOAD overlaps the first */
    img[120] = 1; /* type LOAD */
    img[128] = 144; /* offset */
    img[144] = 0x08; /* paddr 0x100008, inside first seg */
    img[146] = 0x10;
    img[152] = 16; /* filesz */
    img[160] = 16; /* memsz */
    nseg = 0;
    CHECK(kam_elf_prepare(img, sizeof(img), segs, &nseg) == 0);
}

static void test_config(void) {
    static const char *ini =
        "timeout 1\ndefault 2\n"
        "password_salt S\n"
        "password_hash 9f86d081884c7d659a2feaa0c55ad015a3bf4f1b2b0b822cd15d6c15b0f00av\n"
        "[kernel]\nlabel K\npath \\KAM\\KERNEL.ELF\n"
        "[chain]\nlabel H\npath KAM/HELLO.EFI\n"
        "[bogus]\nlabel X\npath \\Y\n"
        "[iso]\nlabel no path\n"
        "[iso]\npath \\KAM\\TEST.ISO\n"
        "[linux]\nlabel L\npath \\KAM\\VMLINUZ\ninitrd \\KAM\\I\ncmdline ab cd\n";
    static kam_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    kam_config_parse((const kam_u8 *)ini, strlen(ini), &cfg);
    CHECK(cfg.count == 4);
    CHECK(cfg.timeout == 1 && cfg.def == 1);
    CHECK(cfg.has_pw == 0); /* 65-char hash rejected */
    CHECK(cfg.entries[0].kind == KAM_ENTRY_ELF);
    CHECK(cfg.entries[1].kind == KAM_ENTRY_EFI);
    CHECK(cfg.entries[1].path[0] == (kam_char16)'\\');
    CHECK(cfg.entries[2].kind == KAM_ENTRY_ISO);
    CHECK(cfg.entries[3].kind == KAM_ENTRY_LINUX);
    CHECK(strcmp(cfg.entries[3].cmdline, "ab cd") == 0);
    {
        static const char *ini2 = "password_salt S\n"
                                  "password_hash "
                                  "9f86d081884c7d659a2feaa0c55ad015a3bf4f1b"
                                  "2b0b822cd15d6c15b0f00a08\n";
        memset(&cfg, 0, sizeof(cfg));
        kam_config_parse((const kam_u8 *)ini2, strlen(ini2), &cfg);
        CHECK(cfg.has_pw == 1);
        CHECK(strcmp(cfg.pw_salt, "S") == 0);
        CHECK(cfg.pw_hash[0] == 0x9F && cfg.pw_hash[31] == 0x08);
    }
    {
        memset(&cfg, 0xAA, sizeof(cfg));
        kam_config_parse((const kam_u8 *)"", 0, &cfg);
        CHECK(cfg.count == 0 && cfg.timeout == 5 && cfg.has_pw == 0);
    }
}

static void test_iso_file(const char *path, int want_ok) {
    FILE *f = fopen(path, "rb");
    long n;
    static kam_u8 img[128 * 1024];
    int rc;
    if (!f) {
        printf("SKIP (missing %s)\n", path);
        return;
    }
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n <= 0 || n > (long)sizeof(img)) {
        fclose(f);
        printf("SKIP (bad size %s)\n", path);
        return;
    }
    if (fread(img, 1, (size_t)n, f) != (size_t)n) {
        fclose(f);
        printf("SKIP (read %s)\n", path);
        return;
    }
    fclose(f);
    rc = kam_iso_boot_report(img, (kam_usize)n, put_noop, putc_noop,
                             putu_noop);
    CHECK((rc == 0) == (want_ok != 0));
    if (want_ok && n > 16 * 2048 + 64) {
        /* Corrupt the PVD magic and the catalog checksum: must fail. */
        kam_u8 save = img[16 * 2048 + 1];
        img[16 * 2048 + 1] = 'X';
        CHECK(kam_iso_boot_report(img, (kam_usize)n, put_noop, putc_noop,
                                  putu_noop) != 0);
        img[16 * 2048 + 1] = save;
        CHECK(kam_iso_boot_report(img, (kam_usize)n, put_noop, putc_noop,
                                  putu_noop) == 0);
    }
}

int main(void) {
    test_sha();
    test_elf();
    test_config();
    test_iso_file("build/test.iso", 1);
    if (fails == 0)
        printf("unittest: all passed\n");
    else
        printf("unittest: %d failures\n", fails);
    return fails != 0;
}
