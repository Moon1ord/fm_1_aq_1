/* SPDX-License-Identifier: MPL-2.0 */
/* The break's sounds, made here and kept in git (app/dsp/brk_data.h): two kits, each two stems, synthesised from
 * nothing, so no recording is in them. IMA ADPCM, mono, BRK_SLICE samples a sixteenth at 170 BPM.
 *
 * A stem is no longer a two-bar loop but a bank: each sound sits at a fixed slice with silence after it, and brk.c's
 * patterns choose the slices (the map is there, BANK_CORE and BANK_TOP, and must match the tables below). Soft and far
 * back on purpose: round kicks, warm rimmy snares with a long soft tail, quiet airy hats. The lateness of the snares,
 * the ghosts and the soft hat is written into the sound (it costs nothing at run time): they begin LAG after their
 * slice, so everything played from these banks sits a little behind the kick.
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
    double kick_hz, kick_sweep, kick_dec, sn_hz, sn_dec, sn_tail, sn_lp, hat_hp, hat_dec;
} kit_t;
static const kit_t KIT[2] = {
    {"GLASS", 55.0, 58.0, 0.135, 188.0, 0.050, 0.095, 4300.0, 4600.0, 0.022},
    {"MOSS", 49.0, 48.0, 0.160, 172.0, 0.060, 0.110, 3200.0, 4200.0, 0.028},
};

static uint32_t rs = 0x1234567u;
static double noise(void)
{
    rs ^= rs << 13;
    rs ^= rs >> 17;
    rs ^= rs << 5;
    return (double)(rs >> 8) / 8388608.0 - 1.0;
}
static double coef(double hz) { return 1.0 - exp(-2.0 * PI * hz / SR); }

static double buf[NS];

/* every sound starts a little after its slice: when the player lets go of the sound before, at the slice's edge, its
 * 2 ms fade must not read the next sound's first samples */
#define HEAD 48

/* a sound's sample i, in its slices: it starts at lag and must be over by the region's end (a fade makes it so) */
static void add(int slice, int nslices, int lag, int i, double x)
{
    int len = nslices * SLICE - lag - HEAD, fade = len / 5;
    if (i >= len)
        return;
    if (i > len - fade)
        x *= (double)(len - i) / fade;
    buf[(slice * SLICE + lag + HEAD + i) % NS] += x;
}

static void kick(const kit_t *k, int slice, int nsl, double vel, double tone)
{
    double ph = 0.0, lp = 0.0;
    const double thump = coef(900.0);
    int i, n = nsl * SLICE;
    for (i = 0; i < n; i++) {
        double t = i / SR, f = k->kick_hz + tone * k->kick_sweep * exp(-t / 0.024), x;
        double env = (1.0 - exp(-t / 0.0028)) * exp(-t / k->kick_dec);
        ph += f / SR;
        lp += thump * (noise() - lp);
        x = sin(2.0 * PI * ph) * env;
        x = tanh(1.5 * x) / tanh(1.5);                  /* round, a little third harmonic for small speakers */
        x += lp * 0.10 * tone * exp(-t / 0.006);        /* the beater: felt, not clicked */
        add(slice, nsl, 0, i, x * vel);
    }
}

static void snare(const kit_t *k, int slice, int nsl, int lag, double vel, double dark)
{
    double ph = 0.0, lp1 = 0.0, lp2 = 0.0, hp = 0.0;
    const double cl = coef(k->sn_lp * dark), ch = coef(480.0);
    int i, n = nsl * SLICE - lag;
    for (i = 0; i < n; i++) {
        double t = i / SR, nz = noise(), x, a = 1.0 - exp(-t / 0.0012);
        hp += ch * (nz - hp);
        lp1 += cl * ((nz - hp) - lp1);
        lp2 += cl * (lp1 - lp2);
        ph += (k->sn_hz * (1.0 + 0.10 * exp(-t / 0.012))) / SR;
        x = sin(2.0 * PI * ph) * 0.40 * exp(-t / 0.036);                     /* the head: warm, and gone */
        x += sin(2.0 * PI * ph * 1.59) * 0.18 * exp(-t / 0.024);
        x += (sin(2.0 * PI * 1190.0 * t) * 0.5 + sin(2.0 * PI * 1860.0 * t) * 0.3) * 0.34 * dark * exp(-t / 0.0065);   /* the rim */
        x += lp2 * (0.95 * exp(-t / k->sn_dec) + 0.16 * exp(-t / k->sn_tail));   /* the wires, tight */
        add(slice, nsl, lag, i, x * a * vel);
    }
}

/* a ghost: the same drum barely touched, a short sound */
static void ghost(const kit_t *k, int slice, int nsl, int lag, double vel, double dark)
{
    double ph = 0.0, lp1 = 0.0, lp2 = 0.0;
    const double cl = coef(k->sn_lp * 0.8 * dark);
    int i, n = nsl * SLICE - lag;
    for (i = 0; i < n; i++) {
        double t = i / SR, a = 1.0 - exp(-t / 0.002);
        lp1 += cl * (noise() - lp1);
        lp2 += cl * (lp1 - lp2);
        ph += k->sn_hz * 1.05 / SR;
        add(slice, nsl, lag, i, (sin(2.0 * PI * ph) * 0.62 * exp(-t / 0.032) + lp2 * 0.75 * exp(-t / 0.050)) * a * vel);
    }
}

