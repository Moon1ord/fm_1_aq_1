/* SPDX-License-Identifier: MPL-2.0 */
/* The break on the host: the decoder, the skeleton, TOP's rules, the player's joins. */
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "brk.h"

static int fails;
#define CHECK(c, ...)                                                                                                  \
    do {                                                                                                               \
        if (!(c)) {                                                                                                    \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);                                                                \
            printf(__VA_ARGS__);                                                                                       \
            printf("\n");                                                                                              \
            fails++;                                                                                                   \
        }                                                                                                              \
    } while (0)

static float out[4096];
static float peak, maxd, prev;
static void play(float bpm, float spacing, uint32_t steps, uint32_t from)
{
    float len = 44100.0f * 60.0f / (bpm * 4.0f) * spacing;
    uint32_t s;
    float left = 0.0f;
    for (s = 0; s < steps; s++) {
        uint32_t n;
        brk_step(from + s);
        left += len;
        n = (uint32_t)left;
        left -= (float)n;
        while (n) {
            uint32_t k = n > 256u ? 256u : n, j;
            memset(out, 0, sizeof(float) * k);
            brk_render(out, k);
            for (j = 0; j < k; j++) {
                float d = fabsf(out[j] - prev);
                maxd = d > maxd ? d : maxd;
                prev = out[j];
                peak = fabsf(out[j]) > peak ? fabsf(out[j]) : peak;
            }
            n -= k;
        }
    }
}

int main(void)
{
    uint32_t s;
    int k, st;
    static int16_t whole[BRK_NSAMPLES], part[BRK_SLICE];

    /* the decoder: the encoder's reconstruction, whole and from any slice */
    for (k = 0; k < 2; k++)
        for (st = 0; st < BRK_NSTEM; st++) {
            uint32_t sl;
            CHECK(brk_stem_hash(k, st) == brk_stem_expected(k, st), "kit %d stem %d hash", k, st);
            brk_decode_range(k, st, 0, BRK_NSAMPLES, whole);
            for (sl = 0; sl < BRK_NSTEP; sl++) {
                brk_decode_range(k, st, sl * BRK_SLICE, BRK_SLICE, part);
                CHECK(!memcmp(part, whole + sl * BRK_SLICE, sizeof part), "kit %d stem %d slice %u", k, st, sl);
            }
        }

    /* the skeleton never moves, in 10 000 bars at full Evolve, and the fill does happen */
    {
        int fills = 0;
        brk_init();
        brk_set_evolve(1.0f);
        brk_levels(1.0f, 1.0f, 1.0f);
        for (s = 0; s < 160000u; s++) {
            uint32_t bs = s % 16u;
            brk_step(s);
            if (bs == 0u || bs == 4u || bs == 10u || bs == 12u)
                CHECK(brk_st_core == (int8_t)(s % BRK_NSTEP), "step %u played %d", s, brk_st_core);
            else if (brk_st_core != (int8_t)(s % BRK_NSTEP)) {
                CHECK(bs >= 13u && s / 16u % 8u == 7u, "a fill outside a phrase's last steps, step %u", s);
                fills++;
            }
        }
        CHECK(fills > 1000, "fills %d", fills);
        printf("  fills in 10000 bars: %d\n", fills);
    }

    /* Evolve 0: no fill, TOP in order; Busy 0: about 40 % of TOP; Busy 1: all of it */
    {
        int on = 0;
        brk_init();
        brk_set_evolve(0.0f);
        brk_set_busy(0.0f);
        brk_levels(1.0f, 1.0f, 1.0f);
        for (s = 0; s < 32000u; s++) {
            brk_step(s);
            CHECK(brk_st_core == (int8_t)(s % BRK_NSTEP), "evolve 0: core moved at %u", s);
            if (brk_st_top >= 0) {
                on++;
                CHECK(brk_st_top == (int8_t)(s % BRK_NSTEP), "evolve 0: top moved at %u", s);
            }
        }
        CHECK(on > 11000 && on < 14500, "busy 0: %d of 32000", on);
        brk_set_busy(1.0f);
        for (s = 0; s < 3200u; s++) {
            brk_step(s);
            CHECK(brk_st_top >= 0, "busy 1: a step without top");
        }
    }

    /* TOP swaps keep the position in the beat */
    {
        int moved = 0;
        brk_init();
        brk_set_evolve(1.0f);
        brk_set_busy(1.0f);
        brk_levels(1.0f, 1.0f, 1.0f);
        for (s = 0; s < 32000u; s++) {
            brk_step(s);
            CHECK(brk_st_top >= 0 && (uint32_t)brk_st_top % 4u == s % 4u, "top %d at step %u", brk_st_top, s);
            moved += brk_st_top != (int8_t)(s % BRK_NSTEP);
        }
        CHECK(moved > 8000, "top moved %d", moved);
    }

    /* the joins: in order at any tempo in range, no crossfade after the first step */
    {
        static const float T[3] = {150.0f, 170.0f, 180.0f};
        for (k = 0; k < 3; k++) {
            brk_init();
            brk_set_evolve(0.0f);
            brk_set_busy(1.0f);
            brk_set_tempo(T[k]);
            brk_levels(1.0f, 1.0f, 1.0f);
            play(T[k], 1.0f, 1u, 0u);
            s = brk_jumps;
            play(T[k], 1.0f, 640u, 1u);
            CHECK(brk_jumps == s, "tempo %.0f: %u jumps playing in order", (double)T[k], brk_jumps - s);
        }
    }

    /* crossfades are no louder a click than the loop's own hits */
    {
        float straight;
        brk_init();
        brk_set_evolve(0.0f);
        brk_set_busy(1.0f);
        brk_levels(1.0f, 1.0f, 1.0f);
        maxd = prev = 0.0f;
        play(170.0f, 1.0f, 640u, 0u);
        straight = maxd;
        brk_init();
        brk_set_evolve(1.0f);
        brk_set_busy(1.0f);
        brk_levels(1.0f, 1.0f, 1.0f);
        maxd = prev = 0.0f;
        play(170.0f, 1.0f, 640u, 0u);
        CHECK(maxd < 1.5f * straight, "jump %f against %f", (double)maxd, (double)straight);
    }

    /* slower steps than the tempo: a slice ends and fades; the next slice's hit is not played early */
    {
        uint32_t d0;
        brk_init();
        brk_set_evolve(0.0f);
        brk_set_busy(1.0f);
        brk_levels(1.0f, 1.0f, 1.0f);
        play(170.0f, 1.0f, 1u, 0u);
        d0 = brk_decoded;
        play(170.0f, 3.0f, 1u, 1u);
        CHECK(brk_decoded - d0 < 2u * (BRK_SLICE + 200u) + 64u, "ran on: %u samples", brk_decoded - d0);
    }

    /* silent stems cost nothing; a level of zero is silence */
    {
        uint32_t d0;
        brk_init();
        brk_levels(0.0f, 0.0f, 1.0f);
        d0 = brk_decoded;
        peak = 0.0f;
        play(170.0f, 1.0f, 64u, 0u);
        CHECK(peak == 0.0f && brk_decoded == d0, "silent stems: peak %f, %u decoded", (double)peak, brk_decoded - d0);
        CHECK(brk_hits == 0u, "no hits while silent");
    }

    printf(fails ? "brk: %d FAILED\n" : "brk: ok\n", fails);
    return fails != 0;
}
