/* SPDX-License-Identifier: MPL-2.0 */
/* The break's loops, made here and kept in git (app/dsp/brk_data.h): two kits, each a two-bar CORE (kick and snare
 * only) and a two-bar TOP (hats, ghost snares, a ride), synthesised from nothing, so no recording is in them.
 * IMA ADPCM, mono, BRK_SLICE samples a sixteenth at 170 BPM.
 *   cc -O2 -std=c99 -o /tmp/gen_breaks tools/gen_breaks.c -lm && /tmp/gen_breaks > app/dsp/brk_data.h */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define SLICE 2029
#define NSTEP 32
#define NS (SLICE * NSTEP)
#define SR 22995.33
#define PI 3.14159265358979

typedef struct {
    const char *name;
    double kick_hz, kick_sweep, kick_dec, sn_hz, sn_dec, sn_noise, sn_lp, hat_dec, hat_hp, ride;
} kit_t;
static const kit_t KIT[2] = {
    {"GLASS", 54.0, 120.0, 0.20, 215.0, 0.13, 0.55, 0.55, 0.030, 0.75, 0.0},
    {"MOSS", 48.0, 90.0, 0.26, 180.0, 0.17, 0.45, 0.35, 0.045, 0.55, 0.35},
};

static uint32_t rs = 0x1234567u;
static double noise(void)
{
    rs ^= rs << 13;
    rs ^= rs >> 17;
    rs ^= rs << 5;
    return (double)(rs >> 8) / 8388608.0 - 1.0;
}

static double buf[NS];

static void add(int step, int i, double x) { buf[(step * SLICE + i) % NS] += x; }

static void kick(const kit_t *k, int step, double vel)
{
    double ph = 0.0;
    int i;
    for (i = 0; i < (int)(k->kick_dec * 6.0 * SR); i++) {
        double t = i / SR, f = k->kick_hz + k->kick_sweep * exp(-t * 45.0);
        ph += f / SR;
        add(step, i, sin(2.0 * PI * ph) * exp(-t / k->kick_dec) * vel);
    }
}

static void snare(const kit_t *k, int step, double vel)
{
    double ph = 0.0, lp = 0.0;
    int i;
    for (i = 0; i < (int)(k->sn_dec * 6.0 * SR); i++) {
        double t = i / SR, n = noise();
        lp += k->sn_lp * (n - lp);
        ph += k->sn_hz / SR;
        add(step, i, (sin(2.0 * PI * ph) * (1.0 - k->sn_noise) * exp(-t / (k->sn_dec * 0.5)) +
                      lp * k->sn_noise * exp(-t / k->sn_dec)) * vel);
    }
}

static void hat(const kit_t *k, int step, double vel)
{
    double lp = 0.0;
    int i;
    for (i = 0; i < (int)(k->hat_dec * 6.0 * SR); i++) {
        double t = i / SR, n = noise();
        lp += k->hat_hp * (n - lp);
        add(step, i, (n - lp) * exp(-t / k->hat_dec) * vel);
    }
}

static void ride(const kit_t *k, int step, double vel)
{
    static const double R[4] = {1.0, 1.47, 2.09, 2.56};
    int i, p;
    for (i = 0; i < (int)(1.2 * SR); i++) {
        double t = i / SR, x = 0.0;
        for (p = 0; p < 4; p++)
            x += sin(2.0 * PI * 1450.0 * R[p] * t) * 0.25;
        add(step, i, x * exp(-t / 0.30) * vel * k->ride);
    }
}

static void render_core(const kit_t *k)
{
    static const int KICK[4] = {0, 10, 16, 26}, SNARE[4] = {4, 12, 20, 28};
    int i;
    memset(buf, 0, sizeof buf);
    for (i = 0; i < 4; i++) {
        kick(k, KICK[i], 1.0);
        snare(k, SNARE[i], 0.9);
    }
}

