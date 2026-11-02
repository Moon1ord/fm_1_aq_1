/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal */
/* The instrument (amb.h). One compilation unit, -O2. */
#include "amb.h"
#include "brk.h"
#include "fastmath.h"

#if defined EN_HOST || defined PLAT_HOST
#define EN_POOL
#else
#define EN_POOL __attribute__((section(".pool")))
#endif
#define BARRIER() __asm__ volatile("" ::: "memory")

const char *const EN_NOTE_NAME[12] = {"C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B"};
const char *const EN_MODE_NAME[EN_NMODES] = {"Ionian", "Dorian", "Phrygian", "Lydian", "Mixolyd.", "Aeolian",
                                             "Pent maj", "Pent min"};
#define B(a, b, c, d, e, f, g) (1u << (a) | 1u << (b) | 1u << (c) | 1u << (d) | 1u << (e) | 1u << (f) | 1u << (g))
const uint16_t EN_MODE_POOL[EN_NMODES] = {
    B(0, 2, 4, 5, 7, 9, 11), B(0, 2, 3, 5, 7, 9, 10), B(0, 1, 3, 5, 7, 8, 10), B(0, 2, 4, 6, 7, 9, 11),
    B(0, 2, 4, 5, 7, 9, 10), B(0, 2, 3, 5, 7, 8, 10), B(0, 2, 4, 7, 9, 9, 9),  B(0, 3, 5, 7, 10, 10, 10)};
#undef B
const uint8_t EN_LOOP_LEN[EN_NLOOP] = {13, 17, 23, 29};

static const en_param_t PARAMS[P_NPARAMS] = {
    {"Pad", 0, 100, 60},      {"Bells", 0, 100, 60},    {"Bass", 0, 100, 50},     {"Break", 0, 100, 45},
    {"Density", 0, 100, 40},  {"Evolve", 0, 100, 35},   {"Drift", 0, 100, 25},    {"Busy", 0, 100, 35},
    {"Echo", 0, 100, 40},     {"Echo time", 0, 5, 3},   {"Reverb", 0, 100, 65},   {"Size", 0, 100, 80},
    {"Bright", 0, 100, 55},   {"Swell", 0, 100, 60},    {"Bell tone", 0, 100, 60}, {"Motion", 0, 100, 60},
    {"Tune", -50, 50, 0},     {"MIDI out", 0, 1, 1},    {"Tempo", 150, 180, 170}, {"Root", 0, 11, 2},
    {"Mode", 0, EN_NMODES - 1, 3}, {"Bell oct", -2, 2, 0}, {"Pad tone", 0, 1, 0},
    {"Bass oct", -1, 1, 0},   {"Glide", 0, 100, 10},    {"Fifth", 0, 100, 35},
    {"Water", 0, 100, 30},    {"Kit", 0, 1, 0},         {"Tide", 0, 100, 50},
};
const en_param_t *en_param_info(int i) { return (i >= 0 && i < P_NPARAMS) ? &PARAMS[i] : &PARAMS[0]; }

/* the pieces: eight recipes for the whole garden (the FM-1's PRESETS, the web app's cards). Tune and
 * MIDI out are taken from here by neither: they belong to the player, not the piece */
/*                     Pad Bel Bas Brk  Den Evo Dri Bsy  Ech ET Rev Siz  Bri Swe Ton Mot  Tun MI Tem  Rt Md Oc  PT  BO Gli Fif  Wat Kit Tid
 * (PT: the pad's colour, 0 Rhodes, 1 Voices; BO: the bass's octave) */
static const struct {
    const char *name;
    int16_t v[P_NPARAMS];
} PIECES[EN_NPIECES] = {
    {"Aqua",      {60, 60, 50, 45, 40, 35, 25, 35, 40, 3, 65, 80, 55, 60, 60, 60, 0, 1, 170, 2, 3, 0, 0, 0, 10, 35, 30, 0, 50}},
    {"Skyline",   {55, 65, 50, 50, 50, 40, 20, 45, 40, 3, 60, 75, 70, 50, 70, 65, 0, 1, 172, 7, 0, 0, 0, 0, 10, 40, 15, 0, 30}},
    {"Bubble",    {45, 80, 45, 45, 65, 45, 25, 40, 45, 3, 60, 75, 60, 50, 80, 60, 0, 1, 174, 9, 6, 1, 0, 0, 10, 30, 25, 0, 40}},
    {"Rainfall",  {65, 45, 45, 35, 30, 30, 35, 25, 45, 4, 70, 85, 45, 70, 40, 55, 0, 1, 165, 5, 0, 0, 1, 0, 15, 25, 60, 0, 80}},
    {"Aurora",    {70, 50, 45, 40, 35, 35, 40, 30, 50, 4, 80, 95, 50, 80, 50, 70, 0, 1, 168, 4, 3, 0, 1, 0, 15, 30, 25, 1, 60}},
    {"Lagoon",    {55, 55, 60, 45, 40, 40, 25, 35, 40, 3, 65, 80, 45, 60, 45, 60, 0, 1, 170, 0, 4, 0, 0, 0, 20, 45, 35, 1, 50}},
    {"Dawn",      {70, 40, 35, 25, 25, 25, 50, 20, 45, 4, 80, 90, 40, 80, 35, 50, 0, 1, 160, 10, 0, 0, 1, 0, 20, 20, 45, 0, 100}},
    {"Afterglow", {60, 55, 50, 40, 35, 40, 30, 35, 45, 4, 70, 85, 40, 65, 45, 60, 0, 1, 165, 5, 1, 0, 0, 0, 15, 40, 20, 1, 50}},
};

const char *en_piece_name(int i) { return PIECES[(unsigned)i < EN_NPIECES ? i : 0].name; }
void en_piece(int i, int16_t *par, uint16_t *pool)
{
    int p;
    i = (unsigned)i < EN_NPIECES ? i : 0;
    for (p = 0; p < P_NPARAMS; p++)
        par[p] = PIECES[i].v[p];
    *pool = EN_MODE_POOL[PIECES[i].v[P_MODE]];
}

/* the demo's default place (3) is the dotted quarter: six sixteenths of the half-time feel's grid */
static const char *const ETIME_NAME[6] = {"1 bar", "1/2", "1/2.", "1/4.", "2 bars", "4 bars"};
static const float ETIME_BEATS[6] = {4.0f, 2.0f, 3.0f, 1.5f, 8.0f, 16.0f};

static char *itoa_s(int v, char *b)
{
    char t[8];
    int n = 0, neg = v < 0;
    if (neg)
        v = -v;
    do {
        t[n++] = (char)('0' + v % 10);
        v /= 10;
    } while (v && n < 6);
    if (neg)
        *b++ = '-';
    while (n)
        *b++ = t[--n];
    *b = 0;
    return b;
}

static float drift_secs(int v) { return 20.0f * fm_exp2f((float)v * 0.039069f); }   /* 20 s .. 5 min */

void en_param_text(int i, int v, char *b)
{
    static const char *const ONOFF[2] = {"Off", "On"};
    const char *s = 0;
    if (i == P_ETIME)
        s = ETIME_NAME[(unsigned)v < 6u ? v : 0];
    else if (i == P_MIDI)
        s = ONOFF[v ? 1 : 0];
    else if (i == P_PADTONE)
        s = v ? "Voices" : "Rhodes";
    else if (i == P_KIT)
        s = v ? "Moss" : "Glass";
    else if (i == P_MODE)
        s = EN_MODE_NAME[(unsigned)v < EN_NMODES ? v : 0];
    else if (i == P_ROOT)
        s = EN_NOTE_NAME[(unsigned)v < 12u ? v : 0];
    if (s) {
        int k = 0;
        while (s[k] && k < 9) {
            b[k] = s[k];
            k++;
        }
        b[k] = 0;
        return;
    }
    if (i == P_DRIFT) {
        int sec = (int)(drift_secs(v) + 0.5f);
        if (sec < 60) {
            b = itoa_s(sec, b);
            *b++ = 's';
            *b = 0;
        } else {
            b = itoa_s(sec / 60, b);
            *b++ = ':';
            *b++ = (char)('0' + sec % 60 / 10);
            *b++ = (char)('0' + sec % 10);
            *b = 0;
        }
        return;
    }
    if ((i == P_TUNE || i == P_BOCT || i == P_BASSOCT) && v > 0)
        *b++ = '+';
    itoa_s(v, b);
}

/* ------------------------------------------------------- the pure parts --- */

int en_scale(uint32_t pool, int *steps)
{
    int i, m = 0;
    pool |= 1u;
    for (i = 0; i < 12; i++)
        if (pool >> i & 1u)
            steps[m++] = i;
    return m;
}

int en_chord_tones(uint32_t pool, int root, int deg, int *pcs)
{
    static const uint8_t OFF[7] = {0, 2, 4, 6, 1, 3, 5};   /* stacked thirds, then what's left */
    int st[12], m = en_scale(pool, st), want, n = 0, i, j;
    want = m >= 6 ? 4 : 3;
    deg = ((deg % m) + m) % m;
    for (i = 0; i < 7 && n < want; i++) {
        int pc = (root + st[(deg + OFF[i]) % m]) % 12, dup = 0;
        for (j = 0; j < n; j++)
            dup |= pcs[j] == pc;
        if (!dup)
            pcs[n++] = pc;
    }
    return n;
}

static int nearest(int from, int pc, int lo, int hi)    /* the note of class pc nearest from, in lo..hi */
{
    int n = from + ((pc - from) % 12 + 12) % 12;          /* at or above */
    if (n - from > 6)
        n -= 12;
    while (n < lo)
        n += 12;
    while (n > hi)
        n -= 12;
    return n;
}

void en_voice_lead(const int *from, const int *pcs, int nt, int *to, int lo, int hi)
{
    static const uint8_t PERM[24][4] = {
        {0, 1, 2, 3}, {0, 1, 3, 2}, {0, 2, 1, 3}, {0, 2, 3, 1}, {0, 3, 1, 2}, {0, 3, 2, 1},
        {1, 0, 2, 3}, {1, 0, 3, 2}, {1, 2, 0, 3}, {1, 2, 3, 0}, {1, 3, 0, 2}, {1, 3, 2, 0},
        {2, 0, 1, 3}, {2, 0, 3, 1}, {2, 1, 0, 3}, {2, 1, 3, 0}, {2, 3, 0, 1}, {2, 3, 1, 0},
        {3, 0, 1, 2}, {3, 0, 2, 1}, {3, 1, 0, 2}, {3, 1, 2, 0}, {3, 2, 0, 1}, {3, 2, 1, 0}};
    int t[4], p, v, best = 1 << 30;
    for (v = 0; v < 4; v++)
        t[v] = pcs[v < nt ? v : 0];                        /* three tones: the root doubled */
    for (p = 0; p < 24; p++) {
        int c = 0, n[4], a, b;
        for (v = 0; v < 4; v++) {
            n[v] = nearest(from[v], t[PERM[p][v]], lo, hi);
            c += n[v] > from[v] ? n[v] - from[v] : from[v] - n[v];
        }
        for (a = 0; a < 4; a++)                            /* no two voices on one note */
            for (b = a + 1; b < 4; b++)
                if (n[a] == n[b])
                    c += 40;
        if (c < best) {
            best = c;
            for (v = 0; v < 4; v++)
                to[v] = n[v];
        }
    }
}

const char *en_chord_name(const int *pcs, int n, char *buf)
{
    uint32_t iv = 0;
    int i, k = 0, third, fifth;
    const char *q = "", *r = EN_NOTE_NAME[pcs[0] % 12];
#define HAS(x) (iv >> (x) & 1u)
    for (i = 1; i < n; i++)
        iv |= 1u << ((pcs[i] - pcs[0] + 12) % 12);
    third = HAS(4) ? 4 : HAS(3) ? 3 : 0;
    fifth = HAS(7) ? 7 : HAS(6) ? 6 : HAS(8) ? 8 : 0;
    if (third == 4) {
        q = fifth == 8 ? "aug" : HAS(11) ? (HAS(2) ? "M9" : "M7") : HAS(10) ? (HAS(2) ? "9" : "7")
            : HAS(9) ? (HAS(2) ? "6/9" : "6") : HAS(2) ? "add9" : "";
    } else if (third == 3) {
        if (fifth == 6)
            q = HAS(10) ? "m7b5" : HAS(9) ? "dim7" : "dim";
        else
            q = HAS(10) ? (HAS(2) ? "m9" : HAS(5) ? "m11" : "m7") : HAS(11) ? "mM7"
                : HAS(9) ? (HAS(2) ? "m6/9" : "m6") : HAS(2) ? "madd9" : "m";
    } else if (HAS(5)) {
        q = HAS(10) ? (HAS(9) ? "13sus" : HAS(2) ? "9sus" : "7sus4") : "sus4";
    } else {
        q = HAS(2) ? (HAS(10) ? "9sus" : "sus2") : fifth == 7 ? "5" : "";
    }
#undef HAS
    while (*r)
        buf[k++] = *r++;
    while (*q)
        buf[k++] = *q++;
    buf[k] = 0;
    return buf;
}

/* ------------------------------------------------------------ commands --- */
enum { C_SET, C_POOL, C_PLAY, C_NEXT, C_FREEZE, C_SEED, C_PANIC, C_CLOCK, C_REGROW };
#define QN 64u
static uint32_t q[QN];
static volatile uint32_t q_w, q_r;
static void post(uint32_t c, int a, int b)
{
    uint32_t w = q_w;
    if (w - q_r >= QN)
        return;                                   /* full: dropped (never, at the UI's rate) */
    q[w % QN] = c << 24 | ((uint32_t)a & 0xFFu) << 16 | ((uint32_t)b & 0xFFFFu);
    BARRIER();
    q_w = w + 1;
}
void en_set(int p, int v) { post(C_SET, p, v); }
void en_pool(uint32_t pool) { post(C_POOL, 0, (int)(pool & 0xFFFu)); }
void en_play(int on) { post(C_PLAY, on, 0); }
void en_next(void) { post(C_NEXT, 0, 0); }
void en_freeze(int on) { post(C_FREEZE, on, 0); }
void en_seed(uint32_t s) { post(C_SEED, 0, (int)(s & 0xFFFFu)); }
void en_panic(void) { post(C_PANIC, 0, 0); }
void en_clock(int msg) { post(C_CLOCK, msg, 0); }
void en_regrow(void) { post(C_REGROW, 0, 0); }

/* -------------------------------------------------------------- state --- */
volatile uint8_t en_playing, en_frozen, en_ext;
volatile uint8_t en_chord[EN_NPAD], en_chord_root, en_chord_n;
volatile float en_drift, en_pad_level, en_bass_level;
volatile uint8_t en_loop_pos[EN_NLOOP];
volatile int8_t en_loop_note[EN_NLOOP][EN_LOOP_MAX];
volatile float en_loop_glow[EN_NLOOP];
volatile uint32_t en_bars;
volatile float en_arc;
volatile uint32_t en_hits;
volatile float en_break_level;
volatile uint8_t en_pad_note[EN_NPAD];
volatile float en_pad_amp[EN_NPAD];

static int16_t par[P_NPARAMS];
static uint32_t pool = 0xAB5u;                  /* Ionian until told: what the harmony is made of, from the next seam */
static float tunefac = 1.0f;
static uint32_t rng = 0x9E3779B9u;

static uint32_t rnd(void)                       /* Marsaglia's xorshift (Xorshift RNGs, 2003): shifts 13, 17, 5 */
{
    uint32_t x = rng;
    x = x ^ (x << 13);
    x = x ^ (x >> 17);
    rng = x ^ (x << 5);
    return rng;
}
static float frand(void) { return (float)(rnd() >> 8) * (1.0f / 16777216.0f); }
static int irand(int n) { return n > 0 ? (int)((rnd() >> 8) % (uint32_t)n) : 0; }

static void midi(uint32_t st, uint32_t d1, uint32_t d2)
{
    if (par[P_MIDI])
        en_midi_out(st, d1, d2);
}

static float mtof(float n) { return 440.0f * tunefac * fm_exp2f((n - 69.0f) * (1.0f / 12.0f)); }

/* a sine table: 1024 points a turn, plus one */
#define SN 1024
static float sint[SN + 1];
static inline float sine(float ph)              /* ph in turns, any value >= 0 */
{
    float x = (ph - (float)(int)ph) * (float)SN;
    int i = (int)x;
    float f = x - (float)i;
    return sint[i] + (sint[i + 1] - sint[i]) * f;
}

