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
    for (i = 1; i < n; i++)
        iv |= 1u << ((pcs[i] - pcs[0] + 12) % 12);
    third = iv >> 4 & 1u ? 4 : iv >> 3 & 1u ? 3 : 0;
    fifth = iv >> 7 & 1u ? 7 : iv >> 6 & 1u ? 6 : iv >> 8 & 1u ? 8 : 0;
    if (third == 4) {
        q = fifth == 8 ? "aug" : iv >> 11 & 1u ? "M7" : iv >> 10 & 1u ? "7" : iv >> 9 & 1u ? "6"
            : iv >> 2 & 1u ? "add9" : "";
    } else if (third == 3) {
        if (fifth == 6)
            q = iv >> 10 & 1u ? "m7b5" : iv >> 9 & 1u ? "dim7" : "dim";
        else
            q = iv >> 10 & 1u ? "m7" : iv >> 11 & 1u ? "mM7" : iv >> 9 & 1u ? "m6" : iv >> 2 & 1u ? "madd9" : "m";
    } else {
        q = iv >> 5 & 1u ? (iv >> 10 & 1u ? "7sus4" : "sus4") : iv >> 2 & 1u ? "sus2" : "5";
    }
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
static uint32_t pool = 0xAADu;                  /* Ionian until told; the screen's, the demo has its own notes */
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
 * the major key on Root (D, by default), IV iii vi V: Gmaj7 F#m7 Bm9 Aadd9, each 64 sixteenths (5.6 s), repeating
 * exactly. The tonic is never the root of anything and the V goes back to the IV: the cycle leans forward and does not
 * land. Everything below is fixed but for small human errors of time and touch (rnd(): the same for a fixed seed). */
#define CHORD_STEPS 64u
#define CYCLE_STEPS (4u * CHORD_STEPS)
#define SONG_STEPS 2880u                         /* 4:14, then again from the top */
#define LAST_CHORD 2752u                         /* cycle 10's V: the last thing played; its answer never comes */
#define PAD_OFF 2788u

/* semitones above the tonic */
static const int8_t CH_ROOT[4] = {5, 4, 9, 7};                                 /* in D: G F# B A */
static const int8_t CH_KEYS[4][4] = {{5, 9, 0, 4}, {4, 7, 11, 2}, {0, 4, 7, 11}, {7, 9, 11, 2}};   /* maj7, m7, rootless m9, add9 */
static const int8_t CH_PAD[4][4] = {{5, 9, 0, 5}, {4, 7, 11, 2}, {9, 0, 4, 7}, {7, 11, 2, 9}};   /* plainer: the piano has the colours */
static const int8_t CH_COLOUR[4] = {7, 9, 9, 11};                              /* a passing colour above: 9, 11, root, 3 */
static uint8_t keys_v[4][4], pad_v[4][4];        /* the voicings, ascending: en_voice_lead, once round to settle */

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

static void voicings(const int8_t ch[4][4], uint8_t out[4][4], int start, int lo, int hi)
{
    int from[4], to[4], pcs[4], pass, c, v;
    for (v = 0; v < 4; v++)
        from[v] = start + 4 * v;
    for (pass = 0; pass < 3; pass++)             /* the last pass starts from where the cycle ends: it closes */
        for (c = 0; c < 4; c++) {
            for (v = 0; v < 4; v++)
                pcs[v] = (par[P_ROOT] + ch[c][v]) % 12;
            en_voice_lead(from, pcs, 4, to, lo, hi);
            for (v = 0; v < 4; v++)
                from[v] = to[v];
            sort4(to);
            for (v = 0; v < 4; v++)
                out[c][v] = (uint8_t)to[v];
        }
}

static void publish_chord(int c)
{
    int v;
    for (v = 0; v < EN_NPAD; v++)
        en_chord[v] = pad_v[c][v];
    en_chord_root = (uint8_t)((par[P_ROOT] + CH_ROOT[c]) % 12);
    en_chord_n = 4;
}

/* ---------------------------------------------------------------- pad --- */
/* Beneath the piano: two soft saws to a voice, a few cents apart, one weaker and each leaning to its own side so the
 * pair moves in the field more than it pulses in level; then a dark 12 dB low-pass. A voice whose note the next chord
 * keeps simply holds; the others let go just before the change and swell back on their new note. */
