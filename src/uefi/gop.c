/* KAM GOP header. Picks the first >=640x480 direct-color mode, sets it,
 * and paints art straight into the linear framebuffer. */

#include "kam/gop.h"
#include "kam/efi.h"

#define KAM_BG_R 0x0Au
#define KAM_BG_G 0x14u
#define KAM_BG_B 0x1Eu
#define KAM_BAR_R 0xB4u
#define KAM_BAR_G 0x5Au
#define KAM_BAR_B 0x00u
#define KAM_BAR_H 96u

static kam_u32 kam_pack(kam_u8 r, kam_u8 g, kam_u8 b, kam_u32 fmt) {
    if (fmt == 1) /* BGRR: byte0 = blue */
        return (kam_u32)b | ((kam_u32)g << 8) | ((kam_u32)r << 16);
    /* RGBR: byte0 = red */
    return (kam_u32)r | ((kam_u32)g << 8) | ((kam_u32)b << 16);
}

static void kam_fill(kam_u32 *fb, kam_u32 ppsl, kam_u32 x0, kam_u32 y0,
                     kam_u32 x1, kam_u32 y1, kam_u32 color) {
    kam_u32 y, x;
    for (y = y0; y < y1; y++) {
        for (x = x0; x < x1; x++)
            fb[(kam_usize)y * ppsl + x] = color;
    }
}

int kam_gop_draw(struct kam_boot_services *bs, kam_u32 *w_out,
                 kam_u32 *h_out) {
    kam_gop_t *gop = 0;
    kam_gop_info_t *info = 0;
    kam_usize info_size = 0;
    kam_u32 m, mw = 0, mh = 0, mppsl = 0, mfmt = 3;
    kam_u32 *fb;
    kam_u32 bg, bar, white;
    kam_u32 i;

    if (!bs || !bs->locateproto)
        return -1;
    if (KAM_EFI_ERROR(bs->locateproto(&KAM_GUID_GOP, 0, (void **)&gop)) ||
        !gop || !gop->query || !gop->set_mode || !gop->mode)
        return -2;

    /* First direct-color mode at >= 640x480. */
    for (m = 0; m < gop->mode->maxmode; m++) {
        info = 0;
        info_size = 0;
        if (KAM_EFI_ERROR(gop->query(gop, m, &info_size, &info)) || !info)
            continue;
        if ((info->fmt == 0 || info->fmt == 1) && info->w >= 640 &&
            info->h >= 480) {
            mw = info->w;
            mh = info->h;
            mppsl = info->ppsl;
            mfmt = info->fmt;
            break;
        }
    }
    if (mw == 0)
        return -3;
    if (KAM_EFI_ERROR(gop->set_mode(gop, m)))
        return -4;
    /* Re-read mode: SetMode may change stride/base. */
    info = gop->mode->info;
    if (!info)
        return -5;
    mw = info->w;
    mh = info->h;
    mppsl = info->ppsl;
    mfmt = info->fmt;
    if ((mfmt != 0 && mfmt != 1) || gop->mode->fb_base == 0)
        return -6;
    fb = (kam_u32 *)gop->mode->fb_base;

    bg = kam_pack(KAM_BG_R, KAM_BG_G, KAM_BG_B, mfmt);
    bar = kam_pack(KAM_BAR_R, KAM_BAR_G, KAM_BAR_B, mfmt);
    white = kam_pack(0xFF, 0xFF, 0xFF, mfmt);

    kam_fill(fb, mppsl, 0, 0, mw, mh, bg);
    kam_fill(fb, mppsl, 0, 0, mw, KAM_BAR_H, bar);
    /* White frame, 2px. */
    kam_fill(fb, mppsl, 0, 0, mw, 2, white);
    kam_fill(fb, mppsl, 0, mh - 2, mw, mh, white);
    kam_fill(fb, mppsl, 0, 0, 2, mh, white);
    kam_fill(fb, mppsl, mw - 2, 0, mw, mh, white);
    /* Three ascending signal bars inside the header. */
    for (i = 0; i < 3; i++) {
        kam_u32 x0 = 20 + i * 20;
        kam_u32 top = KAM_BAR_H - 8 - (24 + i * 16);
        kam_fill(fb, mppsl, x0, top, x0 + 10, KAM_BAR_H - 8, white);
    }

    *w_out = mw;
    *h_out = mh;
    return 1;
}