/* ------------------------------------------------- slow random motion --- */
#define NMOD 9                                  /* 0..5 the pads', 6..8 the echo's */
#define NMOD_PAD 6
static struct {
    float v, tgt, rate;
    uint32_t left;                              /* samples until a new target */
} mod[NMOD];

static void mods_step(uint32_t n)
{
    int i;
    for (i = 0; i < NMOD; i++) {
        if (mod[i].left <= n) {
            float secs = 7.0f + 33.0f * frand();
            mod[i].tgt = frand() * 2.0f - 1.0f;
            mod[i].left = (uint32_t)(secs * EN_SR);
            mod[i].rate = (float)n / (secs * EN_SR * 0.5f);
        } else {
            mod[i].left -= n;
        }
        mod[i].v += (mod[i].tgt - mod[i].v) * fm_minf(mod[i].rate, 1.0f);
    }
}
static float motion;                            /* 0..1: Motion */

/* ----------------------------------------------------------------- break --- */
static float break_vol;
static float step_len, pulse_left;               /* a sixteenth, in samples; to the next MIDI clock pulse */
static float brk_lp[2], brk_hp, plp[4], eplp[2];

/* ------------------------------------------------------------------ space --- */
static float echo_send, rev_send, echo_len, echo_len_t, echo_fb = 0.55f;
static float rev_in_gain = 1.0f;                /* falls to 0 while frozen */

/* ---------------------------------------------------------- the demo's song --- */
/* Waiting for something to load: warm, calm, hopeful, a thing about to begin that never quite arrives. Four chords of
 * the pool on Root, in D Ionian IV iii vi V: Gmaj7 F#m7 Bm9 Aadd9, each 64 sixteenths (5.6 s), repeating
 * exactly. The tonic is never the root of anything and the V goes back to the IV: the cycle leans forward and does not
 * land. Root, mode and keys change the chords at the next cycle's seam, single notes and motifs at once. Everything below is fixed but for small human errors of time and touch (rnd(): the same for a fixed seed). */
#define CHORD_STEPS 64u
#define CYCLE_STEPS (4u * CHORD_STEPS)
#define SONG_STEPS 2880u                         /* 4:14, then again from the top */
#define LAST_CHORD 2752u                         /* cycle 10's V: the last thing played; its answer never comes */
#define PAD_OFF 2788u

/* the timeline */
typedef struct {
    uint16_t step;
    uint8_t level;                               /* the drums: 0 none, 1 hats, 2 light two-step, 3 fuller, 4 half time */
    uint8_t harm;                                /* the harmony's: 3 is the second cycle */
    uint8_t bars;                                /* how many bars of 170 the drums take to arrive */
    uint8_t bass;                                /* its level, % */
    uint8_t touch;                               /* how firmly the piano plays, %: it brightens with the music */
} part_t;
static const part_t PART[] = {
    {0, 0, 0, 1, 0, 92},                         /* 0:00 pad and piano */
    {256, 1, 1, 4, 0, 95},                       /* 0:23 hats alone */
    {384, 2, 1, 2, 0, 97},                       /* 0:34 a light kick and snare */
    {512, 2, 2, 2, 100, 100},                    /* 0:45 the two-step, the bass */
    {1536, 1, 1, 2, 75, 96},                     /* 2:15 the breakdown: hats, chords, long roots */
    {1664, 4, 1, 2, 75, 96},                     /* 2:27 half time, for a moment */
    {1792, 3, 3, 1, 112, 114},                   /* 2:38 back, fuller, the second cycle: the arrival */
    {2304, 3, 2, 1, 112, 108},                   /* 3:23 the harmony starts home; the drums stay */
    {2624, 1, 1, 8, 55, 95},                     /* 3:51 out: the drums thin away */
    {2752, 0, 0, 4, 0, 90},                      /* 4:03 the last chord, alone; ends open */
    {SONG_STEPS, 0, 0, 1, 0, 0},
};
/* The harmony grows, a little, and only at the cycle's seam. Three layers, from a small hand-written vocabulary:
 *   colour:       the same chord with other extensions, most cycles, one or two chords at a time
 *   substitution: every 2 to 4 cycles one chord becomes a relative along the graph below; these accumulate, and the
 *                 more there are the likelier the next move is one back home
 *   section:      at level 3 a second cycle, ii V iii vi, falling fifths and a higher hand; out of it, chord by chord
 * The tonic stays in the upper voices and is never in the bass as a root, including at the end.
 * en_harm_step is pure: state, cycle, seed, level in, state out (a later generator can drive it live); en_cycle_chords
 * replays it.
 *
 * The walk moves over functions, not notes: an id below is a place in the graph, named for what it is in D Ionian. What
 * it sounds like comes from the pool, by en_harm_resolve: the family (the pool's character) gives the id a root, the
 * first of a short list that stands on a fifth and a third in this pool; the shape gives its voices as functions of that
 * root (1 3 5 7 9 11 13), each found in the pool or replaced along a chain. So the walk keeps its place when the
 * pool changes under it. No dominant sevenths (a major third and a flat seventh pull somewhere, and this music does not
 * arrive), so that seventh is left out for the ninth; no minor sixth over a minor chord's third either, for the same
 * tritone. A minor ninth above a chord's root, a flat fifth and a flat thirteenth are never asked for. */
enum { H_G7, H_G9, H_G69, H_FM7, H_FM11, H_BM9, H_BM7, H_AADD9, H_A7SUS, H_A69, H_EM9, H_EM7, H_DF, H_GB, H_A13, H_A9SUS,
       H_CM9, H_SEC, H_SEC7, H_SEC3, H_SECD9, EN_HARM_N };
enum { R_A, R_B, R_C, R_D, R_SUB0, R_TON, R_SUBC, R_SEC0, R_SEC1, R_SEC3, R_SUSD, R_BORROW, R_N };   /* the places a root is found for */
typedef struct {
    const char *name;                            /* in D Ionian */
    int8_t role;                                 /* R_*: where its root comes from */
    int8_t plain;                                /* the family's plain chord: itself when it has no other */
    int8_t colour[2];                            /* other colours of this family (from plain), -1 none */
    int8_t sub;                                  /* (plain) the relative along the graph, -1 none */
    int8_t hid;                                  /* the bass's seeded figures go by this */
} hnode_t;
static const hnode_t HNODE[EN_HARM_N] = {
    /* name       role      plain     colours          sub      hid */
    {"Gmaj7",     R_A,      H_G7,    {H_G9, H_G69},    H_EM9,   H_G7},
    {"Gmaj9",     R_A,      H_G7,    {-1, -1},         -1,      H_G9},
    {"G6/9",      R_A,      H_G7,    {-1, -1},         -1,      H_G69},
    {"F#m7",      R_B,      H_FM7,   {H_FM11, -1},     H_DF,    H_FM7},
    {"F#m11",     R_B,      H_FM7,   {-1, -1},         -1,      H_FM11},
    {"Bm9",       R_C,      H_BM9,   {H_BM7, -1},      H_GB,    H_BM9},
    {"Bm7",       R_C,      H_BM9,   {-1, -1},         -1,      H_BM7},
    {"Aadd9",     R_D,      H_AADD9, {H_A7SUS, H_A69}, H_A9SUS, H_AADD9},
    {"A7sus4",    R_D,      H_AADD9, {-1, -1},         -1,      H_A7SUS},
    {"A6/9",      R_D,      H_AADD9, {-1, -1},         -1,      H_A69},
    {"Em9",       R_SUB0,   H_EM9,   {H_EM7, -1},      H_G7,    H_EM9},
    {"Em7",       R_SUB0,   H_EM9,   {-1, -1},         -1,      H_EM7},
    {"Dmaj7/F#",  R_TON,    H_DF,    {-1, -1},         H_FM7,   H_DF},
    {"Gadd9/B",   R_SUBC,   H_GB,    {-1, -1},         H_BM9,   H_GB},
    {"A13sus",    R_SEC1,   H_A13,   {H_SECD9, -1},    -1,      H_A13},
    {"A9sus",     R_SUSD,   H_A9SUS, {-1, -1},         H_AADD9, H_A9SUS},
    {"Cmaj9",     R_BORROW, H_CM9,   {-1, -1},         -1,      H_CM9},   /* the one borrowed chord */
    /* the second cycle's own places: in Ionian the same chords as Em9, Em7, Bm9, A9sus */
    {"Em9",       R_SEC0,   H_SEC,   {H_SEC7, -1},     -1,      H_EM9},
    {"Em7",       R_SEC0,   H_SEC,   {-1, -1},         -1,      H_EM7},
    {"Bm9",       R_SEC3,   H_SEC3,  {-1, -1},         -1,      H_BM9},
    {"A9sus",     R_SEC1,   H_SECD9, {-1, -1},         -1,      H_A9SUS},
};
static const uint8_t HOME[4] = {H_G7, H_FM7, H_BM9, H_AADD9};
static const uint8_t SECTION[4] = {H_SEC, H_A13, H_FM7, H_SEC3};

/* The families: where each place's root lies, in semitones above the tonic, the first that stands in the pool.
 *   major: IV iii vi V, then ii V iii vi. Lydian has no IV (its #iv is half-diminished), so II, its own chord; Mixolydian's
 *          iii is half-diminished, so bVII, its own chord: G C Bm Am falls by step.
 *   minor: iv III VI v, the same steps: down, up a fourth, down, down, never i. Dorian has no VI (its vi is
 *          half-diminished): bVII. Phrygian's v is diminished: bvii. The second cycle is the falling fifths the minor
 *          key really has: iv VII III VI, Dorian's vi replaced by v. Phrygian's relative for iv is its bII, the only
 *          place that chord appears.
 *   sparse: the same motion on what a pentatonic has, as sus chords; a root need not have its fifth. */
#define ROOT_MINOR 0x40                          /* only if its third is minor here */
enum { FAM_MAJOR = EN_FAM_MAJOR, FAM_MINOR = EN_FAM_MINOR, FAM_SPARSE = EN_FAM_SPARSE };
static const int8_t ROOTS[3][R_N][4] = {
    {   /* major */
        {5, 2, -1}, {4, 10, -1}, {9, -1}, {7, -1}, {2 | ROOT_MINOR, 11, -1}, {0, -1}, {5, 2, -1}, {2, -1}, {7, -1}, {9, -1}, {7, -1}, {-1},
    },
    {   /* minor */
        {5, -1}, {3, -1}, {8, 10, -1}, {7, 10, -1}, {1, 2 | ROOT_MINOR, 10, -1}, {0, -1}, {5, -1}, {5, -1}, {10, -1}, {8, 7, -1}, {7, 10, -1}, {-1},
    },
    {   /* sparse */
        {5, 2, 3, -1}, {4, 3, -1}, {9, 10, 8, -1}, {7, 10, -1}, {9, 10, -1}, {0, -1}, {5, 2, -1}, {5, 2, -1}, {10, 7, -1}, {9, 7, 10, -1}, {7, 10, -1}, {-1},
    },
};
/* a chord from outside the pool, once in a walk: the first of these with the fewest notes outside (one or two).
 * major: bVIImaj9 (Mixolydian's); IVmaj9 for Lydian, its lost natural fourth; bIIImaj9 for Mixolydian.
 * minor: IVadd9 with the Dorian's major third; bVImaj9 for Dorian, its lost flat sixth; then bIImaj9 */
static const int8_t BORROW[2][3][2] = {   /* root, seventh above it (-1: the ninth and the root instead) */
    {{10, 11}, {5, 11}, {3, 11}},
    {{5, -1}, {8, 11}, {1, 11}},
};

/* the voices, as functions of the chord's root */
enum { F1, F3, F5, F7, F9, F11, F13 };
typedef struct {
    uint8_t bass, col, sus;                      /* col: a passing colour; sus: no third */
    uint8_t keys[4], pad[4];
} hshape_t;
static const hshape_t HSHAPE[EN_HARM_N] = {
    /* bass col sus  keys                pad */
    {F1, F9,  0, {F1, F3, F5, F7},   {F1, F3, F5, F1}},    /* Gmaj7 */
    {F1, F1,  0, {F3, F5, F7, F9},   {F1, F3, F5, F9}},    /* Gmaj9 */
    {F1, F1,  0, {F3, F13, F9, F5},  {F1, F3, F5, F13}},   /* G6/9 */
    {F1, F11, 0, {F1, F3, F5, F7},   {F1, F3, F5, F7}},    /* F#m7 */
    {F1, F1,  0, {F3, F11, F5, F7},  {F1, F3, F5, F7}},    /* F#m11 */
    {F1, F1,  0, {F3, F5, F7, F9},   {F1, F3, F5, F7}},    /* Bm9 */
    {F1, F11, 0, {F1, F3, F5, F7},   {F1, F3, F5, F7}},    /* Bm7 */
    {F1, F13, 0, {F1, F9, F3, F5},   {F1, F3, F5, F9}},    /* Aadd9 */
    {F1, F9,  1, {F1, F11, F5, F7},  {F1, F11, F5, F7}},   /* A7sus4 */
    {F1, F1,  0, {F3, F13, F9, F5},  {F1, F3, F5, F13}},   /* A6/9 */
    {F1, F11, 0, {F3, F5, F7, F9},   {F1, F3, F5, F7}},    /* Em9 */
    {F1, F9,  0, {F1, F3, F5, F7},   {F1, F3, F5, F7}},    /* Em7 */
    {F3, F9,  0, {F5, F7, F1, F3},   {F3, F5, F1, F7}},    /* Dmaj7/F#: the tonic chord, over its third */
    {F3, F7,  0, {F1, F3, F5, F9},   {F3, F5, F1, F9}},    /* Gadd9/B */
    {F1, F1,  1, {F7, F9, F11, F13}, {F1, F11, F5, F7}},   /* A13sus */
    {F1, F1,  1, {F5, F7, F9, F11},  {F1, F11, F5, F7}},   /* A9sus */
    {F1, F1,  0, {F3, F5, F7, F9},   {F1, F3, F5, F7}},    /* (borrowed: BORROW makes it) */
    {F1, F11, 0, {F3, F5, F7, F9},   {F1, F3, F5, F7}},    /* the second cycle's Em9 */
    {F1, F9,  0, {F1, F3, F5, F7},   {F1, F3, F5, F7}},    /* its Em7 */
    {F1, F1,  0, {F3, F5, F7, F9},   {F1, F3, F5, F7}},    /* its Bm9 */
    {F1, F1,  1, {F5, F7, F9, F11},  {F1, F11, F5, F7}},   /* its A9sus */
};
/* the sparse family's: sus and open chords only */
enum { SP_S2, SP_S4, SP_S2_7, SP_S4_7, SP_S2_6, SP_N };
static const struct {
    uint8_t col;
    uint8_t keys[4], pad[4];
} SP_SHAPE[SP_N] = {
    {F11, {F1, F9, F5, F1},   {F1, F5, F9, F1}},
    {F9,  {F1, F11, F5, F1},  {F1, F5, F11, F1}},
    {F11, {F1, F9, F5, F7},   {F1, F5, F9, F1}},
    {F9,  {F1, F11, F5, F7},  {F1, F5, F11, F1}},
    {F11, {F1, F9, F5, F13},  {F1, F5, F9, F1}},
};
static const uint8_t SP_OF[EN_HARM_N] = {SP_S4, SP_S2, SP_S4_7, SP_S4, SP_S4_7, SP_S2, SP_S4_7, SP_S2, SP_S4_7, SP_S2_6,
                                         SP_S2, SP_S4, SP_S2, SP_S2, SP_S4_7, SP_S2_7, SP_S2, SP_S2, SP_S4, SP_S2, SP_S2_7};

int en_pool_family(uint32_t p)
{
    int i, n = 0;
    p = (p | 1u) & 0xFFFu;
    for (i = 0; i < 12; i++)
        n += (int)(p >> i & 1u);
    if (n < 6 || !(p >> 3 & 1u || p >> 4 & 1u))
        return FAM_SPARSE;
    if (p >> 4 & 1u && p >> 3 & 1u)              /* both thirds: whichever the sixth agrees with */
        return (p >> 8 & 1u) && !(p >> 9 & 1u) ? FAM_MINOR : FAM_MAJOR;
    return p >> 4 & 1u ? FAM_MAJOR : FAM_MINOR;
}