typedef struct {
    float ph[2], inc[2], amp, tgt;
    uint8_t note, next;
} pad_t;
static pad_t pad[EN_NPAD];
static float pad_gain, pad_gate, pad_vol;
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
/* A tine piano in FM: one carrier and two modulators. A modulator at the carrier's pitch is the warm body (index by
 * how hard the key went down, falling away to a little); one at 14 times its pitch is the tine, a short soft ping that
 * only a firm touch brings out. Attack a few ms, softer when played softer. Key up: a damper, about 0.1 s. */
#define NKEYS 6
typedef struct {
    float cph, tph, cinc, tinc;
    float amp, dec, rel, att, arate;
    float ib, ibd, it, itd, gl, gr, last;
    int32_t wait, hold;
    uint8_t on, note;
} ep_t;
static ep_t ep[NKEYS];
static int ep_lim = NKEYS;
void en_shed(void) { ep_lim = 4; }
static float ep_vol, song_touch = 0.9f;
static float ep_gone[2];                         /* what a stolen voice was saying, let down quickly: no click */

static void ep_strike(int n, float vel, int32_t hold, int32_t wait)
{
    int i, best = 0;
    float lo = 9.0f, v2, top, p, amp;
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
    if (e->on)
        midi(0x80, e->note, 0);
    vel = fm_clampf(vel * song_touch, 0.05f, 1.0f);
    v2 = vel * vel;
    top = 1.0f - 0.022f * fm_clampf((float)(n - 67), 0.0f, 24.0f);   /* the high notes are less bright for the same touch */
    amp = 0.42f + 0.58f * vel;
    if (e->on) {
        ep_gone[0] += e->last * e->gl;               /* (last: at the bus's gain) */
        ep_gone[1] += e->last * e->gr;
    }
    e->att = 0.0f;
    e->cph = 0.0f;
    e->tph = 0.0f;
    e->on = 1;
    e->note = (uint8_t)n;
    e->cinc = mtof((float)n) * (1.0f / EN_SR);
    e->tinc = e->cinc * 14.0f;
    e->amp = amp;
    e->dec = fm_expf(-6.9f / (fm_clampf(8.0f - (float)(n - 48) * 0.11f, 3.5f, 9.0f) * EN_SR));
    e->rel = fm_expf(-1.0f / (0.10f * EN_SR));
    e->arate = 1.0f / ((0.0065f - 0.0035f * vel) * EN_SR);
    e->ib = (0.10f + 1.25f * v2) * top * (1.0f / FM_TWO_PI);
    e->ibd = fm_expf(-1.0f / (0.38f * EN_SR));
    e->it = (0.02f + 0.42f * v2) * top * (1.0f / FM_TWO_PI);
    e->itd = fm_expf(-1.0f / (0.055f * EN_SR));
    p = (fm_clampf((float)(n - 62) * 0.03f, -0.35f, 0.35f) + 1.0f) * (FM_PI / 4.0f);
    e->gl = fm_cosf(p);
    e->gr = fm_sinf(p);
    e->wait = wait;
    e->hold = hold;
    midi(0x90, (uint32_t)n, (uint32_t)(vel * 120.0f));
}

/* one comping gesture: on its step, these of the chord's voices, this hard, this long */
#define M_LH 0x10u                               /* the root, an octave under the hand: while there is no bass */
#define M_NEXT 0x20u                             /* the next chord's notes: played ahead of the change */
#define M_COL 0x40u                              /* a passing colour above the hand */
typedef struct {
    uint8_t step, mask, vel, len;
} comp_t;
#define COMP_MAX 6
/* how the hand plays each of the four chords, by how far the music has come, in two ways that take turns cycle by
 * cycle. Sparse: a chord lives 5.6 s, and is touched two to five times; off the kick and the snare, on the even
 * sixteenths of the half-time feel */
