/* SPDX-License-Identifier: MPL-2.0 */
/* The art as it is kept: every row decodes to its width, cut anywhere it gives the same pixels and reads no further than asked, the tones are what the generator
 * saw (a CRC), the patches stand where nothing kept in RAM is, the size. */
#include <stdio.h>
#include <string.h>
#include "art.h"
#include "art_data.h"

static uint32_t crc32_(uint32_t c, const uint8_t *p, size_t n)
{
    size_t i;
    int k;
    c = ~c;
    for (i = 0; i < n; i++) {
        c ^= p[i];
        for (k = 0; k < 8; k++)
            c = c >> 1 ^ (0xEDB88320u & (0u - (c & 1u)));
    }
    return ~c;
}

int main(void)
{
    static const uint16_t ID[8] = {0, 1, 2, 3, 4, 5, 6, 7};
    int fails = 0, i, row;
    size_t total = 0;
    for (i = 0; i < ART_N; i++) {
        const art_t *a = &ART[i];
        uint32_t crc = 0;
        uint8_t all[240 * 240];
        for (row = 0; row < a->h; row++) {
            uint16_t px[240], cut[240];
            int j, x0, x1;
            uint32_t rd;
            memset(px, 0xFF, sizeof px);
            rd = art_row(a, (uint32_t)row, px, a->x, a->x + a->w, ID);
            if (rd != (uint32_t)(a->r[row + 1] - a->r[row])) {
                printf("FAIL art %d row %d: read %u of %d\n", i, row, rd, a->r[row + 1] - a->r[row]);
                fails++;
            }
            for (j = 0; j < a->w; j++) {
                if (px[j] > 7u) {
                    printf("FAIL art %d row %d: pixel %d not filled\n", i, row, j);
                    fails++;
                }
                all[row * a->w + j] = (uint8_t)px[j];
            }
            for (x0 = 0; x0 < a->w; x0 += 37) {          /* a rectangle's piece of the row: the same pixels, the rest untouched */
                x1 = x0 + 23 > a->w ? a->w : x0 + 23;
                memset(cut, 0xEE, sizeof cut);
                art_row(a, (uint32_t)row, cut, a->x + x0, a->x + x1, ID);
                if (memcmp(cut, px + x0, (size_t)(x1 - x0) * 2u) || (x1 - x0 < 240 && cut[x1 - x0] != 0xEEEEu)) {
                    printf("FAIL art %d row %d: cut at %d differs\n", i, row, x0);
                    fails++;
                }
            }
        }
        crc = crc32_(0u, all, (size_t)a->w * a->h);
        if (crc != a->crc) {
            printf("FAIL art %d: tones %08x, the generator's %08x\n", i, crc, a->crc);
            fails++;
        }
        total += a->r[a->h] + (a->h + 1u) * 2u;
    }
    for (i = ART_EYE_HALF; i < ART_N; i++)              /* (ui.c: rows 0-27, 178-239 and the meters' rectangle are kept decoded) */
        if (ART[i].y < 28 || ART[i].y + ART[i].h > 178 || (ART[i].x + ART[i].w > 206 && ART[i].y + ART[i].h > 146)) {
            printf("FAIL patch %d stands on what is kept\n", i);
            fails++;
        }
    if (total != ART_BYTES || total > 200000u) {
        printf("FAIL art: %zu bytes, header says %u, allowed 200000\n", total, ART_BYTES);
        fails++;
    }
    printf("art: %d pictures, %zu bytes\n", ART_N, total);
    return fails != 0;
}