#define IN_POOL(p, r, iv) ((p) >> (((r) + (iv)) % 12) & 1u)

static int root_tones(uint32_t p, int r)         /* how many notes a chord on r can have: never b9, b5, b13 above it */
{
    static const uint8_t OK[8] = {2, 3, 4, 5, 7, 9, 10, 11};
    int i, n = 1;
    for (i = 0; i < 8; i++)
        n += (int)IN_POOL(p, r, OK[i]);
    return n;
}

static int root_stands(uint32_t p, int r, int fam)
{
    if (!(p >> r & 1u))
        return 0;
    if (fam == FAM_SPARSE)
        return root_tones(p, r) >= 3;
    return IN_POOL(p, r, 7) && (IN_POOL(p, r, 3) || IN_POOL(p, r, 4));
}

static int root_of(uint32_t p, int fam, int role)
{
    const int8_t *c = ROOTS[fam][role];
    int i, d, want;
    for (i = 0; i < 4 && c[i] >= 0; i++) {
        const int r = c[i] & 15;
        if (root_stands(p, r, fam) && (!(c[i] & ROOT_MINOR) || (IN_POOL(p, r, 3) && !IN_POOL(p, r, 4))))
            return r;
    }
    /* a pool of its own: the nearest root that stands; failing that the nearest with something over it, and not the tonic */
    want = c[0] & 15;
    for (i = 0; i < 3; i++)
        for (d = 0; d <= 6; d++) {
            const int a = (want + 12 - d) % 12, b = (want + d) % 12;
            if (a && p >> a & 1u && (i == 0 ? root_stands(p, a, fam) : root_tones(p, a) >= 4 - i))
                return a;
            if (b && p >> b & 1u && (i == 0 ? root_stands(p, b, fam) : root_tones(p, b) >= 4 - i))
                return b;
        }
    return 0;                                    /* only the root in the pool */
}

/* a function's interval above the chord's root, -1 if the pool has none */
static int fn_iv(uint32_t p, int r, int third, int fn)
{
    int iv = -1;
    switch (fn) {
    case F1: return 0;
    case F3: iv = third; break;
    case F5: iv = 7; break;
    case F7: iv = third == 4 ? 11 : 10; break;
    case F9: iv = 2; break;
    case F11: iv = third == 4 ? -1 : 5; break;
    default: iv = third == 3 ? -1 : 9; break;
    }
    return iv >= 0 && IN_POOL(p, r, iv) ? iv : -1;
}

/* what stands in for a function the pool does not have: the first not yet used */
static int fn_instead(uint32_t p, int r, int third, int fn, uint32_t used)
{
    static const uint8_t CHAIN[7][5] = {
        {F1, F1, F1, F1, F1},                    /* (always there) */
        {F11, F9, F5, F7, F13},
        {F7, F9, F11, F13, F1},
        {F9, F13, F11, F5, F1},
        {F1, F7, F11, F5, F13},
        {F9, F5, F7, F13, F1},
        {F9, F5, F7, F1, F11},
    };
    static const uint8_t ANY[8] = {7, 5, 2, 10, 3, 4, 9, 11};
    int i, classes = 0;
    uint32_t u;
    for (u = used; u; u &= u - 1u)
        classes++;
    if (fn == F9 && third < 0 && IN_POOL(p, r, 5) && !(used >> 5 & 1u))
        return 5;                                /* a sus chord with no second: the fourth */
    for (i = 0; i < 5; i++) {
        const int iv = fn_iv(p, r, third, CHAIN[fn][i]);
        if (iv >= 0 && !(used >> iv & 1u))
            return iv;
    }
    for (i = 0; i < 8; i++)                      /* a small pool: anything that may stand over this root; a sus chord of */
        if (IN_POOL(p, r, ANY[i]) && !(used >> ANY[i] & 1u) &&   /* three tones stays open rather than take a third */
            !(third < 0 && classes >= 3 && (ANY[i] == 3 || ANY[i] == 4)))
            return ANY[i];
    return 0;
}

static void fn_voices(uint32_t p, int r, int third, const uint8_t *fns, int8_t *out)
{
    int v, iv[4];
    uint32_t used = 0u;
    for (v = 0; v < 4; v++) {
        iv[v] = fn_iv(p, r, third, fns[v]);
        if (iv[v] >= 0)
            used |= 1u << iv[v];
    }
    for (v = 0; v < 4; v++) {
        if (iv[v] < 0) {
            iv[v] = fn_instead(p, r, third, fns[v], used);
            used |= 1u << iv[v];
        }
        out[v] = (int8_t)((r + iv[v]) % 12);
    }
}

static void chord_tones_of(en_hchord_t *o)
{
    uint32_t m = 1u << o->bass;
    int v;
    for (v = 0; v < 4; v++)
        m |= 1u << o->keys[v] | 1u << o->pad[v];
    o->tones = (uint16_t)m;
}

static int chord_borrow(uint32_t p, int fam, en_hchord_t *o)
{
    int i, best = -1, least = 3;
    if (fam == FAM_SPARSE)
        return 0;
    for (i = 0; i < 3; i++) {
        const int r = BORROW[fam == FAM_MINOR][i][0], s = BORROW[fam == FAM_MINOR][i][1];
        const uint32_t m = 1u << r | 1u << ((r + 4) % 12) | 1u << ((r + 7) % 12) | 1u << ((r + 2) % 12) |
                           (s >= 0 ? 1u << ((r + s) % 12) : 0u);
        int out = 0;
        uint32_t x;
        for (x = m & ~p; x; x &= x - 1u)
            out++;
        if (out >= 1 && out < least) {
            least = out;
            best = i;
        }
    }
    if (best < 0)
        return 0;
    {
        const int r = BORROW[fam == FAM_MINOR][best][0], s = BORROW[fam == FAM_MINOR][best][1];
        const int t = (r + 4) % 12, f = (r + 7) % 12, nine = (r + 2) % 12, top = s >= 0 ? (r + s) % 12 : r;
        o->root = o->bass = o->col = (uint8_t)r;
        o->borrowed = 1u;
        o->keys[0] = (int8_t)t;
        o->keys[1] = (int8_t)f;
        o->keys[2] = (int8_t)top;
        o->keys[3] = (int8_t)nine;
        o->pad[0] = (int8_t)r;
        o->pad[1] = (int8_t)t;
        o->pad[2] = (int8_t)f;
        o->pad[3] = (int8_t)(s >= 0 ? top : nine);
        chord_tones_of(o);
    }
    return 1;
}

/* the chord an id is in this pool: tones in semitones above the tonic */
void en_harm_resolve(uint32_t p, int id, en_hchord_t *o)
{
    const int fam = en_pool_family(p);
    int r, third, sus, v;
    const uint8_t *keys, *pad_f;
    int bfn, cfn;
    p = (p | 1u) & 0xFFFu;
    id = (unsigned)id < EN_HARM_N ? id : 0;
    o->family = (uint8_t)fam;
    o->borrowed = 0u;
    if (id == H_CM9) {
        if (chord_borrow(p, fam, o))
            return;
        id = H_SEC3;                             /* nothing to borrow here: it stays what it was */
    }
    r = root_of(p, fam, HNODE[id].role);
    if (fam == FAM_SPARSE) {
        const int sp = SP_OF[id];
        keys = SP_SHAPE[sp].keys;
        pad_f = SP_SHAPE[sp].pad;
        cfn = SP_SHAPE[sp].col;
        bfn = id == H_DF || id == H_GB ? F5 : F1;  /* over its fifth: an open chord, not its third */
        sus = 1;
    } else {
        keys = HSHAPE[id].keys;
        pad_f = HSHAPE[id].pad;
        cfn = HSHAPE[id].col;
        bfn = HSHAPE[id].bass;
        sus = HSHAPE[id].sus;
    }
    third = -1;
    if (!sus) {
        const int m = IN_POOL(p, r, 4), n = IN_POOL(p, r, 3);
        third = m && n ? (fam == FAM_MINOR ? 3 : 4) : m ? 4 : n ? 3 : -1;
    }
    o->root = (uint8_t)r;
    fn_voices(p, r, third, keys, o->keys);
    fn_voices(p, r, third, pad_f, o->pad);
    v = fn_iv(p, r, third, cfn);
    o->col = (uint8_t)((r + (v >= 0 ? v : fn_instead(p, r, third, cfn, 0u))) % 12);
    v = fn_iv(p, r, third, bfn);
    o->bass = (uint8_t)((r + (v >= 0 ? v : fn_instead(p, r, third, bfn, 1u))) % 12);
    chord_tones_of(o);
    if (!o->bass) {                              /* the tonic stays out of the bass: the nearest other tone of the chord */
        int d;
        for (d = 1; d <= 6; d++) {
            if (o->tones >> ((12 - d) % 12) & 1u) {
                o->bass = (uint8_t)((12 - d) % 12);
                break;
            }
            if (o->tones >> d & 1u) {
                o->bass = (uint8_t)d;
                break;
            }
        }
    }
}

const char *en_harm_name(int id) { return HNODE[(unsigned)id < EN_HARM_N ? id : 0].name; }
int en_harm_count(void) { return EN_HARM_N; }
int en_harm_plain(int id) { return HNODE[(unsigned)id < EN_HARM_N ? id : 0].plain; }
int en_harm_sub(int id) { return HNODE[(unsigned)id < EN_HARM_N ? id : 0].sub; }
int en_harm_home(int slot) { return HOME[slot & 3]; }
int en_harm_section(int slot) { return SECTION[slot & 3]; }

/* in D Ionian (or any Ionian): semitones above the tonic */
#define POOL_IONIAN 0xAB5u
int en_harm_colour(int id)
{
    en_hchord_t c;
    en_harm_resolve(POOL_IONIAN, id, &c);
    return c.col;
}
int en_harm_bass(int id)
{
    en_hchord_t c;
    en_harm_resolve(POOL_IONIAN, id, &c);
    return c.bass;
}
uint32_t en_harm_tones(int id)
{
    en_hchord_t c;
    en_harm_resolve(POOL_IONIAN, id, &c);
    return c.tones;
}

static uint32_t hhash(uint32_t seed, uint32_t cycle, uint32_t what)
{
    uint32_t x = (seed ^ 0x51ED270Bu) + cycle * 0x9E3779B1u + what * 0x85EBCA77u;
    x ^= x >> 15;
    x *= 0xC2B2AE3Du;
    x ^= x >> 13;
    x *= 0x27D4EB2Fu;
    return x ^ x >> 16;
}

static int colour_move(en_harm_t *st, uint32_t h, int skip)      /* a colour on, or back off, one chord; the slot, -1 none */
{
    int k, ncol = 0;
    for (k = 0; k < 4; k++)
        ncol += st->chord[k] != HNODE[st->chord[k]].plain;
    for (k = 0; k < 4; k++) {
        const int s = (int)((h + (uint32_t)k) & 3u), c = st->chord[s], p = HNODE[c].plain;
        if (s == skip || c == H_CM9)
            continue;
        if (c != p) {                            /* a colour goes home */
            st->chord[s] = (uint8_t)p;
            return s;
        }
        if (ncol < 2 && HNODE[p].colour[0] >= 0) { /* never more than two coloured at once */
            const int n = HNODE[p].colour[1] >= 0 ? (int)(h >> 4 & 1u) : 0;
            st->chord[s] = (uint8_t)HNODE[p].colour[n];
            return s;
        }
    }
    return -1;
}

void en_harm_step(en_harm_t *st, uint32_t cycle, uint32_t seed, int level)
{
    int k;
    const uint32_t h0 = hhash(seed, cycle, 0u), h1 = hhash(seed, cycle, 1u), h2 = hhash(seed, cycle, 2u);
    if (cycle == 0u) {
        for (k = 0; k < 4; k++)
            st->chord[k] = HOME[k];
        st->section = st->age = st->exiting = st->since = st->borrowed = 0u;
        st->next_sub = (uint8_t)(2u + (h0 >> 8) % 3u);
        if (level < 3)
            return;
    }
    if (level >= 3) {
        if (!st->section) {                      /* the second cycle, all at once: the arrival */
            for (k = 0; k < 4; k++)
                st->chord[k] = SECTION[k];
            st->section = 1u;
            st->age = 0u;
            st->exiting = 0u;
            return;
        }
        st->age++;
        if (st->chord[3] == H_CM9)               /* it was only passing */
            st->chord[3] = H_SEC3;
        else if (st->age == 1u && !st->borrowed) {   /* the borrowed chord, once in a walk: it leads back round to the ii */
            st->chord[3] = H_CM9;
            st->borrowed = 1u;
        }
        if (st->age == 1u || (h1 & 3u)) {
            int s = colour_move(st, h1 >> 4, 3);
            (void)s;
        }
        return;
    }
    if (st->section) {                           /* out, in two moves: the outer chords, then the inner */
        st->section = 0u;
        st->exiting = 1u;
        st->chord[0] = HOME[0];
        st->chord[3] = HOME[3];
        return;
    }
    if (st->exiting) {
        st->exiting = 0u;
        st->chord[1] = HOME[1];
        st->chord[2] = HOME[2];
        st->since = 0u;
        return;
    }
    /* the drift */
    {
        int changed = -1;
        if (level >= 1 && ++st->since >= st->next_sub) {
            int nsub = 0, s;
            for (k = 0; k < 4; k++)
                nsub += HNODE[st->chord[k]].plain != HOME[k];
            st->since = 0u;
            st->next_sub = (uint8_t)(2u + (h0 >> 8) % 3u);
            if (nsub && (h1 >> 8) % 3u < (uint32_t)nsub) {   /* the more it has wandered, the likelier it comes back */
                for (k = 0; k < 4; k++) {
                    s = (int)((h1 + (uint32_t)k) & 3u);
                    if (HNODE[st->chord[s]].plain != HOME[s]) {
                        st->chord[s] = HOME[s];
                        changed = s;
                        break;
                    }
                }
            } else {
                for (k = 0; k < 4; k++) {
                    s = (int)((h1 + (uint32_t)k) & 3u);
                    if (HNODE[st->chord[s]].plain == HOME[s] && HNODE[HOME[s]].sub >= 0) {
                        st->chord[s] = (uint8_t)HNODE[HOME[s]].sub;
                        changed = s;
                        break;
                    }
                }
            }
        }
        /* the colours: most cycles one, now and then two; with a substitution at most one */
        if (cycle >= 1u && (h2 & 3u)) {
            const int a = colour_move(st, h2 >> 4, changed);
            if (changed < 0 && a >= 0 && (h2 >> 12 & 3u) == 0u)
                colour_move(st, h2 >> 16, a);
        }
    }
}

int en_song_level(uint32_t cycle)                /* the fixed timeline's harmonic level at a cycle's seam */
{
    const uint32_t s = cycle * CYCLE_STEPS;
    int i;
    for (i = 0; PART[i + 1].step <= s && PART[i + 1].step < SONG_STEPS; i++)
        ;
    return PART[i].harm;
}

/* the pure function: the cycle's four chords. level is this cycle's; the ones before it come from the timeline */
void en_cycle_chords(uint32_t cycle, uint32_t seed, int level, uint8_t *out)
{
    en_harm_t st;
    uint32_t c;
    int k;
    for (c = 0u; c <= cycle; c++)
        en_harm_step(&st, c, seed, c == cycle ? level : en_song_level(c));
    for (k = 0; k < 4; k++)
        out[k] = st.chord[k];
}

/* the walk's next step and what it is in this pool and key: state, pool, root, cycle, seed, level in; state and chords out */
void en_harm_walk(en_harm_t *st, uint32_t p, int root, uint32_t cycle, uint32_t seed, int level, en_hchord_t *out)
{
    int k;
    en_harm_step(st, cycle, seed, level);
    for (k = 0; k < 4; k++) {
        en_harm_resolve(p, st->chord[k], &out[k]);
        out[k].tonic = (uint8_t)(((root % 12) + 12) % 12);
    }
}

/* its name, as the screen says it: from its root and the hand's notes */
const char *en_hchord_name(const en_hchord_t *c, char *buf)
{
    int pcs[5], v;
    pcs[0] = (c->tonic + c->root) % 12;
    for (v = 0; v < 4; v++)
        pcs[v + 1] = (c->tonic + c->keys[v]) % 12;
    return en_chord_name(pcs, 5, buf);
}