#define F 0x0Fu
static const comp_t COMP[3][2][4][COMP_MAX] = {
    {   /* alone with the pad: slow, the root in the left hand */
        {
            {{0, 0x1F, 58, 34}, {36, 0x0C, 34, 22}},
            {{0, 0x1F, 54, 34}, {38, 0x06, 34, 16}, {56, M_COL, 30, 6}},
            {{0, 0x1F, 58, 20}, {22, 0x0E, 40, 26}},
            {{0, 0x1F, 52, 44}, {46, 0x0C, 34, 12}},
        },
        {
            {{0, 0x1F, 54, 30}, {32, 0x0E, 38, 22}},
            {{0, 0x1F, 52, 44}, {46, 0x0C, 34, 12}},
            {{0, 0x1F, 56, 18}, {20, 0x0C, 36, 6}, {40, 0x06, 38, 18}},
            {{0, 0x1F, 50, 28}, {30, 0x0E, 36, 20}, {56, M_COL, 30, 6}},
        },
    },
    {   /* with the half-time drums: the bass has the root */
        {
            {{0, F, 56, 20}, {22, 0x0E, 40, 6}, {38, F, 46, 16}},
            {{0, F, 54, 14}, {14, 0x0C, 38, 4}, {32, F, 46, 18}, {54, 0x06, 34, 6}},
            {{0, F, 56, 20}, {22, 0x0E, 40, 6}, {38, F, 46, 14}, {56, M_COL, 32, 6}},
            {{0, F, 54, 18}, {30, 0x0C, 38, 6}, {44, 0x0E, 42, 14}},
        },
        {
            {{0, F, 54, 12}, {14, 0x0C, 36, 4}, {32, F, 46, 22}},
            {{0, F, 52, 20}, {22, 0x06, 38, 8}, {38, 0x0E, 44, 6}, {46, 0x0C, 34, 4}},
            {{0, F, 56, 14}, {30, 0x0E, 40, 8}, {44, F, 44, 14}},
            {{0, F, 52, 10}, {14, 0x06, 36, 4}, {22, 0x0E, 42, 8}, {38, F, 44, 18}},
        },
    },
    {   /* the two-step: a little more syncopated, ahead of the changes, and still not busy */
        {
            {{0, F, 56, 12}, {14, 0x0E, 40, 4}, {32, F, 48, 10}, {46, 0x0C, 38, 4}, {62, M_NEXT | F, 52, 12}},
            {{14, 0x0C, 38, 4}, {22, F, 46, 8}, {38, 0x06, 38, 8}, {54, 0x0E, 42, 4}},
            {{0, F, 56, 12}, {14, 0x0E, 40, 4}, {32, F, 48, 12}, {46, 0x06, 36, 4}, {62, M_NEXT | F, 52, 14}},
            {{14, 0x0C, 40, 4}, {22, F, 46, 8}, {46, 0x06, 36, 6}, {54, M_COL, 34, 6}},
        },
        {
            {{0, F, 54, 8}, {22, 0x0E, 42, 6}, {32, F, 48, 8}, {54, 0x0C, 38, 4}, {62, M_NEXT | F, 50, 12}},
            {{22, F, 46, 10}, {38, 0x0E, 40, 4}, {46, 0x06, 36, 6}},
            {{0, F, 54, 10}, {22, 0x0C, 40, 4}, {32, F, 46, 14}, {54, 0x0E, 38, 4}, {62, M_NEXT | F, 50, 14}},
            {{14, 0x0E, 40, 4}, {30, F, 44, 10}, {54, M_COL, 34, 6}},
        },
    },
};
#undef F
static uint8_t comp_set;                         /* moves only at the cycle's seam */