/* noise above hp, gently shut at the top: an airy hat, no metal in it */
static void hat(int slice, int nsl, int lag, double vel, double hpf, double att, double dec)
{
    double l1 = 0.0, l2 = 0.0, top = 0.0;
    const double c = coef(hpf), ct = coef(9300.0);
    int i, n = nsl * SLICE - lag;
    for (i = 0; i < n; i++) {
        double t = i / SR, nz = noise(), x;
        l1 += c * (nz - l1);
        nz -= l1;
        l2 += c * (nz - l2);
        nz -= l2;
        top += ct * (nz - top);
        x = top * (1.0 - exp(-t / att)) * exp(-t / dec);
        add(slice, nsl, lag, i, x * vel);
    }
}

/* a soft stick on the rim: woody, two short tones */
static void rim(int slice, int nsl, int lag, double vel)
{
    int i, n = nsl * SLICE - lag;
    for (i = 0; i < n; i++) {
        double t = i / SR, a = 1.0 - exp(-t / 0.0007);
        add(slice, nsl, lag, i, (sin(2.0 * PI * 1040.0 * t) * 0.55 * exp(-t / 0.007) +
                                 sin(2.0 * PI * 470.0 * t) * 0.60 * exp(-t / 0.016)) * a * vel);
    }
}

/* air rising: noise whose low-pass opens as it swells, over at the slice's end (what a level's entrance leans on) */
static void swell(int slice, int nsl, double vel)
{
    double l1 = 0.0, l2 = 0.0, hp = 0.0;
    int i, n = nsl * SLICE;
    const double ch = coef(700.0);
    for (i = 0; i < n; i++) {
        double u = (double)i / n, nz = noise(), c = coef(900.0 + 5200.0 * u * u), e = u * u * u;
        hp += ch * (nz - hp);
        l1 += c * ((nz - hp) - l1);
        l2 += c * (l1 - l2);
        if (n - i < 120)
            e *= (n - i) / 120.0;
        buf[(slice * SLICE + i) % NS] += l2 * e * vel;
    }
}

/* a soft tom for a pickup at a phrase's end: low, round, quickly over */
static void tom(const kit_t *k, int slice, int nsl, int lag, double vel)
{
    double ph = 0.0;
    int i, n = nsl * SLICE - lag;
    for (i = 0; i < n; i++) {
        double t = i / SR, a = 1.0 - exp(-t / 0.003);
        ph += (k->sn_hz * 0.62 * (1.0 + 0.12 * exp(-t / 0.03))) / SR;
        add(slice, nsl, lag, i, sin(2.0 * PI * ph) * exp(-t / 0.075) * a * vel);
    }
}

/* the banks: see brk.c's BANK_CORE and BANK_TOP */
static void render_core(const kit_t *k)
{
    memset(buf, 0, sizeof buf);
    kick(k, 0, 6, 1.0, 1.0);                                 /* K */
    kick(k, 6, 6, 0.70, 0.65);                               /* k: softer, rounder */
    snare(k, 12, 6, (int)(0.019 * SR), 0.80, 1.0);           /* S: well behind */
    snare(k, 18, 6, (int)(0.009 * SR), 0.60, 0.72);          /* s: softer, darker, less late */
    tom(k, 24, 4, (int)(0.010 * SR), 0.30);                  /* t: a soft pickup */
}

static void render_top(const kit_t *k)
{
    memset(buf, 0, sizeof buf);
    /* three closed hats, not one sample: each a little different, the off ones later (the swing is in the sound) */
    hat(0, 1, 0, 1.0, k->hat_hp, 0.0010, k->hat_dec);                       /* h: on the beat */
    hat(1, 1, (int)(0.006 * SR), 1.15, k->hat_hp * 1.12, 0.0013, k->hat_dec * 0.80);   /* j: a little later, thinner */
    hat(2, 1, (int)(0.012 * SR), 0.80, k->hat_hp * 0.86, 0.0016, k->hat_dec * 1.15);   /* l: later, rounder */
    hat(3, 1, (int)(0.010 * SR), 0.62, k->hat_hp * 1.1, 0.0014, k->hat_dec * 0.8);     /* i: the quiet one, level 1's */
    hat(4, 6, 0, 0.60, k->hat_hp * 0.9, 0.006, 0.115);                       /* o: open, a long soft breath */
    ghost(k, 10, 2, (int)(0.014 * SR), 0.085, 1.0);                           /* g */
    ghost(k, 12, 2, (int)(0.022 * SR), 0.07, 0.75);                          /* f: softer, later */
    ghost(k, 14, 2, (int)(0.008 * SR), 0.05, 0.60);                          /* e: barely touched */
    rim(16, 2, (int)(0.008 * SR), 0.07);                                     /* r */
    hat(18, 2, 0, 0.30, k->hat_hp * 0.7, 0.009, 0.024);                      /* x: a shaker's push */
    swell(20, 12, 0.22);                                                     /* ~ */
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

/* the loop is encoded twice and the second pass kept: it starts from the state the first one ended in, which is the
 * state a player has when it wraps (or jumps) to slice 0, so the head of the loop is not lost to a cold start */
static uint32_t encode(void)
{
    double peak = 1e-9;
    int pred = 0, idx = 0, i, pass;
    uint32_t h = 2166136261u;
    for (i = 0; i < NS; i++)
        peak = fabs(buf[i]) > peak ? fabs(buf[i]) : peak;
    for (pass = 0; pass < 2; pass++) {
        memset(enc, 0, sizeof enc);
        h = 2166136261u;
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