/* the note of the pool nearest n; the lower of two as near */
int en_pool_nearest(uint32_t p, int root, int n)
{
    int d;
    p = (p | 1u) & 0xFFFu;
    for (d = 0; d < 12; d++) {
        if (p >> (((n - d - root) % 12 + 12) % 12) & 1u)
            return n - d;
        if (p >> (((n + d - root) % 12 + 12) % 12) & 1u)
            return n + d;
    }
    return n;
}

/* ------------------------------------------------- the chords as notes --- */
#define KEYS_LO(r) (52 + (r))
#define KEYS_HI(r) (71 + (r))
#define SEC_UP 4                                 /* the second cycle's hand: higher */
#define PAD_LO(r) (49 + (r))
#define PAD_HI(r) (67 + (r))

static void sort4(int *a)
{
    int i, j;
    for (i = 1; i < 4; i++)
        for (j = i; j > 0 && a[j - 1] > a[j]; j--) {
            int t = a[j];
            a[j] = a[j - 1];
            a[j - 1] = t;
        }
}

/* no two voices on one note: a doubled one goes to the nearest free note of the chord in lo..hi; a pool so small that
 * there is none (a chord of two or three tones, all near the window's edge) may use the two octaves from base */
static void voices_apart(int *n, uint32_t tones, int root, int lo, int hi, int base)
{
    int v, w, d, pass;
    for (v = 1; v < 4; v++) {
        int dup = 0;
        for (w = 0; w < v; w++)
            dup |= n[w] == n[v];
        if (!dup)
            continue;
        for (pass = 0; pass < 2 && dup; pass++) {
            const int bot = pass ? base : lo, top = pass ? base + 23 : hi;
            for (d = 1; d < 24 && dup; d++) {
                int side;
                for (side = 0; side < 2 && dup; side++) {
                    const int c = side ? n[v] - d : n[v] + d;
                    int free = c >= bot && c <= top && (tones >> (((c - root) % 12 + 12) % 12) & 1u);
                    for (w = 0; w < 4 && free; w++)
                        free = w == v || n[w] != c;
                    if (free) {
                        n[v] = c;
                        dup = 0;
                    }
                }
            }
        }
    }
}

static void lead_to(const int8_t *ch, uint32_t tones, const int *from, int root, int lo, int hi, int base, uint8_t *out)
{
    int to[4], pcs[4], v;
    for (v = 0; v < 4; v++)
        pcs[v] = (root + ch[v]) % 12;
    en_voice_lead(from, pcs, 4, to, lo, hi);
    voices_apart(to, tones, root, lo, hi, base);
    sort4(to);
    for (v = 0; v < 4; v++)
        out[v] = (uint8_t)to[v];
}

void en_home_voicing(uint32_t p, int root, uint8_t keys[4][4], uint8_t pad_[4][4])
{
    en_hchord_t h[4];
    int from[4], pass, c, v;
    for (c = 0; c < 4; c++)
        en_harm_resolve(p, HOME[c], &h[c]);
    for (v = 0; v < 4; v++)
        from[v] = 57 + root + 4 * v;
    for (pass = 0; pass < 3; pass++)             /* the last pass starts from where the cycle ends: it closes */
        for (c = 0; c < 4; c++) {
            lead_to(h[c].keys, h[c].tones, from, root, KEYS_LO(root), KEYS_HI(root), KEYS_LO(root), keys[c]);
            for (v = 0; v < 4; v++)
                from[v] = keys[c][v];
        }
    for (v = 0; v < 4; v++)
        from[v] = 49 + root + 4 * v;
    for (pass = 0; pass < 3; pass++)
        for (c = 0; c < 4; c++) {
            lead_to(h[c].pad, h[c].tones, from, root, PAD_LO(root), PAD_HI(root), PAD_LO(root), pad_[c]);
            for (v = 0; v < 4; v++)
                from[v] = pad_[c][v];
        }
}

/* from one cycle's voicings to the next: a chord that stays is not touched; a chord back home has its home voicing;
 * any other is led from the voicing this slot had. Into the section the hand is led along the cycle, and higher.
 * anew: the pool or the key changed at this seam: nothing stays, all start again from the new key's home voicing */
void en_voice_cycle(const en_harm_t *was, const en_harm_t *now, const en_hchord_t *ch, int anew, int root,
                    const uint8_t home_k[4][4], const uint8_t home_p[4][4], uint8_t keys[4][4], uint8_t pad_[4][4])
{
    const int enter = now->section && (!was->section || anew);
    const int up = now->section ? SEC_UP : 0;
    int s, v, from[4];
    if (anew)
        for (s = 0; s < 4; s++)
            for (v = 0; v < 4; v++) {
                keys[s][v] = home_k[s][v];
                pad_[s][v] = home_p[s][v];
            }
    if (enter)
        for (v = 0; v < 4; v++)
            from[v] = home_k[3][v] + SEC_UP;
    for (s = 0; s < 4; s++) {
        const int id = now->chord[s];
        if (enter) {
            lead_to(ch[s].keys, ch[s].tones, from, root, KEYS_LO(root) + up, KEYS_HI(root) + up, KEYS_LO(root), keys[s]);
            for (v = 0; v < 4; v++)
                from[v] = keys[s][v];
        } else if (id == was->chord[s] && !anew) {
            continue;
        } else if (id == HOME[s]) {
            for (v = 0; v < 4; v++)
                keys[s][v] = home_k[s][v];
        } else {
            for (v = 0; v < 4; v++)
                from[v] = keys[s][v];
            lead_to(ch[s].keys, ch[s].tones, from, root, KEYS_LO(root) + up, KEYS_HI(root) + up, KEYS_LO(root), keys[s]);
        }
        if (id == HOME[s]) {
            for (v = 0; v < 4; v++)
                pad_[s][v] = home_p[s][v];
        } else if (id != was->chord[s] || enter || anew) {
            int pf[4];
            for (v = 0; v < 4; v++)
                pf[v] = pad_[s][v];
            lead_to(ch[s].pad, ch[s].tones, pf, root, PAD_LO(root), PAD_HI(root), PAD_LO(root), pad_[s]);
        }
    }
}

/* the same, replayed: for the tests */
void en_cycle_voicing_pool(uint32_t cycle, uint32_t seed, int level, uint32_t p, int root, uint8_t keys[4][4], uint8_t pad_[4][4])
{
    uint8_t hk[4][4], hp[4][4];
    en_harm_t st, was;
    en_hchord_t ch[4];
    uint32_t c;
    int s, v;
    en_home_voicing(p, root, hk, hp);
    for (s = 0; s < 4; s++)
        for (v = 0; v < 4; v++) {
            keys[s][v] = hk[s][v];
            pad_[s][v] = hp[s][v];
        }
    en_harm_step(&st, 0u, seed, cycle == 0u ? level : en_song_level(0u));
    for (c = 1u; c <= cycle; c++) {
        was = st;
        en_harm_walk(&st, p, root, c, seed, c == cycle ? level : en_song_level(c), ch);
        en_voice_cycle(&was, &st, ch, 0, root, (const uint8_t (*)[4])hk, (const uint8_t (*)[4])hp, keys, pad_);
    }
}
void en_cycle_voicing(uint32_t cycle, uint32_t seed, int level, int root, uint8_t keys[4][4], uint8_t pad_[4][4])
{
    en_cycle_voicing_pool(cycle, seed, level, POOL_IONIAN, root, keys, pad_);
}

/* the engine's: this cycle's chords and voicings, the next one's (made a little ahead, the bass leads into it). Chords, bass
 * and held voices sound h_pool and h_root until the seam; single notes and motifs follow pool and par[P_ROOT] at once */
static en_harm_t h_cur, h_next;
static en_hchord_t h_ch[4], h_chn[4];
static uint32_t h_pool = POOL_IONIAN;
static int h_root;
static uint8_t keys_v[4][4], pad_v[4][4], keys_n[4][4], pad_n[4][4], home_k[4][4], home_p[4][4], home_kn[4][4], home_pn[4][4];
static int8_t bass_root[4];                      /* semitones above the tonic, in the bass's octave */
volatile uint8_t en_cycle_now[4];
#define HARM_SEED EN_HARM_SEED

static int nearest_tone(uint32_t m, int t)       /* t (semitones) if it's in the chord, else the chord's nearest to it */
{
    int d;
    for (d = 0; d <= 6; d++) {
        if (m >> ((t - d + 120) % 12) & 1u)
            return t - d;
        if (m >> ((t + d + 120) % 12) & 1u)
            return t + d;
    }
    return t;
}

static void publish_chord(int c)                 /* the screen: the chord's root and the hand's notes */
{
    uint32_t m = 1u << h_ch[c].root;
    int v, n = 0;
    for (v = 0; v < EN_NPAD; v++) {
        en_chord[v] = keys_v[c][v];
        m |= 1u << h_ch[c].keys[v];
    }
    for (; m; m &= m - 1u)
        n++;
    en_chord_root = (uint8_t)((h_root + h_ch[c].root) % 12);
    en_chord_n = (uint8_t)n;
}

/* a note about to be played, chosen for the chords that sound (h_pool, h_root), made to obey the pool and the root as they
 * are now: moved with the key, then to the nearest note the pool has. While nothing has changed it stays as it is */
static int live_note(int n)
{
    const int r = par[P_ROOT];
    int d = r - h_root;
    if (pool == h_pool && !d)
        return n;
    if (d > 6)
        d -= 12;
    else if (d < -5)
        d += 12;
    return en_pool_nearest(pool, r, n + d);
}

/* ---------------------------------------------------------------- pad --- */
/* Beneath the piano, carrying the flow: two soft saws to a voice, a few cents apart, one weaker and each leaning to its
 * own side so the pair moves in the field more than it pulses in level; then a dark 12 dB low-pass. It never lets go: a
 * voice whose note changes glides there over a third of a second, and the whole swells a little with each chord. */
typedef struct {
    float ph[2], inc[2], amp, tgt, now;          /* now: the pitch sounding, on its way to note */
    uint8_t note;
} pad_t;
static pad_t pad[EN_NPAD];
static float pad_gain, pad_gate, pad_vol, pad_swell = 0.8f, pad_swell_t = 0.8f;
static const float PAD_PAN[EN_NPAD] = {-0.45f, -0.15f, 0.15f, 0.45f};

static inline float blep_saw(float t, float dt)
{
    float y = 2.0f * t - 1.0f;
    if (t < dt) {
        float x = t / dt;
        y -= x + x - x * x - 1.0f;
    } else if (t > 1.0f - dt) {
        float x = (t - 1.0f) / dt;
        y -= x * x + x + x + 1.0f;
    }
    return y;
}

/* ---------------------------------------------------------- electric piano --- */
/* A tine piano in FM, played to flow: one carrier and two modulators. A modulator at the carrier's pitch is the warm
 * body; one at 14 times its pitch is the tine, kept very low. A chord blooms: a slow attack, and the body's index opens
 * after it rather than at it. The damper stays down: a chord holds until the next one, and then what the next chord
 * does not have lets go over a second or two, so one dissolves into the other. What it does have is not struck again:
 * it swells gently, phase unbroken. Between the changes, now and then one soft note. */
#define NKEYS 12
typedef struct {
    float cph, tph, cinc, tinc;
    float amp, dec, rel, att, arate;
    float ib, ibt, ibd, it, itd, gl, gr, last;
    int32_t wait, hold;                          /* hold: samples before the key comes up; 0 held or already up */
    uint8_t on, note, held;
} ep_t;
static ep_t ep[NKEYS];
static int ep_lim = NKEYS;
void en_shed(void) { ep_lim = 6; }
static float ep_vol, song_touch = 0.9f;
static float ep_gone[2];                         /* what a stolen voice was saying, let down quickly: no click */
volatile uint32_t en_ep_notes, en_ep_chords, en_ep_singles, en_ep_steals;   /* notes struck; chord gestures; lone notes; cuts */
volatile uint8_t en_ep_peak;                     /* the most voices sounding at once */

static void ep_key_up(ep_t *e)
{
    e->hold = 0;
    e->held = 0;
    e->dec = e->rel;
    midi(0x80, e->note, 0);
}

static float ep_amp_of(float vel) { return 0.42f + 0.58f * fm_clampf(vel * song_touch, 0.05f, 1.0f); }

static ep_t *ep_strike(int n, float vel, float att_s, int32_t hold, int32_t wait)
{
    int i, best = 0;
    float lo = 9.0f, v2, top, p;
    ep_t *e;
    while (n > 96)
        n -= 12;
    for (i = 0; i < ep_lim; i++) {
        float a = ep[i].on ? ep[i].amp : -1.0f;
        if (ep[i].on && ep[i].note == n) {       /* the same key again: its own voice */
            best = i;
            break;
        }
        if (a < lo) {
            lo = a;
            best = i;
        }
    }
    e = &ep[best];
    if (e->on) {
        if (e->note != n && e->amp > 0.004f)
            en_ep_steals++;
        ep_gone[0] += e->last * e->gl;           /* (last: at the bus's gain) */
        ep_gone[1] += e->last * e->gr;
        midi(0x80, e->note, 0);
    }
    e->amp = ep_amp_of(vel);
    vel = fm_clampf(vel * song_touch, 0.05f, 1.0f);
    v2 = vel * vel;
    top = 1.0f - 0.022f * fm_clampf((float)(n - 67), 0.0f, 24.0f);   /* the high notes are less bright for the same touch */
    e->att = 0.0f;
    e->cph = 0.0f;
    e->tph = 0.0f;
    e->on = 1;
    e->held = 0;
    e->note = (uint8_t)n;
    e->cinc = mtof((float)n) * (1.0f / EN_SR);
    e->tinc = e->cinc * 14.0f;
    e->dec = fm_expf(-6.9f / (fm_clampf(16.0f - (float)(n - 48) * 0.14f, 9.0f, 18.0f) * EN_SR));
    e->rel = fm_expf(-1.0f / (0.45f * EN_SR));
    e->arate = 1.0f / (att_s * EN_SR);
    e->ib = 0.0f;                                /* the body opens after the attack, then slowly closes */
    e->ibt = (0.10f + 0.95f * v2) * top * (1.0f / FM_TWO_PI);
    e->ibd = fm_expf(-1.0f / (1.7f * EN_SR));
    e->it = (0.008f + 0.10f * v2) * top * (1.0f / FM_TWO_PI);
    e->itd = fm_expf(-1.0f / (0.055f * EN_SR));
    p = (fm_clampf((float)(n - 62) * 0.03f, -0.35f, 0.35f) + 1.0f) * (FM_PI / 4.0f);
    e->gl = fm_cosf(p);
    e->gr = fm_sinf(p);
    e->wait = wait;
    e->hold = hold;
    en_ep_notes++;
    midi(0x90, (uint32_t)n, (uint32_t)(vel * 120.0f));
    return e;
}

/* one soft chord a change: common tones carried over, the rest let go, the new ones rolled up 40 .. 120 ms */
static void ep_chord(int c, float vel, int with_root)
{
    int notes[5], nn = 0, i, k, covered[5], todo = 0;
    float roll;
    int32_t wait, gap;
    const int oct = 12 * par[P_BOCT];
    if (with_root) {
        const int r = (h_root + h_ch[c].bass) % 12;
        notes[nn++] = (r < 4 ? 48 : 36) + r + oct;
    }
    for (k = 0; k < 4; k++)
        notes[nn++] = keys_v[c][k] + oct;
    for (k = 0; k < nn; k++)
        covered[k] = 0;
    for (i = 0; i < ep_lim; i++) {
        ep_t *e = &ep[i];
        int keep = 0;
        if (!e->on)
            continue;
        for (k = 0; k < nn; k++)
            if (notes[k] == e->note && !covered[k]) {
                covered[k] = keep = 1;
                break;
            }
        if (keep) {                              /* it sounds already: no attack, a gentle swell to the new chord's level */
            const float was = e->amp * e->att, to = ep_amp_of(vel);
            if (to > was) {
                e->att = was / to;
                e->amp = to;
                e->arate = 1.0f / (0.9f * EN_SR);
            }
            e->dec = fm_expf(-6.9f / (fm_clampf(16.0f - (float)((int)e->note - 48) * 0.14f, 9.0f, 18.0f) * EN_SR));
            e->ibt = fm_maxf(e->ibt, 0.5f * (0.10f + 0.95f * vel * vel) * (1.0f / FM_TWO_PI));
            e->held = 1;
            e->hold = 0;
        } else if (e->held || e->hold > 0) {
            ep_key_up(e);
        }
    }
    for (k = 0; k < nn; k++)
        todo += !covered[k];
    if (!todo)
        return;
    en_ep_chords++;
    roll = (0.040f + 0.080f * frand()) * EN_SR;
    gap = todo > 1 ? (int32_t)(roll / (float)(todo - 1)) : 0;
    wait = irand(120);
    for (k = 0; k < nn; k++)
        if (!covered[k]) {
            const float v = vel * (with_root && k == 0 ? 0.8f : k == nn - 1 ? 1.03f : 0.95f);
            ep_t *e = ep_strike(notes[k], v, 0.075f, 0, wait);
            e->held = 1;
            wait += gap + irand(gap / 4 + 1);
        }
}