static void comp_play(const comp_t *g, int c)
{
    const int ch = g->mask & M_NEXT ? (c + 1) & 3 : c;
    const float base = (float)g->vel * 0.01f * (0.96f + 0.08f * frand());
    const int32_t hold = (int32_t)((float)g->len * step_len);
    int32_t wait = irand(120);
    int v, oct = 12 * par[P_BOCT];
    if (g->mask & M_LH) {
        int r = (par[P_ROOT] + CH_ROOT[ch]) % 12;
        ep_strike((r < 4 ? 48 : 36) + r + oct, base * 0.8f, hold, wait);
        wait += 200 + irand(120);
    }
    for (v = 0; v < 4; v++)
        if (g->mask >> v & 1u) {                 /* a slight roll up the hand: 4 .. 7 ms a note */
            ep_strike(keys_v[ch][v] + oct, base * (v == 3 ? 1.04f : 0.94f + 0.02f * (float)v), hold, wait);
            wait += 180 + irand(130);
        }
    if (g->mask & M_COL) {
        int n = keys_v[c][3], want = (par[P_ROOT] + CH_COLOUR[c]) % 12;
        n += ((want - n) % 12 + 12) % 12;
        ep_strike(n + oct, base, hold, wait);
    }
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

typedef struct {
    uint8_t step;
    int8_t note;                                 /* semitones above the tonic, down in octave 1 */
    uint8_t len;
} bnote_t;
#define BFIG_MAX 6
static const bnote_t BFIG[4][BFIG_MAX] = {       /* a slow line under each chord, at the half-time levels */
    {{0, 5, 26}, {32, 5, 10}, {44, 12, 12}},                 /* IV .. IV, the tonic an octave up */
    {{0, 4, 26}, {32, 4, 8}, {44, 11, 10}, {56, 7, 6}},      /* iii .. iii, its 5th and 3rd */
    {{0, 9, 26}, {32, 4, 10}, {44, 7, 12}},                  /* vi .. iii, V */
    {{0, 7, 26}, {32, 7, 8}, {44, 14, 8}, {54, 9, 8}},       /* V .. V, its 5th, vi */
};

static void bass_note(int off, uint32_t len)
{
    const int n = 24 + par[P_ROOT] + off + 12 * par[P_BASSOCT];
    if (bass.midi_note)
        midi(0x82, bass.midi_note, 0);
    bass.midi_note = (uint8_t)n;
    midi(0x92, (uint32_t)n, 90);
    bass.inc_t = mtof((float)n) * (1.0f / EN_SR);
    if (bass.amp < 0.002f)
        bass.inc = bass.inc_t;
    bass.sus = 1.0f;
    bass.spk = 0.12f;
    bass.hold = (int32_t)((float)len * step_len);
}

/* ------------------------------------------------------------- the motifs --- */
/* Two short phrases, a call and its answer a tone away in shape: up, up, a step back. The call a soft vibraphone, on
 * the IV leaning into the iii; the answer glass, on the vi leaning into the V, and it stops on the 9th of that chord,
 * a note that wants to fall and doesn't. Rests much longer than the phrases; the last call goes unanswered. */
#define NBELL 4
typedef struct {
    float cph, mph, cinc, minc, amp, adec, idx, idec, att, arate, gl, gr;
    int32_t wait;
    uint8_t on, note, kind;
} bell_t;
static bell_t bell[NBELL];
static float bell_vol, bell_trem_ph;
typedef struct {
    uint8_t step;                                /* in the cycle */
    int8_t note;                                 /* above the tonic, an octave over middle C */
    uint8_t vel;
} mnote_t;
static const mnote_t MOTIF[2][4] = {
    {{52, 0, 44}, {55, 4, 46}, {58, 9, 56}, {64, 7, 52}},            /* 1 3 6, 5 */
    {{180, 4, 40}, {183, 7, 44}, {186, 11, 50}, {194, 9, 54}},       /* 3 5 7, 6 */
};
/* each cycle: bit 0 the call, bit 1 the answer */
static const uint8_t MOTIF_WHEN[12] = {0, 2, 1, 2, 3, 0, 1, 3, 0, 3, 1, 0};

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
    if (kind == 0) {                             /* a vibraphone: a soft bar, a little 4th partial on the strike */
        v->minc = v->cinc * 4.0f;
        v->idx = (0.25f + 0.55f * vel) * (1.0f / FM_TWO_PI);
        v->idec = fm_expf(-1.0f / (0.11f * EN_SR));
        v->adec = fm_expf(-6.9f / (2.9f * EN_SR));
        v->arate = 1.0f / (0.003f * EN_SR);
        p = (-0.35f + 1.0f) * (FM_PI / 4.0f);
    } else {                                     /* glass: octaves a hair stretched, ringing longer, thinner */
        v->minc = v->cinc * 3.003f;
        v->idx = (0.35f + 0.7f * vel) * (1.0f / FM_TWO_PI);
        v->idec = fm_expf(-1.0f / (0.42f * EN_SR));
        v->adec = fm_expf(-6.9f / (3.6f * EN_SR));
        v->arate = 1.0f / (0.0015f * EN_SR);
        p = (0.42f + 1.0f) * (FM_PI / 4.0f);
    }
    v->gl = fm_cosf(p);
    v->gr = fm_sinf(p);
    v->wait = wait;
    en_loop_glow[kind] = 1.0f;
    midi(0x91, (uint32_t)n, (uint32_t)(vel * 110.0f));
}

