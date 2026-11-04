/* SPDX-License-Identifier: MPL-2.0 */
/* The art in flash (art_data.h, tools/gen_art.py): a picture is rows of runs, a byte each: the run's length - 1 in the high five bits, the tone in the low
 * three. Rows start afresh, so a strip or a small rectangle starts anywhere and nothing is held in RAM. */
#pragma once
#include <stdint.h>
typedef struct {
    const uint8_t *d;                  /* the runs */
    const uint16_t *r;                 /* where each row starts in them; one more at the end */
    uint8_t x, y, w, h;                /* where it stands on the screen (a patch: a small rectangle) */
    uint32_t crc;                      /* of its tones: tests/host/art_test.c */
} art_t;

/* its row number `row` (0 is its top) into dst, where dst[0] is screen column xa and the columns xa .. xb - 1 are wanted; lut: a colour a tone. It reads the
 * flash only as far as xb. Answers the bytes read */
static uint32_t art_row(const art_t *a, uint32_t row, uint16_t *dst, int32_t xa, int32_t xb, const uint16_t *lut)
{
    const uint8_t *p = a->d + a->r[row], *p0 = p;
    int32_t x = a->x, end = x + a->w;
    if (xb > end)
        xb = end;
    while (x < xb) {
        const uint32_t b = *p++;
        const int32_t e = x + (int32_t)(b >> 3) + 1;
        const uint16_t c = lut[b & 7u];
        int32_t i = x < xa ? xa : x;
        const int32_t stop = e < xb ? e : xb;
        for (; i < stop; i++)
            dst[i - xa] = c;
        x = e;
    }
    return (uint32_t)(p - p0);
}