/* the notes between the changes: on a step, a tone relative to a voice of the hand; soft, single. When a chord has
 * nothing to add at that place, nothing is played. */
typedef struct {
    uint8_t step, voice, colour, vel;            /* colour: the chord's passing tone above that voice; else that voice an octave up */
} inner_t;
#define INNER_MAX 3
static const inner_t INNER[4][2][INNER_MAX] = {
    {{{38, 1, 1, 30}}, {{46, 2, 0, 28}}},                                        /* alone: rarely */
    {{{30, 1, 1, 30}}, {{44, 2, 1, 30}}},
    {{{22, 1, 1, 32}, {46, 2, 0, 30}}, {{32, 2, 1, 32}, {52, 1, 0, 28}}},
    {{{14, 2, 0, 34}, {30, 1, 1, 34}, {46, 2, 1, 32}}, {{22, 1, 0, 34}, {38, 2, 1, 34}, {54, 0, 0, 30}}},   /* level 3: the lift */
};
static uint8_t inner_set;                        /* moves only at the seam */

#define LONE_MAX 3u                              /* a chord's lone notes, whatever asks for them */
static uint32_t ep_lone;                         /* this chord's so far */
static uint8_t fresh;                            /* a note a key just added to the pool, + 1: it speaks on the next eighth */

static void ep_inner(const inner_t *g, int c)
{
    const float vel = (float)g->vel * 0.01f * (0.94f + 0.12f * frand());
    const int v0 = keys_v[c][g->voice];
    int n, i;
    if (g->colour) {
        const int want = (h_root + h_ch[c].col) % 12;
        n = v0 + ((want - v0) % 12 + 12) % 12;
    } else {
        n = v0 + 12;
    }
    n = live_note(n) + 12 * par[P_BOCT];
    for (i = 0; i < ep_lim; i++)
        if (ep[i].on && ep[i].note == n && ep[i].held)
            return;                              /* the hand has it already: leave it */
    if (ep_lone >= LONE_MAX)
        return;
    ep_lone++;
    en_ep_singles++;
    ep_strike(n, vel, 0.030f, (int32_t)(12.0f * step_len), irand(300));
}

/* a key put a note in the pool: heard now, one soft note just over the hand, within the chord's lone notes */
static void ep_fresh(int c)
{
    const int pc = (par[P_ROOT] + fresh - 1) % 12, top = keys_v[c][3];
    int n = top + ((pc - top) % 12 + 12) % 12, i;
    if (!(pool >> (fresh - 1) & 1u)) {           /* taken out again meanwhile */
        fresh = 0u;
        return;
    }
    if (ep_lone >= LONE_MAX)
        return;                                  /* the next chord's, then */
    fresh = 0u;
    if (n == top)
        n += 12;
    n += 12 * par[P_BOCT];
    for (i = 0; i < ep_lim; i++)
        if (ep[i].on && ep[i].note == n && ep[i].held)
            return;
    ep_lone++;
    en_ep_singles++;
    ep_strike(n, 0.34f * (0.94f + 0.12f * frand()), 0.030f, (int32_t)(12.0f * step_len), irand(300));
}

/* ------------------------------------------------------------------ bass --- */
/* A round sub on the roots. FM again, a modulator at its own pitch: a little more index as a note speaks, so it is
 * heard as a note on small speakers too, and almost none after. Its own envelope: 12 ms up, falling slowly, gone
 * 60 ms after its time. */
static struct {
    float ph, inc, inc_t, amp, sus, idx, spk;
    int32_t hold;
    uint8_t midi_note;
} bass;
static float bass_vol, bass_song, bass_song_t;
volatile uint32_t en_bass_notes;

/* the line: two-bar figures, a note on a step and what it is. Each note lasts until the next, so a figure names only where
 * they fall; a few of them with the kick (0, 10, 16, 26), the others across it. A chord's second half may end in a tone
 * leading to the next chord's root. The seed chooses, figure by figure */
enum { BV_ROOT, BV_FIFTH, BV_OCT };
typedef struct {
    uint8_t step, what;
} bnote_t;
#define BFIG_N 6
#define BFIG_MAX 6
static const bnote_t BFIG[BFIG_N][BFIG_MAX] = {
    {{0, BV_ROOT}, {10, BV_ROOT}, {16, BV_FIFTH}, {26, BV_ROOT}, {255, 0}},                   /* lighter */
    {{0, BV_ROOT}, {12, BV_FIFTH}, {16, BV_ROOT}, {26, BV_OCT}, {255, 0}},
    {{0, BV_ROOT}, {6, BV_OCT}, {16, BV_ROOT}, {22, BV_FIFTH}, {255, 0}},
    {{0, BV_ROOT}, {10, BV_ROOT}, {16, BV_FIFTH}, {22, BV_OCT}, {26, BV_ROOT}, {255, 0}},     /* fuller */
    {{0, BV_ROOT}, {6, BV_FIFTH}, {10, BV_ROOT}, {16, BV_OCT}, {26, BV_FIFTH}, {255, 0}},
    {{0, BV_ROOT}, {10, BV_OCT}, {14, BV_FIFTH}, {16, BV_ROOT}, {26, BV_FIFTH}, {255, 0}},
};
#define BASS_LEAD_STEP 62u

static int bass_off(int b)                       /* the chord's bass note: in octave 1, E and D a little higher */
{
    return b < 4 ? b + 12 : b;
}

/* what the bass plays on a step of a chord, at levels 2 and 3: a pure function. Its note in semitones above the tonic (in
 * the bass's octaves), and how many steps until the next one; -1 nothing on this step. Never the tonic. id is the walk's,
 * for the seeded figure; ch and nx what this chord and the next are in the pool p */
int en_bass_ch(int id, const en_hchord_t *ch, const en_hchord_t *nx, uint32_t p, uint32_t cycle, uint32_t cs, int level,
               uint32_t seed, int *len)
{
    const uint32_t half = cs / 32u;
    const uint32_t h = hhash(seed, cycle, 40u + (uint32_t)HNODE[(unsigned)id < EN_HARM_N ? id : 0].hid * 7u + half * 3u);
    const uint32_t pick = (level == 3 ? 3u : 0u) + (h >> 8) % 3u;
    const bnote_t *f = BFIG[pick];
    const int root = bass_off(ch->bass), next = bass_off(nx->bass);
    const int lead = half == 1u && root != next && (h >> 16 & 3u) != 0u;
    uint32_t in = cs - half * 32u;
    int i, off = -1;
    if (lead && cs == BASS_LEAD_STEP) {
        /* a step from the next root, in the key, not the tonic */
        static const int8_t TRY[4] = {-1, 1, -2, 2};
        for (i = 0; i < 4; i++) {
            const int t = next + TRY[i], pc = (t + 120) % 12;
            if (pc && p >> pc & 1u) {
                off = t;
                break;
            }
        }
        if (off >= 0) {
            *len = 2;
            return off;
        }
    }
    for (i = 0; i < BFIG_MAX && f[i].step != 255u; i++)
        if (f[i].step == in) {
            const int iv = f[i].what == BV_ROOT ? 0 : f[i].what == BV_FIFTH ? 7 : 12;
            uint32_t to = 32u;
            off = iv == 7 ? nearest_tone(ch->tones, root + 7) : root + iv;
            if ((off + 120) % 12 == 0)               /* the tonic stays out of the bass: the root's octave instead */
                off = root + 12;
            if (off > 20)
                off -= 12;
            if (i + 1 < BFIG_MAX && f[i + 1].step != 255u)
                to = f[i + 1].step;
            *len = (int)(to - in);
            if (lead && half == 1u && to == 32u)
                *len = (int)(BASS_LEAD_STEP - cs);
            return off;
        }
    return -1;
}
int en_bass_at(int id, int next_id, uint32_t cycle, uint32_t cs, int level, uint32_t seed, int *len)
{
    en_hchord_t a, b;
    en_harm_resolve(POOL_IONIAN, id, &a);
    en_harm_resolve(POOL_IONIAN, next_id, &b);
    return en_bass_ch(id, &a, &b, POOL_IONIAN, cycle, cs, level, seed, len);
}

/* a note. It lasts almost until the next: held at full level, a short release, and when the last one still sounds this one
 * is not attacked, it glides there in about 50 ms, phase unbroken */
static void bass_note(int off, uint32_t len)
{
    const int n = 24 + h_root + off + 12 * par[P_BASSOCT];
    const int legato = bass.amp > 0.05f;
    if (bass.midi_note)
        midi(0x82, bass.midi_note, 0);
    bass.midi_note = (uint8_t)n;
    midi(0x92, (uint32_t)n, 90);
    bass.inc_t = mtof((float)n) * (1.0f / EN_SR);
    if (!legato)
        bass.inc = bass.inc_t;
    bass.sus = 1.0f;
    bass.spk = legato ? 0.035f : 0.12f;
    bass.hold = (int32_t)(((float)len - 0.12f) * step_len);
    en_bass_notes++;
}

/* ------------------------------------------------------------- the motifs --- */
/* Two short phrases, a call and its answer: up, up, a step back. A soft muted vibraphone: harmonic ratios only, a low
 * index, a soft attack. Each phrase lies inside one chord, early in it, and each note is moved to a tone of that chord, so
 * the note and its echoes all sit in what sounds. The call on the first chord; the answer on the last, where it stops
 * on a tone that wants to fall and doesn't. Rests much longer than the phrases; the last call goes unanswered. */
#define NBELL 4
typedef struct {
    float cph, mph, cinc, minc, amp, adec, idx, idec, att, arate, gl, gr;
    int32_t wait;
    uint8_t on, note, kind;
} bell_t;
static bell_t bell[NBELL];
static float bell_vol;
typedef struct {
    uint8_t step;                                /* in the cycle */
    int8_t note;                                 /* above the tonic, an octave over middle C */
    uint8_t vel;
} mnote_t;
static const mnote_t MOTIF[2][4] = {
    {{8, 0, 44}, {11, 4, 46}, {14, 9, 54}, {20, 7, 50}},             /* 1 3 6, 5 */
    {{200, 4, 40}, {203, 7, 44}, {206, 11, 50}, {214, 9, 52}},       /* 3 5 7, 6 */
};
static int8_t motif_now[2][4];                   /* this cycle's notes: the phrase, each moved to its chord */
/* each cycle: bit 0 the call, bit 1 the answer */
static const uint8_t MOTIF_WHEN[12] = {0, 2, 0, 3, 0, 1, 3, 0, 1, 0, 1, 0};

static void bell_strike(int kind, int n, float vel, int32_t wait)
{
    int i, best = 0;
    float lo = 9.0f, p;
    bell_t *v;
    for (i = 0; i < NBELL; i++) {
        float a = bell[i].on ? bell[i].amp : -1.0f;
        if (a < lo) {
            lo = a;
            best = i;
        }
    }
    v = &bell[best];
    if (v->on)
        midi(0x81, v->note, 0);
    v->on = 1;
    v->kind = (uint8_t)kind;
    v->note = (uint8_t)n;
    v->cph = v->mph = 0.0f;
    v->cinc = mtof((float)n) * (1.0f / EN_SR);
    v->amp = vel;
    v->att = 0.0f;
    /* one family: the fundamental and a little of its neighbours, gone quickly. The answer: its second harmonic's ratio,
     * a lighter index */
    v->minc = v->cinc * (kind == 0 ? 1.0f : 2.0f);
    v->idx = (kind == 0 ? 0.30f + 0.25f * vel : 0.14f + 0.14f * vel) * (1.0f / FM_TWO_PI);
    v->idec = fm_expf(-1.0f / (0.13f * EN_SR));
    v->adec = fm_expf(-6.9f / ((kind == 0 ? 2.4f : 2.2f) * EN_SR));
    v->arate = 1.0f / (0.0040f * EN_SR);         /* about 10 ms to its top */
    p = ((kind == 0 ? -0.30f : 0.30f) + 1.0f) * (FM_PI / 4.0f);
    v->gl = fm_cosf(p);
    v->gr = fm_sinf(p);
    v->wait = wait;
    en_loop_glow[kind] = 1.0f;
    midi(0x91, (uint32_t)n, (uint32_t)(vel * 110.0f));
}

/* ----------------------------------------------------------------- the timeline --- */
/* per level: the stems' levels, the low-pass on the break (Hz), the hats' level, the pad's filter */
static const float CORE_LVL[5] = {0.0f, 0.0f, 0.86f, 1.0f, 0.55f}, TOP_LVL[5] = {0.0f, 0.0f, 0.5f, 0.6f, 0.4f};
static const float BRK_FC[5] = {6000.0f, 6000.0f, 13000.0f, 16000.0f, 6000.0f}, TOP_FC[5] = {7000.0f, 7000.0f, 11000.0f, 12000.0f, 7000.0f};
static const float HAT_LVL[5] = {0.0f, 0.70f, 0.85f, 1.05f, 0.6f};
static const float PAD_FC[5] = {640.0f, 680.0f, 760.0f, 900.0f, 680.0f};

/* the hats: made here, at the full rate, since a stem has nothing above 11 kHz. A few detuned squares and some noise,
 * high-passed: one closed voice, one open, choked by the next closed. Very dry, past the drum bus. The swing is a sample
 * countdown: the off ones a few ms late. Small differences of decay and level with each hit. */
#define HAT_HP 0.68f                             /* 1 - exp(-2 pi 8 kHz / 44.1 kHz) */
typedef struct {
    float amp, dec, lp1, lp2;
    int32_t wait;
    uint8_t on;
} hat_t;
static hat_t hat[2];                             /* 0 closed, 1 open */
static uint32_t hat_ph[4], hat_noise = 0x6C8E9CF5u;
static float hat_lvl, hat_lvl_t;
static const uint32_t HAT_INC[4] = {300400000u, 448200000u, 589700000u, 781100000u};   /* 3.1, 4.6, 6.1, 8.0 kHz, inharmonic */
volatile uint8_t en_hat_peak;

static void hat_hit(char c, float vel, int32_t wait)
{
    const uint32_t h = hat_noise *= 2654435761u;
    const float vary = (float)(h >> 24) * (1.0f / 255.0f);
    const int open = c == 'o';
    hat_t *v = &hat[open];
    float tau = open ? 0.024f + 0.008f * vary : 0.0065f + 0.0025f * vary;
    if (c == 'j')
        wait += 130;                             /* 3 ms */
    else if (c == 'l')
        wait += 260;                             /* 6 ms */
    else if (c == 'i') {
        wait += 180;
        vel *= 0.7f;
        tau *= 0.8f;
    }
    v->amp = vel * (0.92f + 0.16f * vary) * (open ? 0.62f : 1.0f);
    v->dec = fm_expf(-1.0f / (tau * EN_SR));
    v->wait = wait;
    v->on = 1;
    if (!open && hat[1].on)
        hat[1].dec = fm_expf(-1.0f / (0.0035f * EN_SR));   /* choked */
}

static uint32_t song;
static int song_level;
static float brk_fc, brk_fc_t, pad_fc, pad_fc_t, duck, duck_s, lift, lift_t;

static void level_set(const part_t *p, int first)
{
    const float ramp = first ? 1.0f : (float)p->bars * 16.0f * step_len;
    song_level = p->level;
    hat_lvl_t = HAT_LVL[p->level];
    brk_levels(CORE_LVL[p->level], TOP_LVL[p->level], ramp);
    brk_fc_t = BRK_FC[p->level];
    pad_fc_t = PAD_FC[p->level];
    bass_song_t = (float)p->bass * 0.01f;
    song_touch = (float)p->touch * 0.01f;
    lift_t = p->harm == 3 || (p->level == 3 && lift_t > 0.0f) ? 1.0f : 0.0f;
}