/* ----------------------------------------------------------------- the timeline --- */
typedef struct {
    uint16_t step;
    uint8_t level;
    uint8_t bass;                                /* its level, % */
    uint8_t touch;                               /* how firmly the piano plays, %: it brightens with the music */
} part_t;
static const part_t PART[] = {
    {0, 0, 0, 92},                               /* 0:00 pad and piano */
    {448, 1, 0, 95},                             /* 0:40 the motifs have begun; hats and ghosts */
    {896, 2, 100, 100},                          /* 1:19 the half-time drums, the bass */
    {1536, 3, 100, 106},                         /* 2:15 the two-step: the fullest moment */
    {2176, 2, 100, 100},                         /* 3:12 */
    {2432, 1, 55, 95},                           /* 3:35 */
    {2624, 0, 0, 90},                            /* 3:51 pad and piano again; ends open at 4:10 */
    {SONG_STEPS, 0, 0, 0},
};
/* per level: the stems' levels, the low-pass on the break (Hz), its pull on the melodic bus */
static const float CORE_LVL[4] = {0.0f, 0.0f, 0.85f, 1.0f}, TOP_LVL[4] = {0.0f, 0.50f, 0.19f, 0.23f};
static const float BRK_FC[4] = {3000.0f, 3000.0f, 4300.0f, 7000.0f};
static const float PAD_FC[4] = {640.0f, 680.0f, 760.0f, 900.0f};

static uint32_t song;
static int song_level;
static float brk_fc, brk_fc_t, pad_fc, pad_fc_t, duck, duck_s;

static void level_set(const part_t *p, int first)
{
    const float ramp = first ? 1.0f : 8.0f * 16.0f * step_len;   /* eight bars of 170: 11 s */
    song_level = p->level;
    brk_levels(CORE_LVL[p->level], TOP_LVL[p->level], ramp);
    brk_fc_t = BRK_FC[p->level];
    pad_fc_t = PAD_FC[p->level];
    bass_song_t = (float)p->bass * 0.01f;
    song_touch = (float)p->touch * 0.01f;
}