static void render_top(const kit_t *k)
{
    static const int GHOST[6] = {7, 9, 15, 23, 25, 31};
    int s;
    memset(buf, 0, sizeof buf);
    for (s = 0; s < NSTEP; s++) {
        double v = s % 4 == 0 ? 0.55 : s % 2 == 0 ? 0.40 : s % 4 == 3 ? 0.20 : 0.0;
        if (v > 0.0)
            hat(k, s, v);
        if (s % 4 == 2 && k->ride > 0.0)
            ride(k, s, 0.35);
    }
    for (s = 0; s < 6; s++)
        snare(k, GHOST[s], 0.20);
}

/* IMA ADPCM (the standard's tables) */
static const int STEP[89] = {
    7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50, 55, 60, 66, 73, 80, 88, 97, 107, 118, 130, 143,
    157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658, 724, 796, 876, 963, 1060, 1166, 1282, 1411, 1552,
    1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871, 5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487,
    12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767};
static const int INDEX[16] = {-1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8};

static uint8_t enc[NS / 2];
static int at_pred[NSTEP], at_idx[NSTEP];

static uint32_t encode(void)
{
    double peak = 1e-9;
    int pred = 0, idx = 0, i;
    uint32_t h = 2166136261u;
    for (i = 0; i < NS; i++)
        peak = fabs(buf[i]) > peak ? fabs(buf[i]) : peak;
    memset(enc, 0, sizeof enc);
    for (i = 0; i < NS; i++) {
        int x = (int)(buf[i] / peak * 0.72 * 32767.0), diff = x - pred, st = STEP[idx], code = 0, dq = st >> 3;
        if (i % SLICE == 0) {
            at_pred[i / SLICE] = pred;
            at_idx[i / SLICE] = idx;
        }
        if (diff < 0) {
            code = 8;
            diff = -diff;
        }
        if (diff >= st) { code |= 4; diff -= st; dq += st; }
        if (diff >= st >> 1) { code |= 2; diff -= st >> 1; dq += st >> 1; }
        if (diff >= st >> 2) { code |= 1; dq += st >> 2; }
        pred += code & 8 ? -dq : dq;
        pred = pred > 32767 ? 32767 : pred < -32768 ? -32768 : pred;
        idx += INDEX[code];
        idx = idx < 0 ? 0 : idx > 88 ? 88 : idx;
        enc[i >> 1] |= (uint8_t)(code << ((i & 1) * 4));
        h = (h ^ (uint32_t)(uint16_t)pred) * 16777619u;
    }
    return h;
}

int main(void)
{
    static const char *const STEM[2] = {"CORE", "TOP"};
    uint32_t hash[2][2];
    int k, s, i;
    printf("/* Generated by tools/gen_breaks.c: do not edit. SPDX-License-Identifier: MPL-2.0 */\n#pragma once\n");
    for (k = 0; k < 2; k++)
        for (s = 0; s < 2; s++) {
            if (s == 0)
                render_core(&KIT[k]);
            else
                render_top(&KIT[k]);
            hash[k][s] = encode();
            printf("static const uint8_t BRK_%s_%s[%d] = {", KIT[k].name, STEM[s], NS / 2);
            for (i = 0; i < NS / 2; i++)
                printf("%s%u,", i % 32 ? "" : "\n", enc[i]);
            printf("\n};\nstatic const brk_ima_t BRK_%s_%s_AT[%d] = {", KIT[k].name, STEM[s], NSTEP);
            for (i = 0; i < NSTEP; i++)
                printf("{%d,%d,0},", at_pred[i], at_idx[i]);
            printf("};\n");
        }
    printf("static const uint8_t *const BRK_DATA[2][2] = {{BRK_GLASS_CORE, BRK_GLASS_TOP}, {BRK_MOSS_CORE, BRK_MOSS_TOP}};\n");
    printf("static const brk_ima_t *const BRK_AT[2][2] = {{BRK_GLASS_CORE_AT, BRK_GLASS_TOP_AT}, {BRK_MOSS_CORE_AT, BRK_MOSS_TOP_AT}};\n");
    printf("static const uint32_t BRK_HASH[2][2] = {{%uu, %uu}, {%uu, %uu}};\n", hash[0][0], hash[0][1], hash[1][0], hash[1][1]);
    return 0;
}