/* the next cycle's notes, from h_next and the pool and the key as they are now */
static uint32_t prep_pool;
static int prep_root;
static void next_voices(void)
{
    const int r = par[P_ROOT], anew = pool != h_pool || r != h_root;
    int s, v;
    for (s = 0; s < 4; s++)
        en_harm_resolve(pool, h_next.chord[s], &h_chn[s]);
    if (anew)
        en_home_voicing(pool, r, home_kn, home_pn);
    for (s = 0; s < 4; s++)
        for (v = 0; v < 4; v++) {
            if (!anew) {
                home_kn[s][v] = home_k[s][v];
                home_pn[s][v] = home_p[s][v];
            }
            keys_n[s][v] = keys_v[s][v];
            pad_n[s][v] = pad_v[s][v];
        }
    en_voice_cycle(&h_cur, &h_next, h_chn, anew, r, (const uint8_t (*)[4])home_kn, (const uint8_t (*)[4])home_pn, keys_n, pad_n);
    prep_pool = pool;
    prep_root = r;
}

/* the next cycle, a little ahead of its seam */
static void cycle_prepare(uint32_t cycle)
{
    h_next = h_cur;
    en_harm_step(&h_next, cycle, HARM_SEED, en_song_level(cycle));
    next_voices();
}

static void cycle_commit(void)                   /* on the seam, and only there */
{
    int s, v, m, n;
    h_cur = h_next;
    h_pool = prep_pool;
    h_root = prep_root;
    for (s = 0; s < 4; s++) {
        h_ch[s] = h_chn[s];
        for (v = 0; v < 4; v++) {
            keys_v[s][v] = keys_n[s][v];
            pad_v[s][v] = pad_n[s][v];
            home_k[s][v] = home_kn[s][v];
            home_p[s][v] = home_pn[s][v];
        }
        bass_root[s] = (int8_t)bass_off(h_ch[s].bass);
        en_cycle_now[s] = h_cur.chord[s];
    }
    for (m = 0; m < 2; m++)
        for (n = 0; n < 4; n++)                  /* a motif's note that clashes moves to the nearest tone of its chord */
            motif_now[m][n] = (int8_t)nearest_tone(h_ch[MOTIF[m][n].step / CHORD_STEPS].tones, MOTIF[m][n].note);
}

/* for the tests: a motif's note, its step in the cycle, its note (semitones) over this cycle's chords */
int en_motif_step(int m, int n) { return MOTIF[m & 1][n & 3].step; }
int en_motif_note(int m, int n, const uint8_t *chords)
{
    return nearest_tone(en_harm_tones(chords[MOTIF[m & 1][n & 3].step / CHORD_STEPS]), MOTIF[m & 1][n & 3].note);
}
int en_motif_note_ch(int m, int n, const en_hchord_t *ch)
{
    return nearest_tone(ch[MOTIF[m & 1][n & 3].step / CHORD_STEPS].tones, MOTIF[m & 1][n & 3].note);
}

static void harmony_restart(void)                /* the top of the song */
{
    int s, v;
    en_home_voicing(pool, par[P_ROOT], home_kn, home_pn);
    for (s = 0; s < 4; s++)
        for (v = 0; v < 4; v++) {
            keys_n[s][v] = home_kn[s][v];
            pad_n[s][v] = home_pn[s][v];
        }
    en_harm_step(&h_next, 0u, HARM_SEED, en_song_level(0u));
    for (s = 0; s < 4; s++)
        en_harm_resolve(pool, h_next.chord[s], &h_chn[s]);
    prep_pool = pool;
    prep_root = par[P_ROOT];
    cycle_commit();
}

static void song_step(uint32_t s)
{
    const uint32_t cs = s % CHORD_STEPS, cy = s % CYCLE_STEPS, cycle = s / CYCLE_STEPS;
    const int c = (int)(s / CHORD_STEPS & 3u);
    int i, flags = 0;
    float k;

    if (s == 0u) {
        inner_set = 0;
        harmony_restart();
    } else if (!cy) {
        if (prep_pool != pool || prep_root != par[P_ROOT])   /* changed in the very last step */
            cycle_prepare(cycle);
        cycle_commit();
    }
    if (cy == CYCLE_STEPS - 8u)
        cycle_prepare(cycle + 1u);
    else if (cy > CYCLE_STEPS - 8u && (prep_pool != pool || prep_root != par[P_ROOT]))
        cycle_prepare(cycle + 1u);               /* changed since: this seam takes it */
    if (!(s & 15u)) {                            /* on the bar: the timeline */
        for (i = 0; PART[i + 1].step <= s; i++)
            ;
        if (PART[i].step == s)
            level_set(&PART[i], s == 0u);
    }
    /* the kit holds its breath in the last bar before a higher level, with the air rising into it */
    for (i = 0; PART[i + 1].step <= s; i++)
        ;
    if ((PART[i + 1].level == 2 || PART[i + 1].level == 3) && PART[i].level != PART[i + 1].level) {
        const uint32_t to = PART[i + 1].step - s;
        if (to <= 16u && PART[i].level >= 2)
            flags |= BRK_DROP;
        if (to == 12u)
            flags |= BRK_SWELL;
    }
    k = brk_pattern_step(s, song_level, flags, HARM_SEED);
    if (k > 0.0f)
        duck = fm_maxf(duck, k);
    if (brk_hat_hit != '.')
        hat_hit(brk_hat_hit, brk_hat_vel, irand(40));

    /* the pad: on the change the voices move to their new notes, gliding, and the whole swells */
    if (cs == 0u) {
        for (i = 0; i < EN_NPAD; i++) {
            pad[i].note = pad_v[c][i];
            pad[i].tgt = 1.0f;
        }
        publish_chord(c);
        pad_swell_t = 1.0f;
    } else if (cs == 24u) {
        pad_swell_t = 0.80f;
    }
    pad_gate = s < PAD_OFF ? 1.0f : 0.0f;

    /* the piano */
    if (!cy)
        inner_set = (uint8_t)en_song_level(cycle);
    if (!cs)
        ep_lone = 0u;
    if (s <= LAST_CHORD) {
        if (!cs)
            ep_chord(c, 0.44f + 0.04f * frand(), bass_song_t <= 0.0f);
        if (s < LAST_CHORD)
            for (i = 0; i < INNER_MAX; i++) {
                const inner_t *g = &INNER[inner_set][cycle & 1u][i];
                /* each chord its own place: a little later along the cycle, so no two are the same */
                if (g->vel && (uint32_t)g->step + 2u * (uint32_t)c == cs)
                    ep_inner(g, c);
            }
        if (fresh && !(s & 1u) && s < LAST_CHORD)
            ep_fresh(c);
    } else if (s == 2800u) {                     /* the last chord lets go, slowly */
        for (i = 0; i < NKEYS; i++)
            if (ep[i].on && ep[i].held) {
                ep[i].rel = fm_expf(-1.0f / (0.9f * EN_SR));
                ep_key_up(&ep[i]);
            }
    }

    /* the bass */
    if (song_level == 2 || song_level == 3) {
        int len = 0;
        /* into a new key the bass does not lead: the next root's step would mean nothing in this one */
        const en_hchord_t *nx = c < 3 ? &h_ch[c + 1] : prep_root == h_root ? &h_chn[0] : &h_ch[c];
        const int off = en_bass_ch(h_cur.chord[c], &h_ch[c], nx, h_pool, cycle, cs, song_level, HARM_SEED, &len);
        if (off >= 0)
            bass_note(off, (uint32_t)len);
    } else if (bass_song_t > 0.0f && !cs) {
        bass_note(bass_root[c], 64u);   /* the breakdown and the way out: long held roots */
    }

    /* the motifs */
    if (cycle < 12u) {
        const uint32_t when = MOTIF_WHEN[cycle];
        int m, n;
        for (m = 0; m < 2; m++)
            if (when >> m & 1u)
                for (n = 0; n < 4; n++)
                    if (MOTIF[m][n].step == cy) {
                        const int note = live_note(72 + h_root + motif_now[m][n]) + 12 * par[P_BOCT];
                        bell_strike(m, note, (float)MOTIF[m][n].vel * 0.01f, irand(180));
                        en_loop_pos[m] = (uint8_t)n;
                    }
    }
    en_drift = (float)cs * (1.0f / (float)CHORD_STEPS);
}

/* the grid: one call a sixteenth (MIDI clock: 6 ticks) */
static uint32_t sixteenth;
static void tick(void)
{
    if (!en_frozen) {
        song_step(song);
        if (++song >= SONG_STEPS)
            song = 0u;
    }
    sixteenth++;
    en_bars = sixteenth;
}

/* ------------------------------------------------- reverb: Dattorro's plate --- */
/* J. Dattorro, "Effect Design, Part 1", JAES 1997 (as in FoMni): a predelay, four input diffusers,
 * then a figure-eight tank of two halves, each a modulated allpass, a delay, a damping lowpass, an
 * allpass and a delay, feeding the other half. Stereo out from seven taps a side. The paper's lengths
 * are for 29761 Hz; DS() scales them to 44.1 kHz. */
#define DS(n) (((n) * 14818 + 5000) / 10000)          /* 44100 / 29761 */
#define PRE_N 1323                                   /* 30 ms */
#define EXC DS(16)
enum { L_PRE, L_IN1, L_IN2, L_IN3, L_IN4, L_APL, L_D1L, L_AP2L, L_D2L, L_APR, L_D1R, L_AP2R, L_D2R, L_N };
static const int DL_LEN[L_N] = {PRE_N, DS(142), DS(107), DS(379), DS(277), DS(672) + EXC + 2, DS(4453), DS(1800),
                                DS(3720), DS(908) + EXC + 2, DS(4217), DS(2656), DS(3163)};
#define DL_TOTAL (PRE_N + DS(142) + DS(107) + DS(379) + DS(277) + DS(672) + DS(4453) + DS(1800) + DS(3720) + DS(908) + \
                  DS(4217) + DS(2656) + DS(3163) + 2 * (EXC + 2))
static float pl_buf[DL_TOTAL] EN_POOL;
typedef struct {
    float *b;
    int n, i;
} dl_t;
static dl_t dl[L_N];
static float pl_bw, pl_damp_l, pl_damp_r, pl_decay = 0.5f, pl_decay_t = 0.5f, pl_dd2 = 0.5f, lfo_s, lfo_c = 1.0f;

static inline float tap(const dl_t *d, int k)
{
    int j = d->i - k;
    return d->b[j < 0 ? j + d->n : j];
}
static inline void push(dl_t *d, float x)
{
    d->b[d->i] = x;
    if (++d->i >= d->n)
        d->i = 0;
}
static inline float allpass(dl_t *d, int len, float x, float g)
{
    float z = tap(d, len), v = x - g * z;
    push(d, fm_flush(v));
    return z + g * v;
}
static inline float allpass_mod(dl_t *d, float len, float x, float g)
{
    int k = (int)len;
    float f = len - (float)k, z = tap(d, k) + (tap(d, k + 1) - tap(d, k)) * f, v = x - g * z;
    push(d, fm_flush(v));
    return z + g * v;
}

static inline void reverb(float in, float *ol, float *or_)
{
    float x, a, b, fb_l = tap(&dl[L_D2R], DS(3163)), fb_r = tap(&dl[L_D2L], DS(3720));
    push(&dl[L_PRE], in);
    x = tap(&dl[L_PRE], PRE_N);
    pl_bw += 0.6f * (x - pl_bw);
    x = allpass(&dl[L_IN1], DS(142), pl_bw, 0.75f);
    x = allpass(&dl[L_IN2], DS(107), x, 0.75f);
    x = allpass(&dl[L_IN3], DS(379), x, 0.625f);
    x = allpass(&dl[L_IN4], DS(277), x, 0.625f);
    {
        float s = lfo_s + 0.0001f * lfo_c, c = lfo_c - 0.0001f * s;
        lfo_s = s;
        lfo_c = c;
    }
    a = allpass_mod(&dl[L_APL], (float)DS(672) + (float)EXC * lfo_s, x + pl_decay * fb_l, -0.7f);
    push(&dl[L_D1L], a);
    b = tap(&dl[L_D1L], DS(4453));
    pl_damp_l += 0.45f * (b - pl_damp_l);
    b = allpass(&dl[L_AP2L], DS(1800), pl_damp_l * pl_decay, pl_dd2);
    push(&dl[L_D2L], b);
    a = allpass_mod(&dl[L_APR], (float)DS(908) + (float)EXC * lfo_c, x + pl_decay * fb_r, -0.7f);
    push(&dl[L_D1R], a);
    b = tap(&dl[L_D1R], DS(4217));
    pl_damp_r += 0.45f * (b - pl_damp_r);
    b = allpass(&dl[L_AP2R], DS(2656), pl_damp_r * pl_decay, pl_dd2);
    push(&dl[L_D2R], b);
    *ol = 0.6f * (tap(&dl[L_D1R], DS(266)) + tap(&dl[L_D1R], DS(2974)) - tap(&dl[L_AP2R], DS(1913)) +
                  tap(&dl[L_D2R], DS(1996)) - tap(&dl[L_D1L], DS(1990)) - tap(&dl[L_AP2L], DS(187)) -
                  tap(&dl[L_D2L], DS(1066)));
    *or_ = 0.6f * (tap(&dl[L_D1L], DS(353)) + tap(&dl[L_D1L], DS(3627)) - tap(&dl[L_AP2L], DS(1228)) +
                   tap(&dl[L_D2L], DS(2673)) - tap(&dl[L_D1R], DS(2111)) - tap(&dl[L_AP2R], DS(335)) -
                   tap(&dl[L_D2R], DS(121)));
}

static void plate_init(void)
{
    float *p = pl_buf;
    int k, i;
    for (k = 0; k < L_N; k++) {
        dl[k].b = p;
        dl[k].n = DL_LEN[k];
        dl[k].i = 0;
        p += DL_LEN[k];
    }
    for (i = 0; i < DL_TOTAL; i++)
        pl_buf[i] = 0.0f;
    pl_bw = pl_damp_l = pl_damp_r = 0.0f;
    lfo_s = 0.0f;
    lfo_c = 1.0f;
}

/* ------------------------------------------------- echo: tape and Echoplex --- */
/* After Discreet Music's chain: an Echoplex (a short, smearing echo, ~160 ms, fed back) and a tape loop
 * (long: up to ~5.6 s, synced to the tempo), each repeat a little darker and thinner, with a little
 * wow. The tape runs at a quarter rate (11 kHz, 16-bit, mono): it loses its highs anyway. Two heads:
 * the first plays the loop's length, the second a part of it (3/4 at first). With Motion the echo
 * evolves: each head wanders across the field, the feedback leans now on one head, now on the other
 * (the repeats fall in changing patterns, left and right), and now and then the second head slides to
 * another part of the loop, as a head moved by hand (the repeats bend a little in pitch on the way).
 * The Echoplex has a second tap, for the right. */
#define TAPE_N 62000                             /* 5.6 s at 11.025 kHz */
#define TAPE_SR 11025.0f
static int16_t tape[TAPE_N] EN_POOL;
static int tape_w, tape_ph;
static float tape_acc, tape_lp, tape_hp, tape_p1, tape_p2, tape_c1, tape_c2;
static const float HEAD2[5] = {0.75f, 0.6667f, 0.625f, 0.5f, 0.375f};   /* the second head's parts */
static float head2 = 0.75f, head2_t = 0.75f, head2_v;   /* its place (of the loop), target, speed */
static float fb_lean;                            /* 0..1: how much of the feedback is the second head's */
static float hg[4] = {1.0f, 0.0f, 0.0f, 1.0f};   /* the heads' pan gains: 1 left, 1 right, 2 left, 2 right */
static uint32_t head_left;                       /* samples until the second head may move */
#define PLEX_N 7100                              /* 160 ms at 44.1 kHz */
#define PLEX_TAP 2700                            /* the right's tap: ~100 ms */
static int16_t plex[PLEX_N];
static int plex_w;
static float plex_lp, plex_lp2;

static inline float tape_read(float d)           /* d quarter-rate samples ago */
{
    int k = (int)d, i0, i1;
    float f = d - (float)k, a, c;
    i0 = tape_w - k;
    i1 = i0 - 1;
    if (i0 < 0)
        i0 += TAPE_N;
    if (i1 < 0)
        i1 += TAPE_N;
    a = (float)tape[i0];
    c = (float)tape[i1];
    return (a + (c - a) * f) * (1.0f / 12000.0f);
}
static inline int16_t s16(float x)
{
    x *= 12000.0f;
    return (int16_t)(x > 32767.0f ? 32767.0f : x < -32767.0f ? -32767.0f : x);
}