static void song_step(uint32_t s)
{
    const uint32_t cs = s % CHORD_STEPS, cy = s % CYCLE_STEPS, cycle = s / CYCLE_STEPS;
    const int c = (int)(s / CHORD_STEPS & 3u), nc = (c + 1) & 3;
    int i, flags = 0;
    float k;

    if (s == 0u)
        comp_set = 0;
    if (!(s & 15u)) {                            /* on the bar: the timeline */
        for (i = 0; PART[i + 1].step <= s; i++)
            ;
        if (PART[i].level != song_level || s == 0u)
            level_set(&PART[i], s == 0u);
    }
    /* the kit holds its breath in the last bar before a higher level, with the air rising into it */
    for (i = 0; PART[i + 1].step <= s; i++)
        ;
    if (PART[i + 1].level > PART[i].level && PART[i + 1].level >= 2) {
        const uint32_t to = PART[i + 1].step - s;
        if (to <= 16u && PART[i].level >= 2)
            flags |= BRK_DROP;
        if (to == 12u)
            flags |= BRK_SWELL;
    }
    k = brk_pattern_step(s, song_level, flags);
    if (k > 0.0f)
        duck = fm_maxf(duck, k);

    /* the pad: leave a little before the change, arrive after it */
    if (cs == CHORD_STEPS - 4u)
        for (i = 0; i < EN_NPAD; i++) {
            pad[i].next = pad_v[nc][i];
            if (pad[i].next != pad[i].note)
                pad[i].tgt = 0.0f;
        }
    if (cs == 0u) {
        for (i = 0; i < EN_NPAD; i++) {
            pad[i].note = pad_v[c][i];
            pad[i].tgt = 1.0f;
        }
        publish_chord(c);
    }
    pad_gate = s < PAD_OFF ? 1.0f : 0.0f;

    /* the piano */
    if (!cy)
        comp_set = (uint8_t)(song_level <= 1 ? 0 : song_level - 1);
    if (s <= LAST_CHORD) {
        const comp_t *g = COMP[comp_set][cycle & 1u][c];
        for (i = 0; i < COMP_MAX && (g[i].mask); i++)
            if (g[i].step == cs && (s < LAST_CHORD - 64u + 62u || s == LAST_CHORD))
                comp_play(&g[i], c);
    }

    /* the bass */
    if (song_level >= 2) {
        for (i = 0; i < BFIG_MAX && BFIG[c][i].len; i++)
            if (BFIG[c][i].step == cs)
                bass_note(BFIG[c][i].note, BFIG[c][i].len);
    } else if (bass_song_t > 0.0f && !cs) {
        bass_note(CH_ROOT[c], 58u);   /* the way out: long held roots */
    }

    /* the motifs */
    if (cycle < 12u) {
        const uint32_t when = MOTIF_WHEN[cycle];
        int m, n;
        for (m = 0; m < 2; m++)
            if (when >> m & 1u)
                for (n = 0; n < 4; n++)
                    if (MOTIF[m][n].step == cy) {
                        const int note = 72 + par[P_ROOT] + MOTIF[m][n].note + 12 * par[P_BOCT];
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

static void harmony(void)                       /* the voicings, for the key; the chord for the screen */
{
    if (par[P_TEMPO] <= 0)
        return;                                  /* not yet (en_init applies in order) */
    voicings(CH_KEYS, keys_v, 57 + par[P_ROOT], 52 + par[P_ROOT], 71 + par[P_ROOT]);
    voicings(CH_PAD, pad_v, 49 + par[P_ROOT], 49 + par[P_ROOT], 67 + par[P_ROOT]);
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
    case P_ROOT: harmony(); break;               /* the cycle moves with the next chord */
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
    }
    for (i = 0; i < NKEYS; i++)
        ep[i].on = 0;
    for (i = 0; i < NBELL; i++)
        bell[i].on = 0;
    pad_gain = pad_gate = 0.0f;
    ep_gone[0] = ep_gone[1] = 0.0f;
    ep_lim = NKEYS;
    duck = duck_s = 0.0f;
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
    harmony();
    pool = EN_MODE_POOL[par[P_MODE]];
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
        case C_POOL: pool = ((uint32_t)b | 1u) & 0xFFFu; break;   /* kept for the screen: the demo's cycle has its own notes */
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
    const float g = pad_vol * 0.097f;
    for (j = 0; j < n; j++)
        padl[j] = padr[j] = 0.0f;
    pad_gain += (pad_gate - pad_gain) * fm_minf((float)n / ((pad_gate > 0.0f ? 2.5f : 2.2f) * EN_SR), 1.0f);
    for (i = 0; i < EN_NPAD; i++) {
        pad_t *v = &pad[i];
        float p0 = v->ph[0], p1 = v->ph[1], amp = v->amp, f, gl, gr, pn;
        const float tgt = v->tgt * pad_gain, rate = tgt > amp ? 1.0f / (1.1f * EN_SR) : 1.0f / (0.22f * EN_SR),
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
        f = mtof((float)v->note + 0.05f * motion * mod[i].v);
        v->inc[0] = f * 0.99856f * (1.0f / EN_SR);           /* -2.5 and +2.5 cents: slow, and warm */
        v->inc[1] = f * 1.00144f * (1.0f / EN_SR);
        en_pad_note[i] = v->note;
        en_pad_amp[i] = amp;
        lvl = fm_maxf(lvl, amp);
    }
    en_pad_level = lvl;
    c = 1.0f - fm_expf(-FM_TWO_PI * fm_clampf(pad_fc * (1.0f + 0.10f * motion * mod[4].v), 200.0f, 4000.0f) * (1.0f / EN_SR));
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
            gd = 1.0f - 0.30f * dk;
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
    const float g = ep_vol * 0.73f, cl = 0.40f;              /* the bus's one pole: ~3.6 kHz */
    float trem0, trem1, dt, tr, gd, dg;
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
        float cph = e->cph, tph = e->tph, amp = e->amp, ib = e->ib, it = e->it, att = e->att;
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
                e->dec = e->rel;
                dec = e->rel;
                midi(0x80, e->note, 0);
            }
        }
        if (it > 0.0006f) {                      /* the tine's ping, then a lookup saved */
            for (; k < n; k++) {
                const float m = sine(cph) * (ib + 0.024f) + sine(tph) * it;
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
                ib *= ibd;
                it *= itd;
                att += (1.0f - att) * ar;
            }
        } else {
            for (; k < n; k++) {
                const float m = sine(cph) * (ib + 0.024f);
                x = sine(cph + m + 2.0f) * amp * att * (1.0f + ib * 1.8f);
                padl[k] += x * gl;
                padr[k] += x * gr;
                cph += cinc;
                if (cph >= 1.0f)
                    cph -= 1.0f;
                amp *= dec;
                ib *= ibd;
                att += (1.0f - att) * ar;
            }
        }
        e->cph = cph;
        e->tph = tph;
        e->amp = amp;
        e->ib = fm_flush(ib);
        e->it = fm_flush(it);
        e->att = att;
        e->last = x * g;
        if (amp < 0.0005f)
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
        dd = 1.0f - 0.16f * gd;
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
    const float g = bass_vol * 0.98f * bass_song, tgt = bass.hold > 0 ? bass.sus : 0.0f,
                rate = tgt > bass.amp ? 1.0f / (0.012f * EN_SR) : 1.0f / (0.06f * EN_SR);
    const float dd = (duck1 - duck0) * (1.0f / (float)n);
    float dk = duck0;
    float ph = bass.ph, amp = bass.amp, idx = bass.idx, inc = bass.inc, spk = bass.spk;
    if (amp < 1e-5f && bass.hold <= 0) {
        bass.amp = 0.0f;
        en_bass_level = 0.0f;
        return;
    }
    for (j = 0; j < n; j++) {
        const float m = sine(ph) * idx, x = sine(ph + m + 1.0f) * amp * g * (1.0f - 0.25f * dk);
        dk += dd;
        ph += inc;
        if (ph >= 1.0f)
            ph -= 1.0f;
        inc += (bass.inc_t - inc) * 0.004f;
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
    bass.sus *= fm_expf(-(float)n / (6.0f * EN_SR));
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
    const float g = bell_vol * 0.55f;
    float tr0, tr1;
    tr0 = sine(bell_trem_ph);
    bell_trem_ph += (float)n * (4.4f / EN_SR);
    if (bell_trem_ph >= 1.0f)
        bell_trem_ph -= 1.0f;
    tr1 = sine(bell_trem_ph);
    for (i = 0; i < NBELL; i++) {
        bell_t *v = &bell[i];
        float cph = v->cph, mph = v->mph, amp = v->amp, idx = v->idx, att = v->att;
        const float depth = v->kind == 0 ? 0.13f : 0.0f;     /* the vibraphone's slow fan */
        float tr, dt;
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
        tr = 1.0f + depth * tr0;
        dt = depth * (tr1 - tr0) * (1.0f / (float)n);
        for (; j < n; j++) {
            float m = sine(mph) * idx, x;
            x = sine(cph + m + 4.0f) * amp * att * g * tr;
            mixl[j] += x * v->gl * 0.55f;
            mixr[j] += x * v->gr * 0.55f;
            rsend[j] += x * 1.10f;
            esend[j] += x * 0.95f;
            cph += v->cinc;
            mph += v->minc;
            if (cph >= 1.0f)
                cph -= 1.0f;
            if (mph >= 1.0f)
                mph -= 1.0f;
            amp *= v->adec;
            idx *= v->idec;
            att += (1.0f - att) * v->arate;
            tr += dt;
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
                ct = 1.0f - fm_expf(-FM_TWO_PI * (brk_fc * 1.25f) * (1.0f / EN_SR));
    const float v = break_vol * 1.87f;
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
        x -= 0.40f * s;                          /* the highs of it further back than the thump */
        mixl[j] += x + t * 0.50f;
        mixr[j] += x + t * 0.66f;
        rsend[j] += s * 1.30f + t * 2.20f;
        esend[j] += s * 0.05f + t * 0.30f;
        a = x < 0.0f ? -x : x;
        pk = a > pk ? a : pk;
    }
    brk_lp[0] = fm_flush(brk_lp[0]);
    brk_lp[1] = fm_flush(brk_lp[1]);
    brk_hp = fm_flush(brk_hp);
    en_break_level += (pk - en_break_level) * 0.2f;
    en_hits = brk_hits;
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
    duck *= fm_expf(-(float)n * (1.0f / (0.17f * EN_SR)));
    duck1 = duck_s;
    render_pad(n);
    render_keys(n);
    render_bass(n);
    render_bells(n);
    render_break(n);
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
    brk_fc += (brk_fc_t - brk_fc) * slow;
    pad_fc += (pad_fc_t - pad_fc) * slow;
    bass_song += (bass_song_t - bass_song) * slow;
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
