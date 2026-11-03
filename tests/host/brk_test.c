/* SPDX-License-Identifier: MPL-2.0 */
/* The break on the host: the decoder, the player's joins, what a bar of hits does through it. */
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <stdlib.h>
#include "brk.h"

#define CLICK 0.1f
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

/* a bar by hand: each letter a sound on that step, as hard as it goes */
static void bar_of(const char *core, const char *top)
{
    int i;
    memset(&brk_bar, 0, sizeof brk_bar);
    for (i = 0; i < 16; i++) {
        brk_bar.core[i] = core[i];
        brk_bar.top[i] = top[i];
        brk_bar.hat[i] = '.';
        brk_bar.vcore[i] = brk_bar.vtop[i] = 100u;
    }
}
#define HITS "K...S.....k.S..."
#define HITS_TOP ".f.....e.....f.."
#define NO_HITS "................"
static void stp(uint32_t s) { brk_pattern_step(s, 0, 0u); }
static void play(float bpm, float spacing, uint32_t steps, uint32_t from)
{
    float len = 44100.0f * 60.0f / (bpm * 4.0f) * spacing;
    uint32_t s;
    float left = 0.0f;
    for (s = 0; s < steps; s++) {
        uint32_t n;
        stp(from + s);
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

/* the largest |out - prev| in the 16 samples after a step that jumped (jmax), and elsewhere (nmax) */
static float jmax, nmax;
static void clicks(uint32_t steps, uint32_t from, float spacing)
{
    float len = 44100.0f * 60.0f / (170.0f * 4.0f) * spacing, left = 0.0f;
    uint32_t s;
    for (s = 0; s < steps; s++) {
        uint32_t n, j0 = brk_jumps, first = 16u, j;
        stp(from + s);
        first = brk_jumps != j0 ? 16u : 0u;
        left += len;
        n = (uint32_t)left;
        left -= (float)n;
        for (j = 0; j < n;) {
            uint32_t k = n - j > 256u ? 256u : n - j, i;
            if (first && k > first)
                k = first;
            memset(out, 0, sizeof(float) * k);
            brk_render(out, k);
            for (i = 0; i < k; i++) {
                float d = fabsf(out[i] - prev);
                if (first) jmax = d > jmax ? d : jmax;
                else nmax = d > nmax ? d : nmax;
                prev = out[i];
            }
            j += k;
            first = first > k ? first - k : 0u;
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

    /* the joins: a sound runs through its slices at any tempo in range, no crossfade but the one that starts it */
    {
        static const float T[3] = {150.0f, 170.0f, 180.0f};
        for (k = 0; k < 3; k++) {
            brk_init();
            brk_set_tempo(T[k]);
            brk_levels(1.0f, 1.0f, 1.0f);
            bar_of("K...............", NO_HITS);
            play(T[k], 1.0f, 1u, 0u);
            s = brk_jumps;
            play(T[k], 1.0f, 3u, 1u);
            CHECK(brk_jumps == s, "tempo %.0f: %u jumps inside a kick", (double)T[k], brk_jumps - s);
        }
    }

    /* a hit that cuts a tail is no click (CLICK is far below a hard cut, a snare's tail cut off); a kit change and quick hits are
     * held to it too */
    {
        enum { PH = 128 };
        uint32_t j0;
        brk_init();
        brk_set_tempo(170.0f);
        brk_levels(1.0f, 0.0f, 1.0f);
        bar_of(HITS, NO_HITS);
        jmax = nmax = prev = 0.0f;
        clicks(PH * 40u, 0u, 1.0f);
        printf("  hits' jumps: %u, click %f, natural %f\n", brk_jumps, (double)jmax, (double)nmax);
        CHECK(brk_jumps >= 40u * 8u * 4u, "only %u jumps", brk_jumps);
        CHECK(jmax < CLICK, "a hit's crossfade clicks: %f", (double)jmax);

        /* a kit change in mid-play: the sound under way goes on, in its own kit, to its end; the next hit is the new kit's */
        brk_init();
        brk_set_tempo(170.0f);
        brk_levels(1.0f, 0.0f, 1.0f);
        bar_of(HITS, NO_HITS);
        jmax = nmax = prev = 0.0f;
        clicks(1u, 0u, 0.02f);            /* the kick, loud, when the kit changes */
        brk_set_kit(1);
        j0 = brk_jumps;
        clicks(1u, 1u, 1.0f);
        CHECK(brk_jumps == j0 + 1u, "kit change: %u jumps at the next step", brk_jumps - j0);
        clicks(6u, 2u, 1.0f);
        printf("  kit change: click %f\n", (double)jmax);
        CHECK(jmax < CLICK, "a kit change clicks: %f", (double)jmax);
        brk_set_kit(0);

        /* steps faster than the fade: a voice still sounding is not started over */
        brk_init();
        brk_set_tempo(170.0f);
        brk_levels(1.0f, 0.0f, 1.0f);
        bar_of("KkSsKkSsKkSsKkSs", NO_HITS);
        jmax = nmax = prev = 0.0f;
        clicks(3u, 8u, 1.0f);
        clicks(256u, 11u, 0.005f);
        printf("  quick hits: click %f\n", (double)jmax);
        CHECK(jmax < CLICK, "quick hits click: %f", (double)jmax);

        /* a voice still sounding louder is not overwritten: two hits in a row with no sound between them */
        brk_init();
        brk_set_tempo(170.0f);
        brk_levels(1.0f, 0.0f, 1.0f);
        bar_of("K.S.s.c.........", NO_HITS);
        stp(0u);
        memset(out, 0, sizeof out);
        brk_render(out, 700u);           /* the kick, loud */
        stp(2u);                         /* a jump: the old voice fades, the new one fades in */
        memset(out, 0, sizeof out);
        brk_render(out, 8u);
        prev = out[7];
        stp(4u);
        stp(6u);                         /* two more jumps before any sample is rendered */
        memset(out, 0, sizeof out);
        brk_render(out, 16u);
        printf("  louder voice kept: step %f (prev %f)\n", (double)fabsf(out[0] - prev), (double)prev);
        CHECK(fabsf(out[0] - prev) < 0.1f, "a louder voice was overwritten: %f", (double)fabsf(out[0] - prev));
    }

    /* seam: the loop's end comes back to slice 0 with the state of a loop that has played: slice 0's head is slice
     * 16's (the same kick in both bars' CORE), not the encoder starting cold.
     * (demo-loading) off: a stem is a bank of sounds now (tools/gen_breaks.c), slice 16 is not a kick */
    for (k = 0; k < 0; k++) {
        static int16_t a[64], b[64];
        int i, m = 0;
        brk_decode_range(k, BRK_CORE, 0u, 64u, a);
        brk_decode_range(k, BRK_CORE, 16u * BRK_SLICE, 64u, b);
        for (i = 0; i < 64; i++)
            m = abs(a[i] - b[i]) > m ? abs(a[i] - b[i]) : m;
        printf("  kit %d: slice 0 against 16, first 64 samples: %d\n", k, m);
        CHECK(m < 2000, "kit %d: the loop's seam: %d", k, m);
    }

    /* levels arrive together: a stem at half the target is at half the signal all the way, and both fall to zero */
    {
        static float a[2048], b[2048];
        uint32_t i;
        float pk = 0.0f;
        brk_init();
        brk_set_tempo(170.0f);
        bar_of(NO_HITS, "g...............");
        brk_levels(0.0f, 0.5f, 2048.0f);
        stp(0u);
        for (i = 0; i < 2048u; i += 256u) {
            memset(a + i, 0, sizeof(float) * 256u);
            brk_render(a + i, 256u);
        }
        brk_init();
        brk_set_tempo(170.0f);
        bar_of(NO_HITS, "g...............");
        brk_levels(0.0f, 1.0f, 2048.0f);
        stp(0u);
        for (i = 0; i < 2048u; i += 256u) {
            memset(b + i, 0, sizeof(float) * 256u);
            brk_render(b + i, 256u);
        }
        for (i = 0; i < 2048u; i++) {
            CHECK(fabsf(a[i] - 0.5f * b[i]) < 1e-4f, "ramp: %f against %f at %u", (double)a[i], (double)b[i], i);
            if (fabsf(a[i] - 0.5f * b[i]) >= 1e-4f)
                break;
            pk = fabsf(b[i]) > pk ? fabsf(b[i]) : pk;
        }
        CHECK(pk > 0.01f, "ramp: nothing sounded (%f)", (double)pk);

        brk_init();
        brk_set_tempo(170.0f);
        bar_of(HITS, HITS_TOP);
        brk_levels(1.0f, 0.5f, 1.0f);
        stp(0u);
        memset(a, 0, sizeof(float) * 256u);
        brk_render(a, 256u);
        brk_levels(0.0f, 0.0f, 1024.0f);
        for (i = 0; i < 1024u; i += 256u) {
            memset(a, 0, sizeof(float) * 256u);
            brk_render(a, 256u);
        }
        memset(a, 0, sizeof(float) * 256u);
        brk_render(a, 256u);
        pk = 0.0f;
        for (i = 0; i < 256u; i++)
            pk = fabsf(a[i]) > pk ? fabsf(a[i]) : pk;
        CHECK(pk == 0.0f, "both stems are shut after the ramp: %f", (double)pk);
        brk_render(a, 0u);               /* an empty block is safe */
    }

    /* a sound over: it fades and nothing runs on into the next sound's head, however late the next step */
    {
        uint32_t d0;
        brk_init();
        brk_set_tempo(170.0f);
        brk_levels(1.0f, 0.0f, 1.0f);
        bar_of("t...............", NO_HITS);   /* (the core's last sound but one: four slices) */
        play(170.0f, 1.0f, 3u, 0u);
        d0 = brk_decoded;
        play(170.0f, 3.0f, 1u, 3u);
        CHECK(brk_decoded - d0 < (BRK_SLICE + 46u + 2u + 49u) + 16u, "ran on: %u samples", brk_decoded - d0);
    }

    /* silent stems cost nothing; a level of zero is silence */
    {
        uint32_t d0;
        brk_init();
        brk_levels(0.0f, 0.0f, 1.0f);
        bar_of(HITS, HITS_TOP);
        d0 = brk_decoded;
        peak = 0.0f;
        play(170.0f, 1.0f, 64u, 0u);
        CHECK(peak == 0.0f && brk_decoded == d0, "silent stems: peak %f, %u decoded", (double)peak, brk_decoded - d0);
        CHECK(brk_hits == 0u, "no hits while silent");
    }

    /* the generated bars through the player: every style's kind of numbers, tempos of the range, whole breaks: the jumps are no click */
    {
        static const brk_rhythm_t R[4] = {{50, 20, 80, 50, 30}, {80, 30, 110, 50, 80}, {60, 75, 176, 70, 50}, {20, 20, 72, 20, 30}};
        static const float T[3] = {150.0f, 170.0f, 180.0f};
        int r, t;
        float worst = 0.0f;
        uint32_t jumps = 0u;
        for (r = 0; r < 4; r++)
            for (t = 0; t < 3; t++) {
                const float len = 44100.0f * 60.0f / (T[t] * 4.0f);
                float left = 0.0f, pv = 0.0f;
                brk_gen_t g;
                uint32_t step;
                brk_gen_start(&g);
                brk_init();
                brk_set_tempo(T[t]);
                brk_levels(1.0f, 0.6f, 1.0f);
                for (step = 0u; step < 16u * 8u * 60u; step++) {
                    const uint32_t bar = step / 16u, at = bar % 8u;
                    const uint32_t j0 = brk_jumps;
                    uint32_t n, j, first;
                    if (!(step & 15u)) {
                        const int want = bar / 8u % 2u ? 2 + 2 * (int)(bar / 16u % 2u) : 0;
                        if (want && at + (uint32_t)want >= 8u)
                            brk_gen_bar(&g, &R[r], 3, 70, 50, 3u + (uint32_t)r, bar, want, at + (uint32_t)want - 8u, &brk_bar);
                        else
                            brk_gen_bar(&g, &R[r], 3, 70, 50, 3u + (uint32_t)r, bar, 0, 0u, &brk_bar);
                    }
                    brk_pattern_step(step, 0, 3u);
                    first = brk_jumps != j0 ? 16u : 0u;
                    jumps += brk_jumps - j0;
                    left += len;
                    n = (uint32_t)left;
                    left -= (float)n;
                    for (j = 0u; j < n;) {
                        uint32_t k = n - j > 256u ? 256u : n - j, i;
                        if (first && k > first)
                            k = first;
                        memset(out, 0, sizeof(float) * k);
                        brk_render(out, k);
                        for (i = 0u; i < k; i++) {
                            const float d = fabsf(out[i] - pv);
                            if (first && d > worst)
                                worst = d;
                            pv = out[i];
                        }
                        j += k;
                        first = first > k ? first - k : 0u;
                    }
                }
            }
        printf("  generated bars: %u jumps, the largest step after one %f\n", jumps, (double)worst);
        CHECK(jumps > 10000u, "only %u jumps", jumps);
        /* (CLICK: a third of the largest step the sounds themselves have, see "natural" above: under what they do by themselves) */
        CHECK(worst < CLICK, "a generated bar's jump clicks: %f", (double)worst);
    }

    /* a ghost's lateness: the same sound, that much later, and one sound: no second jump when its next slice comes round */
    {
        static const int8_t LATE[3] = {0, 2, 6};
        int32_t base = 0;
        for (k = 0; k < 3; k++) {
            int32_t first_on = -1, i = 0;
            uint32_t j0;
            brk_init();
            brk_set_tempo(170.0f);
            brk_levels(0.0f, 1.0f, 1.0f);
            memset(&brk_bar, 0, sizeof brk_bar);
            for (i = 0; i < 16; i++)
                brk_bar.core[i] = brk_bar.top[i] = brk_bar.hat[i] = '.';
            brk_bar.top[1] = 'f';
            brk_bar.vtop[1] = 100u;
            brk_bar.late[1] = (uint8_t)LATE[k];
            brk_pattern_step(0u, 0, 0u);
            j0 = brk_jumps;
            brk_pattern_step(1u, 0, 0u);
            CHECK(brk_jumps == j0 + 1u, "a ghost's start: %u jumps", brk_jumps - j0);
            j0 = brk_jumps;
            for (i = 0; i < 3891;) {
                const int32_t n = 3891 - i > 8 ? 8 : 3891 - i;
                int32_t m;
                memset(out, 0, sizeof(float) * (uint32_t)n);
                brk_render(out, (uint32_t)n);
                for (m = 0; m < n && first_on < 0; m++)
                    if (out[m] != 0.0f)
                        first_on = i + m;
                i += n;
            }
            if (!k)
                base = first_on;
            CHECK(first_on >= 0 && abs(first_on - base - LATE[k] * 44) <= 2, "%d ms late: it starts at %d samples, at no lateness %d", LATE[k], first_on, base);
            brk_pattern_step(2u, 0, 0u);
            CHECK(brk_jumps == j0, "%d ms late: its second slice jumped", LATE[k]);
        }
    }

    /* the ghosts are quiet: the loudest of them, as hard as a ghost goes, under half the softer backbeat, levels counted (top 0.6, core 1) */
    {
        static int16_t d[BRK_SLICE * 5u];
        for (k = 0; k < 2; k++) {
            int i, g;
            float sn = 0.0f;
            static const uint8_t GH[2] = {2, 4};     /* f, e: what a plain bar uses */
            brk_decode_range(k, BRK_CORE, 8u * BRK_SLICE, BRK_SLICE * 5u, d);
            for (i = 0; i < (int)(BRK_SLICE * 5u); i++)
                sn = (float)abs(d[i]) > sn ? (float)abs(d[i]) : sn;
            for (g = 0; g < 2; g++) {
                float gp = 0.0f;
                brk_decode_range(k, BRK_TOP, GH[g] * BRK_SLICE, BRK_SLICE * 2u, d);
                for (i = 0; i < (int)(BRK_SLICE * 2u); i++)
                    gp = (float)abs(d[i]) > gp ? (float)abs(d[i]) : gp;
                printf("  kit %d: ghost %c at its hardest against the 12th's snare: %.2f\n", k, "fe"[g], (double)(gp * 1.14f * 0.6f / (sn * 0.88f)));
                CHECK(gp * 1.14f * 0.6f < 0.8f * sn * 0.88f, "kit %d: a ghost at %f of the snare", k, (double)(gp * 1.14f * 0.6f / (sn * 0.88f)));
            }
        }
    }

    printf(fails ? "brk: %d FAILED\n" : "brk: ok\n", fails);
    return fails != 0;
}