static void echo_time(void)
{
    int i = par[P_ETIME];
    float beat, t;
    if (par[P_TEMPO] <= 0)
        return;                                  /* not yet (en_init applies in order) */
    beat = 60.0f / (float)par[P_TEMPO];
    t = ETIME_BEATS[i] * beat;
    while (t * TAPE_SR > (float)(TAPE_N - 8) && i > 0)
        t = ETIME_BEATS[--i] * beat;             /* too long for the tape: the next shorter */
    if (t * TAPE_SR > (float)(TAPE_N - 8))
        t = (float)(TAPE_N - 8) / TAPE_SR;
    echo_len_t = t * TAPE_SR;
    if (echo_len <= 0.0f)
        echo_len = echo_len_t;
}

/* once a chunk: where the heads sit in the field, how the feedback leans, the second head's slide */
static void heads_step(uint32_t n)
{
    float p, want, vmax;
    int i;
    for (i = 0; i < 2; i++) {
        p = fm_clampf((i ? 0.7f : -0.7f) + 0.9f * motion * mod[6 + i].v, -1.0f, 1.0f);
        p = (p + 1.0f) * (FM_PI / 4.0f);
        hg[2 * i] = fm_cosf(p);
        hg[2 * i + 1] = fm_sinf(p);
    }
    fb_lean = 0.25f * motion * (1.0f + mod[8].v);
    if (head_left <= n) {
        head_left = (uint32_t)((20.0f + 40.0f * frand()) * EN_SR);
        if (en_playing && !en_frozen && frand() < motion)
            head2_t = HEAD2[(int)(frand() * 4.999f)];
    } else {
        head_left -= n;
    }
    /* its speed (a part of the loop per tape sample): at most a bend of ~14 cents, easing in and out */
    vmax = 0.008f / fm_maxf(echo_len, 100.0f);
    want = fm_clampf((head2_t - head2) * (1.0f / (2.0f * TAPE_SR)), -vmax, vmax);
    head2_v += (want - head2_v) * fm_minf((float)n * (1.0f / EN_SR), 1.0f);
}

/* one sample of both: the input (a mono send) to the tape's heads (el, er) and the Echoplex (xl, xr) */
static inline void echoes(float in, float *el, float *er, float *xl, float *xr)
{
    float y = (float)plex[plex_w] * (1.0f / 12000.0f);         /* the Echoplex: 160 ms, fed back, darker */
    int t = plex_w - PLEX_TAP;
    plex_lp += 0.35f * (y - plex_lp);
    plex_lp2 += 0.35f * ((float)plex[t < 0 ? t + PLEX_N : t] * (1.0f / 12000.0f) - plex_lp2);
    plex[plex_w] = s16(in + 0.55f * plex_lp);
    if (++plex_w >= PLEX_N)
        plex_w = 0;
    *xl = plex_lp;
    *xr = plex_lp2;
    tape_acc += in + 0.3f * plex_lp;
    if (++tape_ph >= 4) {                        /* the tape, a quarter of the time */
        float d, h1, h2, fb;
        tape_ph = 0;
        echo_len += (echo_len_t - echo_len) * 0.001f;
        head2 += head2_v;
        d = echo_len * (1.0f + 0.003f * motion * mod[5].v);   /* wow */
        if (d > (float)(TAPE_N - 4))
            d = (float)(TAPE_N - 4);
        h1 = tape_read(d);
        h2 = tape_read(d * head2);
        tape_lp += 0.55f * (h1 + fb_lean * (h2 - h1) - tape_lp);   /* each time round: darker, thinner */
        tape_hp += 0.03f * (tape_lp - tape_hp);
        fb = tape_lp - tape_hp;
        if (++tape_w >= TAPE_N)
            tape_w = 0;
        tape[tape_w] = s16(tape_acc * 0.25f + echo_fb * fb);
        tape_acc = 0.0f;
        tape_p1 = tape_c1;
        tape_p2 = tape_c2;
        tape_c1 = h1;
        tape_c2 = h2;
    }
    {   /* between the tape's samples: a line from the last to the next; then to the field */
        float f = (float)tape_ph * 0.25f;
        float h1 = tape_p1 + (tape_c1 - tape_p1) * f, h2 = tape_p2 + (tape_c2 - tape_p2) * f;
        *el = hg[0] * h1 + hg[2] * h2;
        *er = hg[1] * h1 + hg[3] * h2;
    }
}

/* ------------------------------------------------------------ parameters --- */
static int pulse;
static uint32_t ext_age;

static void tempo_set(void) { step_len = EN_SR * 60.0f / ((float)par[P_TEMPO] * 4.0f); }

/* the key or the pool changed. Playing: nothing now, the next seam takes it (song_step). Stopped: at once, the walk's
 * place kept, so the screen shows it and the music starts from it */
static void harmony(void)
{
    if (par[P_TEMPO] <= 0)
        return;                                  /* not yet (en_init applies in order) */
    if (!song) {
        harmony_restart();
    } else if (!en_playing) {
        h_next = h_cur;
        next_voices();
        cycle_commit();
    }
    publish_chord((int)(song / CHORD_STEPS & 3u));
}

static void apply(int p, int v)
{
    const en_param_t *pi = &PARAMS[p];
    if (v < pi->lo)
        v = pi->lo;
    if (v > pi->hi)
        v = pi->hi;
    par[p] = (int16_t)v;
    switch (p) {
    case P_PAD: pad_vol = (float)(v * v) * 0.0001f; break;
    case P_BELLS: bell_vol = (float)(v * v) * 0.0001f; break;
    case P_BASS: bass_vol = (float)(v * v) * 0.0001f; break;
    case P_BREAK: break_vol = (float)(v * v) * 0.0001f; break;
    case P_SWELL: ep_vol = (float)(v * v) * 0.0001f; break;   /* in this demo, the electric piano's level */
    case P_KIT: brk_set_kit(v); break;
    case P_ECHO: echo_send = (float)(v * v) * 0.0001f; break;
    case P_ETIME: echo_time(); break;
    case P_REVERB: rev_send = (float)(v * v) * 0.0001f; break;
    case P_SIZE:
        pl_decay_t = 0.6f + 0.258f * (float)v * 0.01f;     /* shorter than AMB's: there is a beat to leave room for */
        echo_fb = 0.5f + 0.22f * (float)v * 0.01f;
        break;
    case P_MOTION: motion = (float)v * 0.0058f; break;   /* the demo sits still: only a little of it */
    case P_TUNE: tunefac = fm_exp2f((float)v * (1.0f / 1200.0f)); break;
    case P_TEMPO:
        tempo_set();
        brk_set_tempo((float)par[P_TEMPO]);
        echo_time();
        break;
    case P_ROOT: harmony(); break;
    case P_MIDI:
        if (!v)
            en_midi_out(0xB0, 123, 0);
        break;
    default: break;
    }
}

static void notes_off(void)
{
    int i;
    for (i = 0; i < 4; i++)
        en_midi_out(0xB0 | (uint32_t)i, 123, 0);
    en_midi_out(0xB9, 123, 0);
}

static void quiet(void)
{
    int i;
    for (i = 0; i < NBELL; i++)
        bell[i].on = 0;
    for (i = 0; i < NKEYS; i++)
        ep[i].on = 0;
    brk_stop();
    for (i = 0; i < EN_NPAD; i++)
        pad[i].amp = 0.0f;
    bass.amp = 0.0f;
    bass.hold = 0;
    notes_off();
}

void en_init(void)
{
    int i, l;
    for (i = 0; i <= SN; i++)
        sint[i] = fm_sinf((float)i * (FM_TWO_PI / (float)SN));
    plate_init();
    for (i = 0; i < TAPE_N; i++)
        tape[i] = 0;
    for (i = 0; i < PLEX_N; i++)
        plex[i] = 0;
    tape_w = plex_w = tape_ph = 0;
    head2 = head2_t = 0.75f;
    head2_v = 0.0f;
    echo_len = 0.0f;
    q_r = q_w = 0;
    for (i = 0; i < NMOD; i++) {
        mod[i].v = mod[i].tgt = 0.0f;
        mod[i].left = 0;
    }
    par[P_TEMPO] = 0;
    for (i = 0; i < EN_NPAD; i++) {
        pad[i].ph[0] = 0.13f * (float)i;
        pad[i].ph[1] = 0.37f + 0.21f * (float)i;
        pad[i].amp = pad[i].tgt = 0.0f;
        pad[i].now = 0.0f;
    }
    for (i = 0; i < NKEYS; i++)
        ep[i].on = 0;
    for (i = 0; i < NBELL; i++)
        bell[i].on = 0;
    pad_gain = pad_gate = 0.0f;
    pad_swell = pad_swell_t = 0.8f;
    en_ep_notes = en_ep_chords = en_ep_singles = en_ep_steals = 0u;
    en_bass_notes = 0u;
    en_ep_peak = 0;
    ep_gone[0] = ep_gone[1] = 0.0f;
    ep_lim = NKEYS;
    duck = duck_s = 0.0f;
    lift = lift_t = 0.0f;
    hat_lvl = hat_lvl_t = 0.0f;
    hat[0].on = hat[1].on = 0;
    en_hat_peak = 0;
    bass.amp = bass.idx = bass.spk = 0.0f;
    bass.hold = 0;
    bass.midi_note = 0;
    bass_song = bass_song_t = 0.0f;
    brk_fc = brk_fc_t = BRK_FC[0];
    pad_fc = pad_fc_t = PAD_FC[0];
    brk_lp[0] = brk_lp[1] = brk_hp = 0.0f;
    plp[0] = plp[1] = plp[2] = plp[3] = eplp[0] = eplp[1] = 0.0f;
    song = 0u;
    song_level = 0;
    brk_init();                                  /* before apply: it resets what apply sets */
    for (i = 0; i < P_NPARAMS; i++)
        apply(i, PARAMS[i].def);
    pool = EN_MODE_POOL[par[P_MODE]];
    fresh = 0u;
    ep_lone = 0u;
    harmony();
    for (l = 0; l < EN_NLOOP; l++)               /* the screen's loops show the motifs' notes, nothing moves in them */
        for (i = 0; i < EN_LOOP_MAX; i++)
            en_loop_note[l][i] = l < 2 && i < 4 ? MOTIF[l][i].note : -1;
    pl_decay = pl_decay_t;
}

/* ------------------------------------------------------------ transport --- */
static void clock_pulse(void)
{
    if (pulse == 0)
        tick();
    if (!en_ext)
        midi(0xF8, 0, 0);
    if (++pulse >= 6)
        pulse = 0;
}

static void play_start(int from_top)
{
    if (from_top) {
        pulse = 0;
        sixteenth = 0;
        song = 0u;
    }
    pulse_left = 0.0f;
    en_playing = 1;
    if (!en_ext)
        midi(from_top ? 0xFA : 0xFB, 0, 0);
}

static void play_stop(void)
{
    int i;
    brk_stop();
    en_playing = 0;
    pad_gate = 0.0f;
    for (i = 0; i < NKEYS; i++)
        if (ep[i].on)
            ep[i].hold = 1;
    bass.hold = 0;
    if (!en_ext)
        midi(0xFC, 0, 0);
    notes_off();
}

static void drain(void)
{
    while (q_r != q_w) {
        uint32_t c = q[q_r % QN];
        int a = (int)(c >> 16 & 0xFFu), b = (int)(int16_t)(c & 0xFFFFu);
        BARRIER();
        q_r++;
        switch (c >> 24) {
        case C_SET:
            if (a < P_NPARAMS)
                apply(a, b);
            break;
        case C_POOL: {
            const uint32_t now = ((uint32_t)b | 1u) & 0xFFFu, added = now & ~pool, gone = pool & ~now;
            if (added && !gone && !(added & (added - 1u))) {   /* one note more: a key. It speaks */
                int bit = 0;
                while (!(added >> bit & 1u))
                    bit++;
                fresh = (uint8_t)(bit + 1);
            }
            pool = now;
            harmony();
            break;
        }
        case C_PLAY:
            if (a && !en_playing)
                play_start(1);
            else if (!a && en_playing)
                play_stop();
            break;
        case C_NEXT: break;                      /* the cycle is fixed */
        case C_FREEZE: en_frozen = (uint8_t)(a != 0); break;
        case C_SEED:
            rng ^= (uint32_t)(b & 0xFFFF) * 2654435761u;
            if (!rng)
                rng = 1u;
            break;
        case C_CLOCK:
            en_ext = 1;
            brk_set_ext(1);
            ext_age = 0;
            if (a == EN_CLK_TICK) {
                if (en_playing)
                    clock_pulse();
            } else if (a == EN_CLK_START) {
                play_start(1);
            } else if (a == EN_CLK_CONTINUE) {
                if (!en_playing)
                    play_start(0);
            } else if (en_playing) {
                play_stop();
            }
            break;
        case C_PANIC: quiet(); break;
        case C_REGROW:                           /* a new piece: its key; the song goes on */
            harmony();
            ep_lim = NKEYS;
            break;
        }
    }
}

/* --------------------------------------------------------------- render --- */
#define BLK 256
static float mixl[BLK], mixr[BLK], rsend[BLK], esend[BLK];

/* the melodic bus: filled by the pad, then (the same memory) by the piano */
static float padl[BLK], padr[BLK];
static float duck0, duck1;                       /* this chunk's duck, ramped: 0 none .. 1 the kick's full weight */

static void render_pad(uint32_t n)
{
    int i;
    float lvl = 0.0f, c;
    uint32_t j;
    const float g = pad_vol * 0.20f * pad_swell * (1.0f + 0.30f * lift);
    for (j = 0; j < n; j++)
        padl[j] = padr[j] = 0.0f;
    pad_gain += (pad_gate - pad_gain) * fm_minf((float)n / ((pad_gate > 0.0f ? 2.5f : 2.2f) * EN_SR), 1.0f);
    for (i = 0; i < EN_NPAD; i++) {
        pad_t *v = &pad[i];
        float p0 = v->ph[0], p1 = v->ph[1], amp = v->amp, f, gl, gr, pn;
        const float tgt = v->tgt * pad_gain, rate = 1.0f / (0.6f * EN_SR),
                    inc0 = v->inc[0], inc1 = v->inc[1];
        if (amp < 1e-5f && tgt < 1e-4f) {
            v->amp = 0.0f;
            en_pad_amp[i] = 0.0f;
            continue;
        }
        pn = fm_clampf(PAD_PAN[i] + 0.25f * motion * mod[(i + 3) % NMOD_PAD].v, -1.0f, 1.0f);
        gl = fm_cosf((pn + 1.0f) * (FM_PI / 4.0f)) * g;
        gr = fm_sinf((pn + 1.0f) * (FM_PI / 4.0f)) * g;
        for (j = 0; j < n; j++) {
            const float a = blep_saw(p0, inc0) * amp, b = blep_saw(p1, inc1) * amp;
            p0 += inc0;
            if (p0 >= 1.0f)
                p0 -= 1.0f;
            p1 += inc1;
            if (p1 >= 1.0f)
                p1 -= 1.0f;
            amp += (tgt - amp) * rate;
            padl[j] += (a + 0.28f * b) * gl;
            padr[j] += (0.60f * b + 0.42f * a) * gr;
        }
        v->ph[0] = p0;
        v->ph[1] = p1;
        v->amp = fm_flush(amp);
        if (v->now <= 0.0f)
            v->now = (float)v->note;
        v->now += ((float)v->note - v->now) * fm_minf((float)n * (1.0f / (0.11f * EN_SR)), 1.0f);   /* a third of a second */
        f = mtof(v->now + 0.05f * motion * mod[i].v);
        v->inc[0] = f * 0.99856f * (1.0f / EN_SR);           /* -2.5 and +2.5 cents: slow, and warm */
        v->inc[1] = f * 1.00144f * (1.0f / EN_SR);
        en_pad_note[i] = v->note;
        en_pad_amp[i] = amp;
        lvl = fm_maxf(lvl, amp);
    }
    en_pad_level = lvl;
    c = 1.0f - fm_expf(-FM_TWO_PI * fm_clampf(pad_fc * (1.0f + 0.35f * lift + 0.10f * motion * mod[4].v), 200.0f, 4000.0f) * (1.0f / EN_SR));
    {
        const float dd = (duck1 - duck0) * (1.0f / (float)n);
        float dk = duck0;
        for (j = 0; j < n; j++) {
            float l, r, gd;
            plp[0] += c * (padl[j] - plp[0]);
            plp[1] += c * (plp[0] - plp[1]);
            plp[2] += c * (padr[j] - plp[2]);
            plp[3] += c * (plp[2] - plp[3]);
            l = plp[1];
            r = plp[3];
            dk += dd;
            gd = 1.0f - 0.22f * dk;
            mixl[j] += l * gd;
            mixr[j] += r * gd;
            rsend[j] += (l + r) * 0.42f;
            esend[j] += (l + r) * 0.06f;
        }
    }
    for (j = 0; j < 4; j++)
        plp[j] = fm_flush(plp[j]);
}

static float ep_trem;
static void render_keys(uint32_t n)
{
    int i;
    uint32_t j;
    const float g = ep_vol * 0.62f * (1.0f + 0.18f * lift), cl = 0.40f + 0.12f * lift;              /* the bus's one pole: ~3.6 kHz */
    float trem0, trem1, dt, tr, gd, dg;
    {
        int on = 0;
        for (i = 0; i < ep_lim; i++)
            on += ep[i].on;
        if (on > en_ep_peak)
            en_ep_peak = (uint8_t)on;
    }
    for (j = 0; j < n; j++) {
        padl[j] = ep_gone[0];
        padr[j] = ep_gone[1];
        ep_gone[0] *= 0.993f;
        ep_gone[1] *= 0.993f;
    }
    ep_gone[0] = fm_flush(ep_gone[0]);
    ep_gone[1] = fm_flush(ep_gone[1]);
    for (i = 0; i < ep_lim; i++) {
        ep_t *e = &ep[i];
        float cph = e->cph, tph = e->tph, amp = e->amp, ib = e->ib, ibt = e->ibt, it = e->it, att = e->att;
        const float cinc = e->cinc, tinc = e->tinc, ibd = e->ibd, itd = e->itd, ar = e->arate, gl = e->gl * g, gr = e->gr * g;
        float dec = e->dec, x = 0.0f;
        uint32_t k = 0;
        if (!e->on)
            continue;
        if (e->wait > 0) {
            if ((uint32_t)e->wait >= n) {
                e->wait -= (int32_t)n;
                continue;
            }
            k = (uint32_t)e->wait;
            e->wait = 0;
        }
        if (e->hold > 0) {
            e->hold -= (int32_t)n;
            if (e->hold <= 0) {
                ep_key_up(e);
                dec = e->rel;
            }
        }
        if (it > 0.0006f) {                      /* the tine's ping, then a lookup saved */
            for (; k < n; k++) {
                const float m = sine(cph) * (ib + 0.012f) + sine(tph) * it;
                x = sine(cph + m + 2.0f) * amp * att * (1.0f + ib * 1.8f);
                padl[k] += x * gl;
                padr[k] += x * gr;
                cph += cinc;
                tph += tinc;
                if (cph >= 1.0f)
                    cph -= 1.0f;
                if (tph >= 1.0f)
                    tph -= 1.0f;
                amp *= dec;
                ib += (ibt - ib) * 0.00009f;     /* opens over about a quarter of a second */
                ibt *= ibd;
                it *= itd;
                att += (1.0f - att) * ar;
            }
        } else {
            for (; k < n; k++) {
                const float m = sine(cph) * (ib + 0.012f);
                x = sine(cph + m + 2.0f) * amp * att * (1.0f + ib * 1.8f);
                padl[k] += x * gl;
                padr[k] += x * gr;
                cph += cinc;
                if (cph >= 1.0f)
                    cph -= 1.0f;
                amp *= dec;
                ib += (ibt - ib) * 0.00009f;
                ibt *= ibd;
                att += (1.0f - att) * ar;
            }
        }
        e->cph = cph;
        e->tph = tph;
        e->amp = amp;
        e->ib = fm_flush(ib);
        e->ibt = fm_flush(ibt);
        e->it = fm_flush(it);
        e->att = att;
        e->last = x * g;
        if (amp < 0.0035f && !e->held)           /* -45 dB: under everything, and the voice is free */
            e->on = 0;
    }
    /* a slow stereo tremolo: a quarter of 170, just there */
    trem0 = ep_trem;
    ep_trem += (float)n * (1.0f / (4.0f * step_len));
    if (ep_trem >= 1.0f)
        ep_trem -= 1.0f;
    trem0 = sine(trem0);
    trem1 = sine(ep_trem);
    dt = (trem1 - trem0) * (1.0f / (float)n);
    tr = trem0;
    gd = duck0;
    dg = (duck1 - duck0) * (1.0f / (float)n);
    for (j = 0; j < n; j++) {
        float l, r, a, b, dd;
        eplp[0] += cl * (padl[j] - eplp[0]);
        eplp[1] += cl * (padr[j] - eplp[1]);
        tr += dt;
        gd += dg;
        dd = 1.0f - 0.05f * gd;                  /* barely: the piano must not be interrupted */
        a = 1.0f - 0.14f * (1.0f + tr);
        b = 1.0f - 0.14f * (1.0f - tr);
        l = eplp[0];
        r = eplp[1];
        mixl[j] += l * a * dd;
        mixr[j] += r * b * dd;
        rsend[j] += (l + r) * 0.55f;
        esend[j] += (l + r) * 0.42f;
    }
    eplp[0] = fm_flush(eplp[0]);
    eplp[1] = fm_flush(eplp[1]);
}

static void render_bass(uint32_t n)
{
    uint32_t j;
    const float g = bass_vol * 0.86f * bass_song, tgt = bass.hold > 0 ? bass.sus : 0.0f,
                rate = tgt > bass.amp ? 1.0f / (0.012f * EN_SR) : 1.0f / (0.022f * EN_SR);
    const float dd = (duck1 - duck0) * (1.0f / (float)n);
    float dk = duck0;
    float ph = bass.ph, amp = bass.amp, idx = bass.idx, inc = bass.inc, spk = bass.spk;
    if (amp < 1e-5f && bass.hold <= 0) {
        bass.amp = 0.0f;
        en_bass_level = 0.0f;
        return;
    }
    for (j = 0; j < n; j++) {
        const float m = sine(ph) * idx, x = sine(ph + m + 1.0f) * amp * g * (1.0f - 0.45f * dk);
        dk += dd;
        ph += inc;
        if (ph >= 1.0f)
            ph -= 1.0f;
        inc += (bass.inc_t - inc) * 0.0012f;     /* the glide: about 50 ms */
        amp += (tgt - amp) * rate;
        spk *= 0.99965f;                         /* speaks, then settles: ~65 ms */
        idx += (0.034f + spk - idx) * 0.012f;
        mixl[j] += x;
        mixr[j] += x;
        rsend[j] += x * 0.05f;
    }
    bass.ph = ph;
    bass.amp = fm_flush(amp);
    bass.idx = idx;
    bass.spk = fm_flush(spk);
    bass.inc = inc;
    if (bass.hold > 0) {
        bass.hold -= (int32_t)n;
        if (bass.hold <= 0)
            midi(0x82, bass.midi_note, 0);
    }
    en_bass_level = amp;
}

static void render_bells(uint32_t n)
{
    int i;
    const float g = bell_vol * 0.62f;
    for (i = 0; i < NBELL; i++) {
        bell_t *v = &bell[i];
        float cph = v->cph, mph = v->mph, amp = v->amp, idx = v->idx, att = v->att;
        uint32_t j = 0;
        if (!v->on)
            continue;
        if (v->wait > 0) {
            if ((uint32_t)v->wait >= n) {
                v->wait -= (int32_t)n;
                continue;
            }
            j = (uint32_t)v->wait;
            v->wait = 0;
        }
        for (; j < n; j++) {
            float m = sine(mph) * idx, x;
            x = sine(cph + m + 4.0f) * amp * att * g;
            mixl[j] += x * v->gl * 0.85f;        /* more of the note itself, less of the room */
            mixr[j] += x * v->gr * 0.85f;
            rsend[j] += x * 0.55f;
            esend[j] += x * 0.16f;
            cph += v->cinc;
            mph += v->minc;
            if (cph >= 1.0f)
                cph -= 1.0f;
            if (mph >= 1.0f)
                mph -= 1.0f;
            amp *= v->adec;
            idx *= v->idec;
            att += (1.0f - att) * v->arate;
        }
        v->cph = cph;
        v->mph = mph;
        v->amp = amp;
        v->idx = idx;
        v->att = att;
        if (amp < 0.0006f) {
            v->on = 0;
            midi(0x81, v->note, 0);
        }
    }
}

/* the break: far back. The kick and the snare dead centre, the top a little to one side; each through a gentle low-pass
 * that opens with the level; the send high-passed so the plate gets the snare and the air, not the kick */
static float brk_mix[BLK], brk_top[BLK];
static void render_break(uint32_t n)
{
    uint32_t j;
    float pk = 0.0f;
    const float c = 1.0f - fm_expf(-FM_TWO_PI * brk_fc * (1.0f / EN_SR)),
                ct = 1.0f - fm_expf(-FM_TWO_PI * TOP_FC[song_level] * (1.0f / EN_SR));   /* the hats have their own, wide open */
    const float v = break_vol * 2.6f;
    for (j = 0; j < n; j++)
        brk_mix[j] = brk_top[j] = 0.0f;
    brk_render_stem(BRK_CORE, brk_mix, n);
    brk_render_stem(BRK_TOP, brk_top, n);
    for (j = 0; j < n; j++) {
        float x, t, s, a;
        brk_lp[0] += c * (brk_mix[j] - brk_lp[0]);
        brk_lp[1] += ct * (brk_top[j] - brk_lp[1]);
        x = brk_lp[0] * v;
        t = brk_lp[1] * v;
        brk_hp += 0.045f * (x - brk_hp);
        s = x - brk_hp;
        mixl[j] += x + t * 0.52f;
        mixr[j] += x + t * 0.52f;
        rsend[j] += s * 0.16f + t * 0.40f;
        esend[j] += s * 0.01f + t * 0.04f;
        a = x < 0.0f ? -x : x;
        pk = a > pk ? a : pk;
    }
    brk_lp[0] = fm_flush(brk_lp[0]);
    brk_lp[1] = fm_flush(brk_lp[1]);
    brk_hp = fm_flush(brk_hp);
    en_break_level += (pk - en_break_level) * 0.2f;
    en_hits = brk_hits;
}

static void render_hats(uint32_t n)
{
    int i, on = 0;
    const float g = break_vol * hat_lvl * 3.3f;
    hat_lvl += (hat_lvl_t - hat_lvl) * fm_minf((float)n * (1.0f / (0.4f * EN_SR)), 1.0f);
    for (i = 0; i < 2; i++) {
        hat_t *v = &hat[i];
        float amp = v->amp, l1 = v->lp1, l2 = v->lp2;
        const float dec = v->dec;
        uint32_t j = 0, a = hat_ph[0], b = hat_ph[1], c = hat_ph[2], d = hat_ph[3], nz = hat_noise;
        if (!v->on)
            continue;
        on++;
        if (v->wait > 0) {
            if ((uint32_t)v->wait >= n) {
                v->wait -= (int32_t)n;
                continue;
            }
            j = (uint32_t)v->wait;
            v->wait = 0;
        }
        for (; j < n; j++) {
            float x, y;
            a += HAT_INC[0];
            b += HAT_INC[1];
            c += HAT_INC[2];
            d += HAT_INC[3];
            nz ^= nz << 13;
            nz ^= nz >> 17;
            nz ^= nz << 5;
            x = (float)(int32_t)nz * 4.6566e-10f * 0.75f + 0.14f * (float)((int32_t)(a >> 31) + (int32_t)(b >> 31) + (int32_t)(c >> 31) + (int32_t)(d >> 31));
            l1 += HAT_HP * (x - l1);
            x -= l1;
            l2 += HAT_HP * (x - l2);
            y = (x - l2) * amp * g;
            mixl[j] += y * 0.72f;
            mixr[j] += y * 0.92f;
            rsend[j] += y * 0.03f;               /* almost no room, no echo */
            amp *= dec;
        }
        hat_ph[0] = a;
        hat_ph[1] = b;
        hat_ph[2] = c;
        hat_ph[3] = d;
        hat_noise = nz;
        v->amp = amp;
        v->lp1 = fm_flush(l1);
        v->lp2 = fm_flush(l2);
        if (amp < 0.001f)
            v->on = 0;
    }
    if (on > en_hat_peak)
        en_hat_peak = (uint8_t)on;
}

static void render_chunk(int32_t *out, uint32_t n, float gain)
{
    uint32_t j;
    const float rg = rev_send * rev_in_gain * 1.09f, eg = echo_send * 2.1f;
    for (j = 0; j < n; j++)
        mixl[j] = mixr[j] = rsend[j] = esend[j] = 0.0f;
    /* the duck: the kick pulls the melodic bus down a little and lets it back in a quarter of a second */
    duck0 = duck_s;
    duck_s += (duck - duck_s) * fm_minf((float)n * (1.0f / (0.005f * EN_SR)), 1.0f);
    duck *= fm_expf(-(float)n * (1.0f / (0.11f * EN_SR)));
    duck1 = duck_s;
    render_pad(n);
    render_keys(n);
    render_bass(n);
    render_bells(n);
    render_break(n);
    render_hats(n);
    for (j = 0; j < n; j++) {
        float wl, wr, l, r, el, er, xl, xr;
        echoes(esend[j] * eg, &el, &er, &xl, &xr);
        reverb((rsend[j] * rg + (el + er + xl + xr) * 0.35f * rev_send) * 0.8f, &wl, &wr);
        l = (mixl[j] + 0.75f * el + 0.3f * xl + 1.3f * wl) * 0.80f * gain;
        r = (mixr[j] + 0.75f * er + 0.3f * xr + 1.3f * wr) * 0.80f * gain;
        out[2 * j] = (int32_t)(fm_tanhf(l) * 8300000.0f);
        out[2 * j + 1] = (int32_t)(fm_tanhf(r) * 8300000.0f);
    }
}

static void control(uint32_t n)                 /* once a chunk: the slow things */
{
    int i;
    const float slow = (float)n * (1.0f / (6.0f * EN_SR));
    mods_step(n);
    heads_step(n);
    en_arc = 0.0f;
    brk_fc += (brk_fc_t - brk_fc) * fm_minf(slow * (lift_t > 0.0f ? 4.0f : 1.0f), 1.0f);
    pad_fc += (pad_fc_t - pad_fc) * fm_minf(slow * (lift_t > 0.0f ? 4.0f : 1.0f), 1.0f);
    bass_song += (bass_song_t - bass_song) * slow;
    pad_swell += (pad_swell_t - pad_swell) * fm_minf((float)n * (1.0f / (1.1f * EN_SR)), 1.0f);
    lift += (lift_t - lift) * (lift_t > lift ? (float)n * (1.0f / (0.8f * EN_SR)) : slow * 0.75f);
    pl_decay += ((en_frozen ? 0.9995f : pl_decay_t) - pl_decay) * 0.002f;
    pl_dd2 = fm_clampf(pl_decay + 0.15f, 0.25f, 0.5f);
    rev_in_gain += ((en_frozen ? 0.0f : 1.0f) - rev_in_gain) * 0.01f;
    for (i = 0; i < EN_NLOOP; i++)
        en_loop_glow[i] *= 0.985f;
}

void en_render(int32_t *out, uint32_t n, uint32_t gain_q12)
{
    const float gain = (float)gain_q12 * (1.0f / 4096.0f) * 2.0f;
    drain();
    if (en_ext && ++ext_age > 100u) {
        en_ext = 0;
        brk_set_ext(0);
    }
    while (n) {
        uint32_t k = n > BLK ? BLK : n;
        int timed = en_playing && !en_ext;
        if (timed) {
            if (pulse_left <= 0.0f) {
                clock_pulse();
                pulse_left += step_len * (1.0f / 6.0f);
            }
            if ((float)k > pulse_left)
                k = (uint32_t)pulse_left + 1u;
        }
        if (k > n)
            k = n;
        if (timed)
            pulse_left -= (float)k;
        control(k);
        render_chunk(out, k, gain);
        out += 2 * k;
        n -= k;
    }
}
