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
    {"Tempo", 150, 180, 170}, {"Energy", 0, 100, 70},  {"Evolve", 0, 100, 50},  {"Style", 0, EN_NSTYLES - 1, EN_ST_LIQUID},
    {"Keys", 0, 100, 60},     {"Bloom", 0, 100, 50},   {"Bright", 0, 100, 50},  {"Notes", 0, 3, 3},
    {"Motifs", 0, 100, 60},   {"Rate", 0, 100, 50},    {"Tone", 0, 100, 50},    {"Motif oct", -2, 2, 0},
    {"Bass", 0, 100, 50},     {"Move", 0, 100, 50},    {"Glide", 0, 100, 10},   {"Bass oct", -1, 1, 0},
    {"Drums", 0, 100, 45},    {"Breaks", 0, 100, 50},  {"Hats", 0, 100, 50},    {"Kit", 0, 1, 0},
    {"Echo", 0, 100, 40},     {"Echo time", 0, 5, 3},  {"Reverb", 0, 100, 65},  {"Size", 0, 100, 80},
    {"Root", 0, 11, 2},       {"Tune", -50, 50, 0},    {"MIDI out", 0, 1, 1},
    {"Mode", 0, EN_NMODES - 1, 3},
};
const en_param_t *en_param_info(int i) { return (i >= 0 && i < P_NPARAMS) ? &PARAMS[i] : &PARAMS[0]; }

/* the pieces: eight recipes for the whole instrument (the FM-1's PRESETS). Tune and MIDI out are taken from here by neither:
 * they belong to the player, not the piece */
/*                     Tem  En Evo St  Key Blo Bri No  Mot Rat Ton Oc  Bas Mov Gli BO  Drm Brk Hat Ki  Ech ET Rev Siz  Rt Tun MI  Md */
static const struct {
    const char *name;
    int16_t v[P_NPARAMS];
} PIECES[EN_NPIECES] = {
    {"Aqua",      {170, 70, 50, 0,  60, 50, 50, 3,  60, 50, 50, 0,  50, 50, 10, 0,  45, 50, 50, 0,  40, 3, 65, 80,  2, 0, 1,  3}},
    {"Skyline",   {172, 70, 40, 0,  50, 50, 50, 3,  65, 50, 50, 0,  50, 50, 10, 0,  50, 50, 50, 0,  40, 3, 60, 75,  7, 0, 1,  0}},
    {"Bubble",    {174, 70, 45, 0,  50, 50, 50, 3,  80, 50, 50, 1,  45, 50, 10, 0,  45, 50, 50, 0,  45, 3, 60, 75,  9, 0, 1,  6}},
    {"Rainfall",  {165, 70, 30, 0,  70, 50, 50, 3,  45, 50, 50, 0,  45, 50, 15, 0,  35, 50, 50, 0,  45, 4, 70, 85,  5, 0, 1,  0}},
    {"Aurora",    {168, 70, 35, 0,  80, 50, 50, 3,  50, 50, 50, 0,  45, 50, 15, 0,  40, 50, 50, 1,  50, 4, 80, 95,  4, 0, 1,  3}},
    {"Lagoon",    {170, 70, 40, 0,  60, 50, 50, 3,  55, 50, 50, 0,  60, 50, 20, 0,  45, 50, 50, 1,  40, 3, 65, 80,  0, 0, 1,  4}},
    {"Dawn",      {160, 70, 25, 0,  80, 50, 50, 3,  40, 50, 50, 0,  35, 50, 20, 0,  25, 50, 50, 0,  45, 4, 80, 90, 10, 0, 1,  0}},
    {"Afterglow", {165, 70, 40, 0,  65, 50, 50, 3,  55, 50, 50, 0,  50, 50, 15, 0,  40, 50, 50, 1,  45, 4, 70, 85,  5, 0, 1,  1}},
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

void en_param_text(int i, int v, char *b)
{
    static const char *const ONOFF[2] = {"Off", "On"};
    const char *s = 0;
    if (i == P_ETIME)
        s = ETIME_NAME[(unsigned)v < 6u ? v : 0];
    else if (i == P_MIDI)
        s = ONOFF[v ? 1 : 0];
    else if (i == P_KIT)
        s = v ? "Moss" : "Glass";
    else if (i == P_STYLE)
        s = EN_STYLE_NAME[(unsigned)v < EN_NSTYLES ? v : 0];
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
    if ((i == P_TUNE || i == P_MOCT || i == P_BASSOCT) && v > 0)
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

/* a knob's effect: base at the knob's default (exactly, so the default is the sound that was approved), lo and hi at its
 * ends, and a square law between, so it is fine near the default and wide at the ends */
static float knob_curve(int v, int mid, float lo, float base, float hi)
{
    float d;
    if (v == mid)
        return base;
    d = v < mid ? (float)(mid - v) / (float)mid : (float)(v - mid) / (float)(100 - mid);
    return base + d * d * ((v < mid ? lo : hi) - base);
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
void en_seed(uint32_t s)
{
    s ^= s >> 24;                                /* what the command has no room for */
    post(C_SEED, (int)(s >> 16 & 0xFFu), (int)(s & 0xFFFFu));
}
void en_panic(void) { post(C_PANIC, 0, 0); }
void en_clock(int msg) { post(C_CLOCK, msg, 0); }
void en_regrow(void) { post(C_REGROW, 0, 0); }

/* -------------------------------------------------------------- state --- */
volatile uint8_t en_playing, en_frozen, en_ext;
volatile uint8_t en_chord[EN_NPAD], en_chord_root, en_chord_n, en_chord_bars;
volatile float en_drift, en_pad_level, en_bass_level;
volatile uint8_t en_loop_pos[EN_NLOOP];
volatile int8_t en_loop_note[EN_NLOOP][EN_LOOP_MAX];
volatile float en_loop_glow[EN_NLOOP];
volatile uint32_t en_bars;
volatile uint32_t en_hits;
volatile float en_break_level;
volatile uint8_t en_pad_note[EN_NPAD];
volatile float en_pad_amp[EN_NPAD];

static int16_t par[P_NPARAMS];
static uint32_t pool = 0xAB5u;                  /* Ionian until told: what the harmony is made of, from the next chord */
static float tunefac = 1.0f;
#define RNG_START 0x9E3779B9u
static uint32_t rng = RNG_START;
static uint32_t song_seed = EN_HARM_SEED;       /* every musical choice: the arranger, the harmony, the bass, the drums */

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
/* how far the slow motion goes: the pad's pan and pitch, the echo's heads. No knob: what the approved sound had */
static const float motion = 60.0f * 0.0058f;

/* ----------------------------------------------------------------- break --- */
static float break_vol;
static float step_len, pulse_left;               /* a sixteenth, in samples; to the next MIDI clock pulse */
static float brk_lp[2], brk_hp, plp[4], eplp[2];

/* ------------------------------------------------------------------ space --- */
static float echo_send, rev_send, echo_len, echo_len_t, echo_fb = 0.55f;
static float rev_in_gain = 1.0f;                /* falls to 0 while frozen */

/* ------------------------------------------------------------- the arranger --- */
/* Endless passes: intro, hats, light, breakdown, full, way out, then the intro again with the chords where they have got to. A
 * section is so many phrases of eight bars, 11.3 s at 170; a pass 18 to 31 of them, 3:23 to 5:50 (at 150 BPM 3:50 to 6:37,
 * at 180 3:12 to 5:30: all of it counts in bars, the tempo only stretches it). Sections change on bars and know nothing of
 * how the harmony is made: it is told a character (en_part_t) and takes it up in its own time.
 * A section has two phases, as the demo's song did: the light starts without the bass, the breakdown is hats and then half
 * time, the full's last two phrases ask the harmony to calm, the way out ends with no drums. A style is a row of
 * numbers: how long each section may be, what drum level each phase has, how often a pass reaches the full section (the
 * others go from the breakdown straight out), which bass figures, and its rhythm for brk_gen_bar. Half's two-step levels
 * are half time, by its density. Drift's peak is half time, so the harmony still rises with no full rhythm. */
const char *const EN_SECTION_NAME[EN_NSECTIONS] = {"Intro", "Hats", "Light", "Breakdown", "Full", "Out"};
const char *const EN_STYLE_NAME[EN_NSTYLES] = {"Liquid", "Roller", "Jungle", "Half", "Drift", "Loading"};
typedef struct {
    uint8_t lo[EN_NSECTIONS], hi[EN_NSECTIONS];  /* phrases */
    uint8_t lv[EN_NSECTIONS][2];                 /* the drums' level, by phase */
    uint8_t full_pct;                            /* a pass's chance of the full section */
    uint8_t bass[2];                             /* the bass's figures at levels 2, 3 */
    brk_rhythm_t rhythm;
} style_t;
#define LV_ALL {0, 0}, {1, 1}, {2, 2}, {1, 4}, {3, 3}, {1, 0}
static const style_t STYLE[EN_NSTYLES] = {
    /* the phrases of:  intro hats light down full out       levels          full%  bass                        dens syn rate len ghost */
    /* Liquid */  {{2, 1, 6, 2, 5, 2},  {3, 2, 11, 4, 8, 3},  {LV_ALL},                                    100, {EN_BASS_LIGHT, EN_BASS_FULL}, {50, 20, 80, 50, 30}},
    /* Roller */  {{1, 1, 5, 2, 7, 2},  {2, 2, 9, 3, 12, 3},  {LV_ALL},                                    100, {EN_BASS_FULL, EN_BASS_ROLL},  {80, 30, 110, 50, 80}},
    /* Jungle */  {{1, 1, 5, 2, 7, 2},  {2, 2, 9, 4, 11, 3},  {LV_ALL},                                    100, {EN_BASS_LIGHT, EN_BASS_FULL}, {60, 75, 176, 70, 50}},
    /* Half */    {{2, 1, 6, 2, 5, 2},  {3, 2, 11, 4, 8, 3},  {LV_ALL},                                    100, {EN_BASS_LONG, EN_BASS_LIGHT}, {20, 20, 72, 20, 30}},
    /* Drift */   {{3, 3, 4, 6, 3, 2},  {4, 4, 6, 9, 4, 3},   {{0, 0}, {1, 1}, {1, 1}, {0, 1}, {4, 4}, {1, 0}}, 100, {EN_BASS_LONG, EN_BASS_LONG}, {10, 10, 32, 10, 0}},
    /* Loading */ {{2, 3, 8, 3, 5, 2},  {3, 4, 11, 4, 6, 3},  {{0, 0}, {1, 1}, {1, 2}, {1, 1}, {3, 3}, {1, 0}}, 15, {EN_BASS_LONG, EN_BASS_LIGHT}, {38, 20, 40, 20, 10}},
};
/* the two phases of each section, in every style: what the demo's song did at each */
static const struct {
    uint8_t character, ramp, bass, touch;
} PHASE[EN_NSECTIONS][2] = {
    {{0, 1, 0, 92}, {0, 1, 0, 92}},
    {{1, 4, 0, 95}, {1, 4, 0, 95}},
    {{1, 2, 0, 97}, {2, 2, 100, 100}},
    {{1, 2, 75, 96}, {1, 2, 75, 96}},
    {{3, 1, 112, 114}, {2, 1, 112, 108}},
    {{1, 8, 55, 95}, {0, 4, 0, 90}},
};
/* Energy: under 10 hats at most, under 35 half time at most, under 60 the light, then all. Above 70 the breakdown shortens,
 * by half at 100 */
#define ENERGY_FULL 60
#define ENERGY_LONG 70

const brk_rhythm_t *en_style_rhythm(int style) { return &STYLE[(unsigned)style < EN_NSTYLES ? style : 0].rhythm; }

static int arr_cap(int level, int energy)        /* a drum level under the energy's ceiling */
{
    static const uint8_t RANK[5] = {0, 1, 3, 4, 2}, LEVEL[5] = {0, 1, 4, 2, 3};
    const int top = energy < 10 ? 1 : energy < 35 ? 2 : energy < ENERGY_FULL ? 3 : 4;
    return RANK[level] > top ? LEVEL[top] : level;
}

void en_arr_range(int style, int section, int energy, int *lo_bars, int *hi_bars)
{
    const style_t *st = &STYLE[(unsigned)style < EN_NSTYLES ? style : 0];
    int lo = st->lo[section], hi = st->hi[section];
    if (section == EN_SEC_DOWN && energy > ENERGY_LONG) {
        const int f = 130 - energy;              /* 60 at 70, 30 at 100 */
        lo = (lo * f + 59) / 60;
        hi = (hi * f + 59) / 60;
    }
    *lo_bars = lo * (int)EN_ARR_PHRASE;
    *hi_bars = hi * (int)EN_ARR_PHRASE;
}

static void arr_enter(en_arr_t *a, int section, uint32_t seed, int style, int energy)
{
    int lo, hi;
    const uint32_t h = brk_hash(seed, 21u + (uint32_t)section, a->pass);
    en_arr_range(style, section, energy, &lo, &hi);
    a->section = (uint8_t)section;
    a->bars = (uint16_t)(lo + (int)((h >> 8) % ((uint32_t)(hi - lo) / EN_ARR_PHRASE + 1u) * EN_ARR_PHRASE));
}

void en_arr_first(en_arr_t *a, uint32_t seed, int style, int energy)
{
    a->pass = 0u;
    arr_enter(a, EN_SEC_INTRO, seed, style, energy);
}

void en_arr_next(en_arr_t *a, uint32_t seed, int style, int energy)
{
    int next = a->section + 1;
    style = (unsigned)style < EN_NSTYLES ? style : 0;
    if (a->section == EN_SEC_DOWN && (brk_hash(seed, 20u, a->pass) >> 8) % 100u >= STYLE[style].full_pct)
        next = EN_SEC_OUT;
    if (next >= EN_NSECTIONS) {
        next = EN_SEC_INTRO;
        a->pass++;
    }
    arr_enter(a, next, seed, style, energy);
}

void en_arr_part(const en_arr_t *a, uint32_t bar_in, int style, int energy, en_part_t *o)
{
    const style_t *st = &STYLE[(unsigned)style < EN_NSTYLES ? style : 0];
    const uint32_t phrases = a->bars / EN_ARR_PHRASE;
    uint32_t first = phrases;                    /* the first phase's phrases */
    int ph, lv;
    switch (a->section) {
    case EN_SEC_LIGHT: first = 1u; break;
    case EN_SEC_DOWN: first = (phrases + 1u) / 2u; break;
    case EN_SEC_FULL: first = phrases > 3u ? phrases - 2u : phrases == 3u ? 2u : phrases; break;
    case EN_SEC_OUT: first = phrases > 1u ? phrases - 1u : 1u; break;
    default: break;
    }
    ph = bar_in >= first * EN_ARR_PHRASE;
    lv = st->lv[a->section][ph];
    o->section = a->section;
    o->level = (uint8_t)arr_cap(lv, energy);
    o->character = PHASE[a->section][ph].character;
    o->ramp = PHASE[a->section][ph].ramp;
    o->bass = PHASE[a->section][ph].bass;
    o->touch = PHASE[a->section][ph].touch;
    if (o->level != lv) {                        /* held under its level: no firmer than that level is */
        const uint8_t bass = o->level == 2u ? 100u : 75u, touch = o->level == 2u ? 100u : 96u;
        o->bass = o->bass < bass ? o->bass : bass;
        o->touch = o->touch < touch ? o->touch : touch;
    }
    if (energy < ENERGY_FULL && o->character == 3u)
        o->character = 2u;
}

int en_arr_break(uint32_t bar, int level, int style, int breaks, uint32_t seed, uint32_t *in)
{
    const brk_rhythm_t *r = en_style_rhythm(style);
    const uint32_t h = brk_hash(seed, 5u, bar / EN_ARR_PHRASE), at = bar % EN_ARR_PHRASE;
    const uint32_t first = (100u - r->break_len) * 8u / 100u, then = first + 3u;   /* of eight: one bar, two, four */
    const uint32_t n = (h >> 8) % 8u, len = n < first ? 1u : n < then ? 2u : 4u;
    uint32_t chance;
    if ((level != 2 && level != 3) || breaks <= 0)
        return 0;
    chance = (uint32_t)r->break_rate * (level == 3 ? 5u : 3u) * (uint32_t)breaks / 250u;
    if (((h >> 16 & 7u) << 5 | (h >> 19 & 31u)) >= chance || at < EN_ARR_PHRASE - len)
        return 0;
    *in = at - (EN_ARR_PHRASE - len);
    return (int)len;
}

volatile uint8_t en_section, en_style, en_drum_level, en_break_bars;

/* ---------------------------------------------------------------- the song --- */
/* Waiting for something to load: warm, calm, hopeful, a thing about to begin that never quite arrives. The tonic is never the
 * root of anything in the bass, and the chords lean forward and do not land. Root, mode and keys change the chords at the next
 * chord, single notes and motifs at once.
 *
 * What a chord can be in a pool: an interval over its root is asked for as a function (1 3 5 7 9 11 13), found in the pool or
 * replaced along a chain. No dominant sevenths (a major third and a flat seventh pull somewhere, and this music does not
 * arrive), so that seventh is left out for the ninth; no minor sixth over a minor chord's third either, for the same
 * tritone. A minor ninth above a chord's root, a flat fifth and a flat thirteenth are never asked for. */
enum { FAM_MAJOR = EN_FAM_MAJOR, FAM_MINOR = EN_FAM_MINOR, FAM_SPARSE = EN_FAM_SPARSE };
/* a chord from outside the pool, now and then: the first of these with the fewest notes outside (one or two).
 * major: bVIImaj9 (Mixolydian's); IVmaj9 for Lydian, its lost natural fourth; bIIImaj9 for Mixolydian.
 * minor: IVadd9 with the Dorian's major third; bVImaj9 for Dorian, its lost flat sixth; then bIImaj9 */
static const int8_t BORROW[2][3][2] = {   /* root, seventh above it (-1: the ninth and the root instead) */
    {{10, 11}, {5, 11}, {3, 11}},
    {{5, -1}, {8, 11}, {1, 11}},
};

/* the voices, as functions of the chord's root */
enum { F1, F3, F5, F7, F9, F11, F13 };
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

#define POOL_IONIAN 0xAB5u
static uint32_t hhash(uint32_t seed, uint32_t cycle, uint32_t what)
{
    uint32_t x = (seed ^ 0x51ED270Bu) + cycle * 0x9E3779B1u + what * 0x85EBCA77u;
    x ^= x >> 15;
    x *= 0xC2B2AE3Du;
    x ^= x >> 13;
    x *= 0x27D4EB2Fu;
    return x ^ x >> 16;
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
#define SEC_UP 4                                 /* the hand at the peak: higher */
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

/* ---------------------------------------------------------- the chords, one at a time --- */
/* No cycle: the next chord is composed when it is nearly due, from any degree of the pool, and that is all the harmony there
 * is. en_gen_next is pure: state, pool, key, seed, Evolve and the arranger's character in; the state and one chord with its
 * voicing and its length out.
 *
 * Which degree. A weight for every root that can carry a chord in this pool: the family's table of function to function
 * (FN_W), times the bass's motion (MOTION: a fourth or a fifth or a step above all, a tritone hardly), times one more than the
 * tones it shares with the chord before, times what the character wants of that function (CHAR_FN) and what the place in the
 * phrase does (FN_POS: phrases lean, ending on a dominant and starting on a subdominant or a relative). Nothing is ever at zero
 * but the chord just played, a chord on the same bass, and half-diminished roots, which do not count as usable while two
 * others stand.
 *
 * The tonic stays out of the bass. A chord on the tonic exists only over its third, or its fifth where that fits better or
 * there is no third, stays at least TONIC_GAP chords apart even when its phrase comes again, is never near the start, and
 * weighs little: a glimpse of home, the waiting goes on.
 *
 * The key stays clear without any walk home: a chord of the tonic area (the tonic chord, and the relatives: FN_T, FN_M) gets
 * heavier with every chord since the last one (away), more so in calm or falling sections, and a colour that has the tonic
 * in it counts double once it has been away a little.
 *
 * Which colour. For the chosen root every shape the pool allows, shapes that come out the same counted once, is weighed: the
 * shape's own (GSHAPE: calm or bright by character), then how little the hand moves from the voicing before and how many of
 * its notes stay. Fuller sections like the ninths and the 6/9s; the hand also sits SEC_UP higher at the peak.
 *
 * How long. A phrase's rhythm comes first (TMPL, by character): a whole phrase is 8 or 16 bars of 2 to 4 chords, so every 8th
 * bar is a change, a chord is 4 bars or 2, and 1-bar chords only come in pairs late in a half. Calm is slow, the peak quicker.
 *
 * What it remembers. The last phrase. Evolve 0: it plays again for ever, note for note. In between it plays again a few times
 * (at 50 once or twice), most of those with one chord recoloured or changed, then a new one. Evolve 100: never again, and no
 * three chords in a row that have come within the last EN_GEN_WINDOW. A rise to the peak does not wait: new phrase, Evolve 0
 * apart. A chord from outside the pool comes rarely in a new phrase (BORROW_GAP chords apart at least), and is never kept.
 *
 * A new pool empties the memory: the rest of the phrase is composed in it, on the same rhythm. A new key moves the same
 * phrase there: the degrees are relative to it. */
typedef struct {
    uint8_t bass, col, sus, calm, bright;        /* calm, bright: how much it is liked, by character */
    uint8_t keys[4], pad[4];
} gshape_t;
static const gshape_t GSHAPE[SH_BORROW] = {
    /* bass col sus calm bright  keys                pad */
    {F1, F11, 0, 6, 3, {F1, F3, F5, F7},   {F1, F3, F5, F7}},    /* maj7, m7 (a maj7's pad leaves the seventh to the hand) */
    {F1, F11, 0, 4, 7, {F3, F5, F7, F9},   {F1, F3, F5, F7}},    /* maj9, m9 */
    {F1, F13, 0, 4, 3, {F1, F9, F3, F5},   {F1, F3, F5, F9}},    /* add9 */
    {F1, F1,  0, 1, 3, {F3, F13, F9, F5},  {F1, F3, F5, F13}},   /* 6/9 */
    {F1, F1,  0, 2, 3, {F3, F11, F5, F7},  {F1, F3, F5, F7}},    /* m11 */
    {F1, F9,  1, 1, 1, {F1, F11, F5, F7},  {F1, F11, F5, F7}},   /* 7sus4 */
    {F1, F1,  1, 1, 2, {F5, F7, F9, F11},  {F1, F11, F5, F7}},   /* 9sus */
    {F1, F1,  1, 1, 2, {F7, F9, F11, F13}, {F1, F11, F5, F7}},   /* 13sus */
    {F3, F9,  0, 1, 1, {F5, F7, F1, F3},   {F3, F5, F1, F7}},    /* a seventh chord over its third */
    {F3, F7,  0, 1, 1, {F1, F3, F5, F9},   {F3, F5, F1, F9}},    /* add9 over its third */
    {F5, F9,  0, 1, 1, {F3, F7, F1, F5},   {F5, F3, F1, F7}},    /* a seventh chord over its fifth: the tonic's only */
    /* a pool without thirds to speak of: open chords */
    {F1, F11, 1, 4, 3, {F1, F9, F5, F1},   {F1, F5, F9, F1}},
    {F1, F9,  1, 3, 3, {F1, F11, F5, F1},  {F1, F5, F11, F1}},
    {F1, F11, 1, 2, 3, {F1, F9, F5, F7},   {F1, F5, F9, F1}},
    {F1, F9,  1, 2, 3, {F1, F11, F5, F7},  {F1, F5, F11, F1}},
    {F1, F11, 1, 1, 3, {F1, F9, F5, F13},  {F1, F5, F9, F1}},
    {F5, F11, 1, 1, 1, {F1, F9, F5, F1},   {F1, F5, F9, F1}},    /* the tonic's, over its fifth */
    {F5, F9,  1, 1, 1, {F1, F11, F5, F1},  {F1, F5, F11, F1}},
};
static const uint8_t LIST_MAJOR[] = {SH_7, SH_9, SH_ADD9, SH_69, SH_7SUS, SH_9SUS, SH_13SUS, SH_7_3, SH_ADD9_3};
static const uint8_t LIST_MINOR[] = {SH_7, SH_9, SH_11, SH_ADD9, SH_7SUS, SH_9SUS, SH_13SUS, SH_7_3};
static const uint8_t LIST_OPEN[] = {SH_S2, SH_S4, SH_S2_7, SH_S4_7, SH_S2_6};
static const uint8_t LIST_TONIC[] = {SH_7_3, SH_7_5};
static const uint8_t LIST_TONIC_OPEN[] = {SH_S2_5, SH_S4_5};
#define COUNTOF(a) ((int)(sizeof(a) / sizeof((a)[0])))

/* the functions: a root's by its distance from the tonic. T the tonic, M its relatives, S the subdominant side, D the dominant
 * side, C whatever else a pool may have (weak) */
enum { FN_T, FN_M, FN_S, FN_D, FN_C, FN_N };
static const uint8_t FN_OF[3][12] = {
    /*           1     2     b3    3     4     #4    5     b6    6     b7    7 */
    {FN_T, FN_C, FN_S, FN_C, FN_M, FN_S, FN_C, FN_D, FN_C, FN_M, FN_D, FN_C},
    {FN_T, FN_C, FN_S, FN_M, FN_C, FN_S, FN_C, FN_D, FN_M, FN_C, FN_D, FN_C},
    {FN_T, FN_C, FN_S, FN_M, FN_M, FN_S, FN_C, FN_D, FN_M, FN_M, FN_D, FN_C},
};
/* from a function (row) to a function (column). Major: the relatives go to the subdominant, the subdominant to the dominant,
 * and the dominant mostly does not land: to a relative, or back to the subdominant. Minor: the same, the falling fifths of
 * III VI iv VII stronger. Sparse: all but level, a pentatonic has little to pull with */
static const uint8_t FN_W[3][FN_N][FN_N] = {
    {   /*  T  M  S  D  C */
        {0, 5, 8, 3, 1},
        {1, 4, 8, 3, 1},
        {2, 3, 3, 8, 1},
        {3, 8, 5, 1, 1},
        {1, 4, 4, 4, 1},
    },
    {
        {0, 5, 8, 3, 1},
        {1, 5, 8, 4, 1},
        {2, 4, 2, 8, 1},
        {3, 8, 4, 1, 1},
        {1, 4, 4, 4, 1},
    },
    {
        {0, 4, 5, 4, 1},
        {1, 4, 5, 4, 1},
        {1, 4, 3, 5, 1},
        {2, 5, 4, 2, 1},
        {1, 4, 4, 4, 1},
    },
};
/* the bass's move, in semitones up. A fourth up, a fifth down: the strongest; then the fifth up and the steps */
static const uint8_t MOTION[12] = {1, 3, 6, 3, 3, 8, 1, 6, 3, 3, 6, 3};
static const uint8_t CHAR_FN[4][FN_N] = {        /* the character's taste: quarters */
    {5, 5, 4, 3, 2},
    {4, 5, 4, 4, 2},
    {3, 4, 4, 4, 3},
    {2, 4, 5, 5, 3},
};
static const uint8_t FN_POS[3][FN_N] = {         /* mid-phrase, first, last: halves */
    {2, 2, 2, 2, 2},
    {1, 3, 3, 1, 2},
    {1, 2, 2, 4, 1},
};
#define TONIC_GAP 6u                             /* chords between chords on the tonic, at least */
#define BORROW_GAP 40u                           /* and between borrowed ones */
/* a phrase's rhythm, in bars. 1-bar chords late, in pairs, so what follows still starts on an even bar */
static const struct {
    uint8_t n, bars[4];
} TMPL[6] = {{4, {4, 4, 4, 4}}, {2, {4, 4}}, {3, {4, 2, 2}}, {3, {2, 2, 4}}, {4, {2, 2, 2, 2}}, {4, {4, 2, 1, 1}}};
static const uint8_t TMPL_W[4][6] = {            /* by character: slower when it is calm */
    {9, 2, 3, 2, 0, 0},
    {7, 2, 4, 3, 1, 0},
    {3, 1, 4, 4, 3, 1},
    {2, 1, 3, 3, 5, 2},
};

static int pop12(uint32_t m)
{
    int n = 0;
    for (m &= 0xFFFu; m; m &= m - 1u)
        n++;
    return n;
}

/* a shape on a root: the old voices-by-function resolver. 0: no such chord (nothing to borrow here) */
int en_chord_make(uint32_t p, int r, int sh, en_hchord_t *o)
{
    const int fam = en_pool_family(p);
    const gshape_t *g;
    int third = -1, v;
    p = (p | 1u) & 0xFFFu;
    o->family = (uint8_t)fam;
    o->borrowed = 0u;
    if (sh == SH_BORROW)
        return chord_borrow(p, fam, o);
    g = &GSHAPE[(unsigned)sh < SH_BORROW ? sh : 0];
    if (!g->sus) {
        const int m = IN_POOL(p, r, 4), n = IN_POOL(p, r, 3);
        third = m && n ? (fam == FAM_MINOR ? 3 : 4) : m ? 4 : n ? 3 : -1;
    }
    o->root = (uint8_t)r;
    fn_voices(p, r, third, g->keys, o->keys);
    fn_voices(p, r, third, g->pad, o->pad);
    if (sh == SH_7 && third == 4)
        o->pad[3] = (int8_t)r;
    v = fn_iv(p, r, third, g->col);
    o->col = (uint8_t)((r + (v >= 0 ? v : fn_instead(p, r, third, g->col, 0u))) % 12);
    v = fn_iv(p, r, third, g->bass);
    o->bass = (uint8_t)((r + (v >= 0 ? v : fn_instead(p, r, third, g->bass, 1u))) % 12);
    chord_tones_of(o);
    return 1;
}

/* may it stand? Always: nothing a minor ninth over its root or its bass, no flat fifth or flat thirteenth over the root, the
 * tonic not in the bass. If strict, as well: no dominant seventh, and three tones at the least. A sus chord has its fourth */
static int chord_fit(const en_hchord_t *c, int sh, int strict)
{
    const uint32_t t = c->tones;
#define OVER(iv) (t >> ((c->root + (iv)) % 12) & 1u)
    if (!c->bass || t >> ((c->bass + 1) % 12) & 1u)
        return 0;
    if (OVER(1) || OVER(6) || OVER(8))
        return 0;
    if (strict) {
        if ((OVER(4) && OVER(10)) || pop12(t) < 3)
            return 0;
        if (sh != SH_BORROW && sh >= SH_7SUS && sh <= SH_13SUS && !OVER(5))
            return 0;
    }
#undef OVER
    return 1;
}

/* the roots a chord may stand on in this pool: those with a real chord; if fewer than two have one, those with less, never
 * one with nothing over it. The tonic is not among them unless nothing else is */
uint32_t en_gen_roots(uint32_t p)
{
    const int fam = en_pool_family(p);
    uint32_t m = 0u;
    int tier, r;
    p = (p | 1u) & 0xFFFu;
    for (tier = 0; tier < 3 && pop12(m) < 2; tier++) {
        m = 0u;
        for (r = 1; r < 12; r++)
            if (p >> r & 1u && (tier == 0 ? root_stands(p, r, fam) : root_tones(p, r) >= 3 - (tier - 1)))
                m |= 1u << r;
    }
    return m ? m : 1u;
}

void en_gen_start(en_gen_t *st)
{
    int i;
    uint8_t *b = (uint8_t *)st;
    for (i = 0; i < (int)sizeof *st; i++)
        b[i] = 0u;
    st->prev_root = st->prev2 = st->prev3 = 0xFFu;
    st->since_t = 0u;                            /* it does not open on it, nor near it */
    st->since_b = BORROW_GAP / 2u;
}

typedef struct {
    uint32_t p, seed;
    int fam, key, evolve, character, up, pos;    /* pos: 0 mid-phrase, 1 first, 2 last */
    int avoid_root, avoid_bass;                  /* the chord after this one, when it is already known */
    int from_k[4], from_p[4];
    uint32_t usable;
    int window;                                  /* how far back three chords may not come again */
} gen_ctx_t;

static uint16_t tri_key(int a, int b, int c)
{
    return (uint16_t)(hhash((uint32_t)a << 18 | (uint32_t)b << 9 | (uint32_t)c, 77u, 3u) & 0xFFFFu);
}

static int tri_seen(const en_gen_t *st, const gen_ctx_t *x, int id)
{
    const uint16_t k = tri_key(st->id2, st->id1, id);
    int i;
    if (st->n < 2u)
        return 0;
    for (i = 1; i <= x->window && (uint32_t)i <= st->n; i++)
        if (st->seen[(st->seen_i + EN_GEN_WINDOW - (uint32_t)i) % EN_GEN_WINDOW] == k)
            return 1;
    return 0;
}

/* a voicing for the hand: led from the one before; a huddle, a sprawl or two notes a minor ninth apart are led afresh from an
 * open one */
static int hand_ugly(const uint8_t *n)
{
    int a, b;
    if (n[3] - n[0] < 7)
        return 1;
    for (a = 0; a < 4; a++)
        for (b = a + 1; b < 4; b++)
            if (n[b] - n[a] == 13)
                return 1;
    return 0;
}
static void gen_hand(const gen_ctx_t *x, const en_hchord_t *c, uint8_t *out)
{
    const int r = x->key, lo = KEYS_LO(r) + x->up, hi = KEYS_HI(r) + x->up;
    lead_to(c->keys, c->tones, x->from_k, r, lo, hi, KEYS_LO(r), out);
    if (hand_ugly(out)) {
        int from[4], v;
        uint8_t again[4];
        for (v = 0; v < 4; v++)
            from[v] = lo + 5 + 4 * v;
        lead_to(c->keys, c->tones, from, r, lo, hi, KEYS_LO(r), again);
        if (!hand_ugly(again))
            for (v = 0; v < 4; v++)
                out[v] = again[v];
    }
}

/* the colour for a root; its shape, -1 none left. not: a shape that will not do (the one being recoloured) */
static int gen_colour(const en_gen_t *st, const gen_ctx_t *x, int r, int not_sh, int use_window, uint32_t h)
{
    const uint8_t *list;
    uint32_t w[10], sum = 0u, masks[10];
    uint8_t sh_of[10], bass_of[10];
    int n = 0, cnt, i, strict, picked = -1;
    const int fn = FN_OF[x->fam][r];
    if (x->fam == FAM_SPARSE || (!IN_POOL(x->p, r, 3) && !IN_POOL(x->p, r, 4))) {
        list = r ? LIST_OPEN : LIST_TONIC_OPEN;
        cnt = r ? COUNTOF(LIST_OPEN) : COUNTOF(LIST_TONIC_OPEN);
    } else if (!r) {
        list = LIST_TONIC;
        cnt = COUNTOF(LIST_TONIC);
    } else if (IN_POOL(x->p, r, 4) && !(IN_POOL(x->p, r, 3) && x->fam == FAM_MINOR)) {
        list = LIST_MAJOR;
        cnt = COUNTOF(LIST_MAJOR);
    } else {
        list = LIST_MINOR;
        cnt = COUNTOF(LIST_MINOR);
    }
    for (strict = 1; strict >= 0 && !sum; strict--) {
        n = 0;
        for (i = 0; i < cnt; i++) {
            const int sh = list[i];
            const gshape_t *g = &GSHAPE[sh];
            en_hchord_t c;
            uint8_t kv[4];
            uint32_t wt, mask;
            int v, k, dup = 0, move = 0, far = 0, held = 0;
            en_chord_make(x->p, r, sh, &c);
            if (sh == not_sh || !chord_fit(&c, sh, strict) || (st->n && c.bass == st->prev_bass) || (int)c.bass == x->avoid_bass)
                continue;
            if (r && c.bass != r && pop12(x->usable) < 3)   /* two roots only: an inversion would leave no way on */
                continue;
            mask = 0u;
            for (v = 0; v < 4; v++)
                mask |= 1u << c.keys[v];
            for (k = 0; k < n; k++)
                dup |= masks[k] == mask && bass_of[k] == c.bass;
            if (dup || (use_window && tri_seen(st, x, r << 5 | sh)))
                continue;
            gen_hand(x, &c, kv);
            for (v = 0; v < 4; v++) {
                const int d = (int)kv[v] > x->from_k[v] ? (int)kv[v] - x->from_k[v] : x->from_k[v] - (int)kv[v];
                move += d;
                far = d > far ? d : far;
                held += !d;
            }
            wt = x->character >= 3 ? g->bright : x->character == 2 ? (uint32_t)(g->calm + g->bright + 1) / 2u : g->calm;
            if (sh == SH_ADD9 && IN_POOL(x->p, r, 3) && !IN_POOL(x->p, r, 4))
                wt = 1u;                         /* a minor add9 is a little sad for this: rarely */
            if (sh >= SH_7SUS && sh <= SH_13SUS && fn == FN_D)
                wt *= 3u;                        /* a sus chord is a dominant that does not insist */
            if (c.tones & 1u && st->away >= 2u)
                wt *= 2u;
            wt *= (uint32_t)(far > 7 || hand_ugly(kv) ? 1 : move <= 4 ? 8 : move <= 8 ? 6 : move <= 12 ? 3 : 1);
            wt *= (uint32_t)(1 + held);
            wt *= (uint32_t)(1 + pop12(c.tones & st->prev_tones));
            if (n < 10) {
                w[n] = wt;
                masks[n] = mask;
                sh_of[n] = (uint8_t)sh;
                bass_of[n] = c.bass;
                sum += wt;
                n++;
            }
        }
    }
    if (!sum)
        return -1;
    h %= sum;
    for (i = 0; i < n && picked < 0; i++) {
        if (h < w[i])
            picked = sh_of[i];
        else
            h -= w[i];
    }
    return picked;
}

/* the root: -1 none left. off: roots that were tried */
static int gen_root(const en_gen_t *st, const gen_ctx_t *x, uint32_t off, uint32_t h)
{
    uint32_t w[12], sum = 0u;
    int r;
    const int fn0 = st->n ? FN_OF[x->fam][st->prev_root & 15] : FN_T;
    for (r = 0; r < 12; r++) {
        uint32_t wt;
        int fn, bass = r;
        en_hchord_t c;
        w[r] = 0u;
        if (!(x->p >> r & 1u) || off >> r & 1u || !(r ? x->usable >> r & 1u : (st->since_t >= TONIC_GAP && x->evolve > 0) || x->usable == 1u))
            continue;
        if (r == st->prev_root || r == x->avoid_root)
            continue;
        en_chord_make(x->p, r, r ? (x->fam == FAM_SPARSE ? SH_S2 : SH_7) : (x->fam == FAM_SPARSE ? SH_S2_5 : SH_7_3), &c);
        if (!r) {
            if (!c.bass && x->usable != 1u)
                continue;
            bass = c.bass;
        }
        if (st->n && bass == st->prev_bass)
            continue;
        fn = FN_OF[x->fam][r];
        wt = FN_W[x->fam][fn0][fn];
        if (!wt)
            wt = 1u;
        wt *= MOTION[(bass - (int)st->prev_bass + 12) % 12];
        wt *= (uint32_t)(1 + pop12(c.tones & st->prev_tones));
        wt *= CHAR_FN[x->character][fn];
        wt *= FN_POS[x->pos][fn];
        if (fn == FN_T || fn == FN_M) {          /* the key stays clear: the tonic area, the longer it has been */
            uint32_t a = st->away;
            if (x->character <= 1)
                a += 1u;
            wt *= 2u + (a > 1u ? a - 1u : 0u) + (a > 6u ? 4u * (a - 6u) : 0u);
            wt /= 2u;
            if (!wt)
                wt = 1u;
        }
        if (r == st->prev2)                      /* not back and forth */
            wt = wt / 4u + 1u;
        else if (r == st->prev3)
            wt = wt / 2u + 1u;
        w[r] = wt;
        sum += wt;
    }
    if (!sum)
        return -1;
    h %= sum;
    for (r = 0; r < 12; r++) {
        if (h < w[r])
            return r;
        h -= w[r];
    }
    return -1;
}

/* a chord to compose here: root, then colour, then if three chords would come again, another */
static en_gid_t gen_compose(const en_gen_t *st, gen_ctx_t *x)
{
    en_gid_t id;
    int attempt;
    id.bars = 0u;
    for (attempt = 0; attempt < 3; attempt++) {
        uint32_t off = 0u;
        int tries;
        if (attempt)                             /* a pool too small to keep its distance from the next chord too */
            x->avoid_root = x->avoid_bass = -1;
        for (tries = 0; tries < 12; tries++) {
            const int r = gen_root(st, x, off, hhash(x->seed, st->n, 62u + (uint32_t)tries));
            int sh;
            if (r < 0)
                break;
            sh = gen_colour(st, x, r, -1, attempt < 2 && x->window > 0, hhash(x->seed, st->n, 80u + (uint32_t)tries));
            if (sh >= 0) {
                id.root = (uint8_t)r;
                id.shape = (uint8_t)sh;
                return id;
            }
            off |= 1u << r;
        }
    }
    /* a pool with one root to stand on: that, plainly */
    {
        int r;
        id.root = 0u;
        for (r = 11; r > 0; r--)
            if (x->usable >> r & 1u && (!id.root || r != st->prev_root))
                id.root = (uint8_t)r;
        id.shape = x->fam == FAM_SPARSE ? SH_S2 : SH_7;
    }
    return id;
}

/* how many times a phrase may come again */
static uint32_t rep_most(int e)
{
    const uint32_t r = e <= 0 ? 32u : e <= 50 ? 100u / (uint32_t)e : e < 100 ? 1u : 0u;
    return r > 32u ? 32u : r;
}
static uint32_t rep_draw(int e, uint32_t h)
{
    const uint32_t most = rep_most(e), lo = (most + 1u) / 2u;
    if (e > 50)
        return h % 50u < (uint32_t)(100 - e) ? 1u : 0u;
    return lo + h % (most - lo + 1u);
}

/* at a phrase's end: again or a new one */
static void gen_turn(en_gen_t *st, const gen_ctx_t *x)
{
    const uint32_t h = hhash(x->seed, st->n, 61u);
    int fresh = !st->valid;
    st->var_kind = 0u;
    if (!fresh && x->evolve > 0) {
        if (st->repeats > rep_most(x->evolve))
            st->repeats = (uint8_t)rep_most(x->evolve);
        if (x->character == 3 && st->ph_char != 3u)
            fresh = 1;
        else if (!st->repeats)
            fresh = 1;
        else
            st->repeats--;
    }
    st->ph_i = 0u;
    if (fresh) {
        uint32_t sum = 0u, pick;
        int t;
        for (t = 0; t < 6; t++)
            sum += TMPL_W[x->character][t];
        pick = (h >> 8) % sum;
        for (t = 0; t < 6; t++) {
            if (pick < TMPL_W[x->character][t])
                break;
            pick -= TMPL_W[x->character][t];
        }
        st->tmpl = (uint8_t)t;
        st->composing = 1u;
        st->valid = 1u;
        st->ph_char = (uint8_t)x->character;
        st->repeats = (uint8_t)rep_draw(x->evolve, h >> 20);
    } else {
        const uint32_t vary = (uint32_t)x->evolve * 7u / 5u;
        st->composing = 0u;
        if (x->evolve > 0 && (h >> 4) % 100u < vary) {
            st->var_kind = (h >> 12) % 5u < 3u ? 1u : 2u;   /* a recolouring, or one chord changed */
            st->var_slot = (uint8_t)((h >> 16) % TMPL[st->tmpl].n);
        }
    }
}

void en_gen_next(en_gen_t *st, uint32_t p, int key, uint32_t seed, int evolve, int character, en_gchord_t *out)
{
    gen_ctx_t x;
    en_gid_t id;
    int v, i, n;
    p = (p | 1u) & 0xFFFu;
    key = (key % 12 + 12) % 12;
    x.p = p;
    x.seed = seed;
    x.fam = en_pool_family(p);
    x.key = key;
    x.evolve = evolve < 0 ? 0 : evolve > 100 ? 100 : evolve;
    x.character = (unsigned)character < 4u ? character : 0;
    x.up = x.character == 3 ? SEC_UP : 0;
    x.usable = en_gen_roots(p);
    x.window = x.evolve > 50 ? (x.evolve - 50) * EN_GEN_WINDOW / 50 : 0;
    x.avoid_root = x.avoid_bass = -1;
    if (st->n) {
        int d = key - st->key;                   /* into a new key the hand moves with it, then leads as ever */
        if (d > 6)
            d -= 12;
        else if (d < -5)
            d += 12;
        for (v = 0; v < 4; v++) {
            x.from_k[v] = st->keys[v] + d;
            x.from_p[v] = st->pad[v] + d;
        }
        {                                        /* the hand drifts towards where this character wants it, a little a chord */
            const int want = (61 + key + x.up) * 4;
            const int mean = x.from_k[0] + x.from_k[1] + x.from_k[2] + x.from_k[3];
            const int by = mean > want + 8 ? -2 : mean < want - 8 ? 2 : 0;
            for (v = 0; v < 4; v++)
                x.from_k[v] += by;
        }
        if (p != st->pool) {                     /* the memory is of another pool now */
            st->valid = 0u;
            st->composing = 1u;
        }
    } else {
        for (v = 0; v < 4; v++) {
            x.from_k[v] = 57 + key + x.up + 4 * v;
            x.from_p[v] = 49 + key + 4 * v;
        }
    }
    st->pool = p;
    if (x.character < st->was_char && st->away < 200u)
        st->away = (uint8_t)(st->away + 1u);     /* falling: a little nearer home */
    st->was_char = (uint8_t)x.character;
    if (!st->n || st->ph_i >= TMPL[st->tmpl].n)
        gen_turn(st, &x);
    i = st->ph_i;
    n = TMPL[st->tmpl].n;
    x.pos = i == 0 ? 1 : i == n - 1 ? 2 : 0;
    if (st->composing) {
        if (i == n - 1 && st->valid && i > 0) {  /* it may come again: its end must lead to its start */
            en_hchord_t f;
            en_chord_make(p, st->phrase[0].root, st->phrase[0].shape, &f);
            x.avoid_root = f.root;
            x.avoid_bass = f.bass;
        }
        id.root = 0u;
        id.shape = 0u;
        if (st->since_b >= BORROW_GAP && x.evolve > 0 && x.fam != FAM_SPARSE && x.pos == 0 && !(hhash(seed, st->n, 63u) & 15u)) {
            en_hchord_t b;
            if (en_chord_make(p, 0, SH_BORROW, &b) && b.root != st->prev_root && b.bass != st->prev_bass &&
                (int)b.root != x.avoid_root && (int)b.bass != x.avoid_bass && !tri_seen(st, &x, b.root << 5 | SH_BORROW)) {
                id.root = b.root;
                id.shape = SH_BORROW;
            }
        }
        if (id.shape != SH_BORROW)
            id = gen_compose(st, &x);
    } else {
        en_hchord_t c;
        en_hchord_t nx;
        const en_gid_t *mem = &st->phrase[i], *after = &st->phrase[(i + 1) % n];
        int redo, sh = -1;
        id = *mem;
        en_chord_make(p, after->root, after->shape == SH_BORROW ? SH_7 : after->shape, &nx);
        en_chord_make(p, id.root, id.shape, &c);
        x.avoid_root = nx.root;
        x.avoid_bass = nx.bass;
        redo = id.shape == SH_BORROW || id.root == st->prev_root || (st->n && c.bass == st->prev_bass) ||
               (!id.root && st->since_t < TONIC_GAP);   /* a glimpse of the tonic is not kept: it goes when the phrase returns */
        if (st->var_kind == 1u && st->var_slot == i && !redo) {
            sh = gen_colour(st, &x, id.root, id.shape, 0, hhash(seed, st->n, 64u));
            if (sh >= 0)
                id.shape = (uint8_t)sh;
        } else if (st->var_kind == 2u && st->var_slot == i) {
            redo = 1;
        }
        if (redo)
            id = gen_compose(st, &x);
    }
    id.bars = TMPL[st->tmpl].bars[i];
    st->phrase[i] = id;
    st->ph_i = (uint8_t)(i + 1);

    /* the chord, its voices, and what it leaves behind */
    en_chord_make(p, id.root, id.shape, &out->ch);
    out->ch.tonic = (uint8_t)key;
    out->shape = id.shape;
    out->bars = id.bars;
    gen_hand(&x, &out->ch, out->keys_v);
    lead_to(out->ch.pad, out->ch.tones, x.from_p, key, PAD_LO(key), PAD_HI(key), PAD_LO(key), out->pad_v);
    for (v = 0; v < 4; v++) {
        st->keys[v] = out->keys_v[v];
        st->pad[v] = out->pad_v[v];
    }
    st->key = (uint8_t)key;
    st->prev3 = st->prev2;
    st->prev2 = st->prev_root;
    st->prev_root = id.root;
    st->prev_bass = out->ch.bass;
    st->prev_tones = out->ch.tones;
    {
        const int fn = FN_OF[x.fam][id.root], cur = id.root << 5 | id.shape;
        st->away = fn == FN_T || fn == FN_M ? 0u : st->away < 200u ? (uint8_t)(st->away + 1u) : st->away;
        st->since_t = !id.root ? 0u : st->since_t < 200u ? (uint8_t)(st->since_t + 1u) : st->since_t;
        st->since_b = id.shape == SH_BORROW ? 0u : st->since_b < 200u ? (uint8_t)(st->since_b + 1u) : st->since_b;
        st->seen[st->seen_i] = tri_key(st->id2, st->id1, cur);
        st->seen_i = (uint8_t)((st->seen_i + 1u) % EN_GEN_WINDOW);
        st->id2 = st->id1;
        st->id1 = (uint16_t)cur;
    }
    st->n++;
}

/* as a score, for the eye: its name, and the bass if the bass is not the root */
const char *en_gchord_name(const en_gchord_t *g, char *buf)
{
    int k = 0;
    const char *s = EN_NOTE_NAME[(g->ch.tonic + g->ch.bass) % 12];
    en_hchord_name(&g->ch, buf);
    if (g->ch.bass != g->ch.root) {
        while (buf[k])
            k++;
        buf[k++] = '/';
        while (*s)
            buf[k++] = *s++;
        buf[k] = 0;
    }
    return buf;
}

/* the engine's: the chord playing, and the next, made a little ahead (the bass leads into it) from a copy of the generator's
 * state, so that a new pool or key can have it made again. Chords, bass and held voices sound h_pool and h_root until the next
 * chord; single notes and motifs follow pool and par[P_ROOT] at once */
static en_gen_t gen_now, gen_nxt;
static en_gchord_t ch_now, ch_nxt;
static uint32_t ch_left, ch_steps, ch_count;     /* steps to the next chord; this chord's; chords since the top */
static uint32_t h_pool = POOL_IONIAN;
static int h_root;

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

static void publish_chord(void)                  /* the screen: the chord's root and the hand's notes */
{
    uint32_t m = 1u << ch_now.ch.root;
    int v, n = 0;
    for (v = 0; v < EN_NPAD; v++) {
        en_chord[v] = ch_now.keys_v[v];
        m |= 1u << ch_now.ch.keys[v];
    }
    for (; m; m &= m - 1u)
        n++;
    en_chord_root = (uint8_t)((h_root + ch_now.ch.root) % 12);
    en_chord_n = (uint8_t)n;
    en_chord_bars = ch_now.bars;
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
#if defined EN_HOST || defined PLAT_HOST
int en_test_pad_off;
#define PAD_TEST_GAIN (en_test_pad_off ? 0.0f : 1.0f)
#else
#define PAD_TEST_GAIN 1.0f
#endif
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
static float ep_vol, ep_trem, song_touch = 0.9f;
static float bloom_att = 1.0f, bloom_open = 0.00009f, bloom_close = 1.0f;   /* Bloom: the attacks' time, how quickly the body opens, closes */
static float bright_body = 1.0f, bright_tine = 1.0f, bright_bus = 0.40f;    /* Bright: the two indexes, the bus's one pole */
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
    e->arate = 1.0f / (att_s * bloom_att * EN_SR);
    e->ib = 0.0f;                                /* the body opens after the attack, then slowly closes */
    e->ibt = (0.10f + 0.95f * v2) * top * (1.0f / FM_TWO_PI) * bright_body;
    e->ibd = fm_expf(-1.0f / (1.7f * bloom_close * EN_SR));
    e->it = (0.008f + 0.10f * v2) * top * (1.0f / FM_TWO_PI) * bright_tine;
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
static void ep_chord(float vel, int with_root)
{
    int notes[5], nn = 0, i, k, covered[5], todo = 0;
    float roll;
    int32_t wait, gap;
    if (with_root) {
        const int r = (h_root + ch_now.ch.bass) % 12;
        notes[nn++] = (r < 4 ? 48 : 36) + r;
    }
    for (k = 0; k < 4; k++)
        notes[nn++] = ch_now.keys_v[k];
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
            e->ibt = fm_maxf(e->ibt, 0.5f * (0.10f + 0.95f * vel * vel) * (1.0f / FM_TWO_PI) * bright_body);
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
static uint8_t inner_set;                        /* moves only with the chord */

/* a chord's lone notes, whatever asks for them: fewer on a short one, and never more than the Notes knob says */
static uint32_t lone_max(uint32_t bars)
{
    const uint32_t most = bars >= 4u ? 3u : bars, knob = (uint32_t)par[P_NOTES];
    return most < knob ? most : knob;
}
static uint32_t ep_lone;                         /* this chord's so far */
static uint8_t fresh;                            /* a note a key just added to the pool, + 1: it speaks on the next eighth */

static void ep_inner(const inner_t *g)
{
    const float vel = (float)g->vel * 0.01f * (0.94f + 0.12f * frand());
    const int v0 = ch_now.keys_v[g->voice];
    int n, i;
    if (g->colour) {
        const int want = (h_root + ch_now.ch.col) % 12;
        n = v0 + ((want - v0) % 12 + 12) % 12;
    } else {
        n = v0 + 12;
    }
    n = live_note(n);
    for (i = 0; i < ep_lim; i++)
        if (ep[i].on && ep[i].note == n && ep[i].held)
            return;                              /* the hand has it already: leave it */
    if (ep_lone >= lone_max(ch_now.bars))
        return;
    ep_lone++;
    en_ep_singles++;
    ep_strike(n, vel, 0.030f, (int32_t)(12.0f * step_len), irand(300));
}

/* a key put a note in the pool: heard now, one soft note just over the hand, within the chord's lone notes */
static void ep_fresh(void)
{
    const int pc = (par[P_ROOT] + fresh - 1) % 12, top = ch_now.keys_v[3];
    int n = top + ((pc - top) % 12 + 12) % 12, i;
    if (!(pool >> (fresh - 1) & 1u)) {           /* taken out again meanwhile */
        fresh = 0u;
        return;
    }
    if (ep_lone >= lone_max(ch_now.bars)) {
        if (!par[P_NOTES])
            fresh = 0u;                          /* none asked for: it does not wait */
        return;                                  /* the next chord's, then */
    }
    fresh = 0u;
    if (n == top)
        n += 12;
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
static float bass_vol, bass_song, bass_song_t, bass_glide = 0.0012f;
volatile uint32_t en_bass_notes;

/* the line: two-bar figures, a note on a step and what it is. Each note lasts until the next, so a figure names only where
 * they fall; a few of them with the kick (0, 10, 16, 26), the others across it. A chord takes one figure for each two bars,
 * what does not fit a shorter chord is not played, and it may end in a tone leading to the next chord's root, which is why
 * the next chord is known before this one ends. The seed chooses, figure by figure */
enum { BV_ROOT, BV_FIFTH, BV_OCT };
typedef struct {
    uint8_t step, what;
} bnote_t;
#define BFIG_N 12
#define BFIG_MAX 7
static const bnote_t BFIG[BFIG_N][BFIG_MAX] = {
    {{0, BV_ROOT}, {10, BV_ROOT}, {16, BV_FIFTH}, {26, BV_ROOT}, {255, 0}},                   /* lighter */
    {{0, BV_ROOT}, {12, BV_FIFTH}, {16, BV_ROOT}, {26, BV_OCT}, {255, 0}},
    {{0, BV_ROOT}, {6, BV_OCT}, {16, BV_ROOT}, {22, BV_FIFTH}, {255, 0}},
    {{0, BV_ROOT}, {10, BV_ROOT}, {16, BV_FIFTH}, {22, BV_OCT}, {26, BV_ROOT}, {255, 0}},     /* fuller */
    {{0, BV_ROOT}, {6, BV_FIFTH}, {10, BV_ROOT}, {16, BV_OCT}, {26, BV_FIFTH}, {255, 0}},
    {{0, BV_ROOT}, {10, BV_OCT}, {14, BV_FIFTH}, {16, BV_ROOT}, {26, BV_FIFTH}, {255, 0}},
    {{0, BV_ROOT}, {6, BV_OCT}, {10, BV_ROOT}, {16, BV_FIFTH}, {22, BV_OCT}, {26, BV_ROOT}, {255, 0}},   /* rolling */
    {{0, BV_ROOT}, {6, BV_ROOT}, {10, BV_OCT}, {16, BV_ROOT}, {22, BV_FIFTH}, {26, BV_OCT}, {255, 0}},
    {{0, BV_ROOT}, {4, BV_OCT}, {10, BV_ROOT}, {16, BV_FIFTH}, {20, BV_ROOT}, {26, BV_FIFTH}, {255, 0}},
    {{0, BV_ROOT}, {16, BV_FIFTH}, {255, 0}},                                                 /* long */
    {{0, BV_ROOT}, {26, BV_ROOT}, {255, 0}},
    {{0, BV_ROOT}, {255, 0}},
};
#define BASS_LEAD_BACK 2u                        /* the leading tone: this many steps before the next chord */

static int bass_off(int b)                       /* the chord's bass note: in octave 1, E and D a little higher */
{
    return b < 4 ? b + 12 : b;
}

/* what the bass plays on a step of a chord of so many steps, from a set of figures (EN_BASS_*): a pure function. Its note in
 * semitones above the tonic (in the bass's octaves), and how many steps until the next one; -1 nothing on this step. Never the
 * tonic. ch and nx: this chord and the next, in the pool p; count: this chord's number, for the seeded figure */
int en_bass_ch(const en_hchord_t *ch, const en_hchord_t *nx, uint32_t p, uint32_t count, uint32_t cs, uint32_t steps, int level,
               uint32_t seed, int *len)
{
    const uint32_t half = cs / 32u, lead_at = steps - BASS_LEAD_BACK;
    const uint32_t h = hhash(seed, count, 40u + (uint32_t)ch->root * 7u + half * 3u);
    const uint32_t hl = hhash(seed, count, 41u);
    const uint32_t pick = (level == EN_BASS_LONG ? 9u : level == EN_BASS_ROLL ? 6u : level == EN_BASS_FULL ? 3u : 0u) + (h >> 8) % 3u;
    const bnote_t *f = BFIG[pick];
    const int root = bass_off(ch->bass), next = bass_off(nx->bass);
    const uint32_t in = cs - half * 32u, end = half * 32u + 32u < steps ? 32u : steps - half * 32u;
    int lead_note = -1, i, off = -1;
    if (root != next && (hl >> 16 & 3u) != 0u) {
        /* a step from the next root, in the key, not the tonic */
        static const int8_t TRY[4] = {-1, 1, -2, 2};
        for (i = 0; i < 4 && lead_note < 0; i++) {
            const int t = next + TRY[i], pc = (t + 120) % 12;
            if (pc && p >> pc & 1u)
                lead_note = t;
        }
    }
    if (lead_note >= 0 && cs == lead_at) {
        *len = (int)BASS_LEAD_BACK;
        return lead_note;
    }
    for (i = 0; i < BFIG_MAX && f[i].step != 255u; i++)
        if (f[i].step == in) {
            const int iv = f[i].what == BV_ROOT ? 0 : f[i].what == BV_FIFTH ? 7 : 12;
            uint32_t to = end, abs_to;
            off = iv == 7 ? nearest_tone(ch->tones, root + 7) : root + iv;
            if ((off + 120) % 12 == 0)               /* the tonic stays out of the bass: the root's octave instead */
                off = root + 12;
            if (off > 20)
                off -= 12;
            if (i + 1 < BFIG_MAX && f[i + 1].step < end)
                to = f[i + 1].step;
            abs_to = half * 32u + to;
            if (lead_note >= 0 && abs_to > lead_at)
                abs_to = lead_at;
            *len = (int)(abs_to - cs);
            return off;
        }
    return -1;
}

int en_bass_move(int set, int move, uint32_t h)
{
    static const uint8_t SETS[4] = {EN_BASS_LONG, EN_BASS_LIGHT, EN_BASS_FULL, EN_BASS_ROLL};
    int rank = 0, at;
    if (move == 50)
        return set;
    while (rank < 3 && SETS[rank] != set)
        rank++;
    at = move < 50 ? rank * move * 20 : rank * 1000 + (3 - rank) * (move - 50) * 20;   /* thousandths of a place in SETS */
    return SETS[at / 1000 + ((h >> 8) % 1000u < (uint32_t)(at % 1000) ? 1 : 0)];
}

/* a note. It lasts almost until the next: held at full level, a short release, and when the last one still sounds this one
 * is not attacked, it glides there in about 50 ms, phase unbroken */
static void bass_note(int off, uint32_t len)
{
    int n = 24 + h_root + off + 12 * par[P_BASSOCT];
    const int legato = bass.amp > 0.05f;
    while (n < 24)
        n += 12;                                 /* under C1 it is not a note any more */
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
 * the note and its echoes all sit in what sounds; of a chord of two bars the last note is left out, and on a chord of one bar
 * the phrase waits for the next. Every 16 bars a turn of the table: the call at the first chord of the turn, the answer in its
 * second half, where it stops on a tone that wants to fall and doesn't. Rests much longer than the phrases; the last call goes
 * unanswered. */
#define NBELL 4
typedef struct {
    float cph, mph, cinc, minc, amp, adec, idx, idec, ifl, att, arate, gl, gr;
    int32_t wait;
    uint8_t on, note, kind;
} bell_t;
static bell_t bell[NBELL];
static float bell_vol;
typedef struct {
    uint8_t step;                                /* in the chord */
    int8_t note;                                 /* above the tonic, an octave over middle C */
    uint8_t vel;
} mnote_t;
static const mnote_t MOTIF[2][4] = {
    {{8, 0, 44}, {11, 4, 46}, {14, 9, 54}, {20, 7, 50}},             /* 1 3 6, 5 */
    {{8, 4, 40}, {11, 7, 44}, {14, 11, 50}, {22, 9, 52}},            /* 3 5 7, 6 */
};
static int8_t motif_now[4];                      /* this chord's phrase, each note moved to the chord */
static int8_t motif_on;                          /* which phrase this chord has, -1 none */
/* each turn: bit 0 the call, bit 1 the answer */
#define MOTIF_TURN 16u                           /* bars */
#define MOTIF_MIN_BARS 2u                        /* a phrase needs a chord this long */
static const uint8_t MOTIF_WHEN[12] = {0, 2, 0, 3, 0, 1, 3, 0, 1, 0, 1, 0};
static uint8_t motif_due;                        /* what this turn still has to play */
static uint32_t h_pass;                          /* the pass the motifs' turns count from */
static uint32_t motif_turn;

int en_motif_due(int due, int rate, uint32_t h)
{
    if (rate < 50) {                             /* each of its phrases kept, by the rate: a fifth of them at 0 */
        const uint32_t keep = 20u + (uint32_t)rate * 8u / 5u;
        if ((h & 0xFFFFu) % 100u >= keep)
            due &= ~1;
        if ((h >> 16) % 100u >= keep)
            due &= ~2;
    } else if (rate > 50) {                      /* a place with none may have one: half of them at 100, twice the table's phrases */
        const uint32_t add = (uint32_t)(rate - 50);
        if ((h & 0xFFFFu) % 100u < add)
            due |= 1;
        if ((h >> 16) % 100u < add)
            due |= 2;
    }
    return due;
}

void en_motif_voice(int tone, int kind, float vel, float *ratio, float *index, float *idec_s, float *floor_idx)
{
    /* one family: the fundamental and a little of its neighbours, gone quickly. The answer: its second harmonic's ratio,
     * a lighter index. Tone moves the index, how long it stays and how much of it stays for good, never the ratio: a warm plain
     * voice at both ends, nearly a sine at 0, round and a little reedy at 100 */
    *ratio = kind == 0 ? 1.0f : 2.0f;
    *index = (kind == 0 ? 0.30f + 0.25f * vel : 0.14f + 0.14f * vel) * knob_curve(tone, 50, 0.22f, 1.0f, 2.2f);
    *idec_s = 0.13f * knob_curve(tone, 50, 0.45f, 1.0f, 2.8f);
    *floor_idx = (kind == 0 ? 1.0f : 0.6f) * knob_curve(tone, 50, 0.0f, 0.0f, 0.40f);
}

static void bell_strike(int kind, int n, float vel, int32_t wait)
{
    float ratio, index, idec, keep;
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
    en_motif_voice(par[P_MTONE], kind, vel, &ratio, &index, &idec, &keep);
    v->minc = v->cinc * ratio;
    v->idx = index * (1.0f / FM_TWO_PI);
    v->ifl = keep * (1.0f / FM_TWO_PI);
    v->idec = fm_expf(-1.0f / (idec * EN_SR));
    v->adec = fm_expf(-6.9f / ((kind == 0 ? 2.4f : 2.2f) * EN_SR));
    v->arate = 1.0f / (0.0040f * EN_SR);         /* about 10 ms to its top */
    p = ((kind == 0 ? -0.30f : 0.30f) + 1.0f) * (FM_PI / 4.0f);
    v->gl = fm_cosf(p);
    v->gr = fm_sinf(p);
    v->wait = wait;
    en_loop_glow[kind] = 1.0f;
    midi(0x91, (uint32_t)n, (uint32_t)(vel * 110.0f));
}

/* ------------------------------------------------------------- the drums' levels --- */
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
static float hat_lvl, hat_lvl_t, hat_vol = 1.0f;   /* hat_vol: the Hats knob, the level only: what they play is the drums' */
static const uint32_t HAT_INC[4] = {300400000u, 448200000u, 589700000u, 781100000u};   /* 3.1, 4.6, 6.1, 8.0 kHz, inharmonic */
volatile uint8_t en_hat_peak;
#if defined EN_HOST || defined PLAT_HOST
volatile uint32_t en_hat_hits;
#endif

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
#if defined EN_HOST || defined PLAT_HOST
    en_hat_hits++;
#endif
    if (!open && hat[1].on)
        hat[1].dec = fm_expf(-1.0f / (0.0035f * EN_SR));   /* choked */
}

static uint32_t song;
static int song_level;
static float brk_fc, brk_fc_t, pad_fc, pad_fc_t, duck, duck_s, lift, lift_t;

static int memcmp_part(const en_part_t *a, const en_part_t *b)
{
    return a->level != b->level || a->character != b->character || a->ramp != b->ramp || a->bass != b->bass || a->touch != b->touch;
}

static void level_set(const en_part_t *p, int first)
{
    const float ramp = first ? 1.0f : (float)p->ramp * 16.0f * step_len;
    song_level = p->level;
    hat_lvl_t = HAT_LVL[p->level];
    brk_levels(CORE_LVL[p->level], TOP_LVL[p->level], ramp);
    brk_fc_t = BRK_FC[p->level];
    pad_fc_t = PAD_FC[p->level];
    bass_song_t = (float)p->bass * 0.01f;
    song_touch = (float)p->touch * 0.01f;
    lift_t = p->character == 3 || (p->level == 3 && lift_t > 0.0f) ? 1.0f : 0.0f;
}

/* where the arranger has got to: this section and the next, which is known only as far ahead as anyone needs to ask */
static en_arr_t arr_cur, arr_nxt;
static uint32_t arr_start;                       /* the step this section began */
static en_part_t part_now, part_nxt;             /* this bar's and the next bar's */
static int arr_style;                            /* the style the bar is played in: taken on the bar */
static brk_gen_t rhythm_gen;
#if defined EN_HOST || defined PLAT_HOST
void (*en_trace)(uint32_t step, int section, int level, int character, int break_bars);
void (*en_trace_chord)(uint32_t step, const en_gchord_t *chord, int character);
#endif

static void part_at(uint32_t s, en_part_t *o)
{
    const uint32_t in = (s - arr_start) / 16u;
    if (in < arr_cur.bars)
        en_arr_part(&arr_cur, in, arr_style, par[P_ENERGY], o);
    else
        en_arr_part(&arr_nxt, in - arr_cur.bars, arr_style, par[P_ENERGY], o);
}

static uint32_t pass_at(uint32_t s)
{
    return (s - arr_start) / 16u < arr_cur.bars ? arr_cur.pass : arr_nxt.pass;
}

static void arr_follow(void)                     /* what comes after this section, as things stand */
{
    arr_nxt = arr_cur;
    en_arr_next(&arr_nxt, song_seed, arr_style, par[P_ENERGY]);
}

static void arr_bar(uint32_t s)                  /* on the bar */
{
    uint32_t in;
    arr_style = par[P_STYLE];
    if (s == 0u) {
        en_arr_first(&arr_cur, song_seed, arr_style, par[P_ENERGY]);
        arr_start = 0u;
        arr_follow();
        return;
    }
    in = (s - arr_start) / 16u;
    if (in >= arr_cur.bars) {
        arr_cur = arr_nxt;
        arr_start = s;
        arr_follow();
    } else if (in + 1u == arr_cur.bars) {
        arr_follow();                            /* the style or the energy may have moved since it was first thought of */
    }
}

/* the harmony's end of it: it hears the arranger's character at each chord */
static uint32_t motif_base;                      /* the bar that pass began at */

/* the next chord, a little ahead of it, from the pool and the key as they are now */
static uint32_t prep_pool;
static int prep_root;
static void chord_prepare(uint32_t at)
{
    en_part_t p;
    part_at(at, &p);
    gen_nxt = gen_now;
    en_gen_next(&gen_nxt, pool, par[P_ROOT], song_seed, par[P_EVOLVE], p.character, &ch_nxt);
    prep_pool = pool;
    prep_root = par[P_ROOT];
}

static int motif_fits(int m, int n, uint32_t steps)   /* a note and its first two echoes inside the chord */
{
    return (uint32_t)MOTIF[m & 1][n & 3].step + 2u * EN_ECHO_STEPS < steps;
}

/* this chord's phrase, if the turn has one due and it has the room */
static void motif_choose(uint32_t bar)
{
    const uint32_t in = bar - motif_base, turn = in / MOTIF_TURN;
    int n;
    if (turn != motif_turn) {
        motif_turn = turn;
        motif_due = (uint8_t)en_motif_due(MOTIF_WHEN[turn % 12u], par[P_MRATE], hhash(song_seed, turn + (h_pass << 8), 45u));
    }
    motif_on = -1;
    if (ch_now.bars < MOTIF_MIN_BARS)
        return;                                  /* it waits */
    if (motif_due & 1u) {
        motif_on = 0;
        motif_due &= (uint8_t)~1u;
    } else if (motif_due & 2u && in % MOTIF_TURN >= MOTIF_TURN / 2u) {
        motif_on = 1;
        motif_due &= (uint8_t)~2u;
    }
    if (motif_on >= 0)
        for (n = 0; n < 4; n++)                  /* a note that clashes moves to the nearest tone of its chord */
            motif_now[n] = (int8_t)nearest_tone(ch_now.ch.tones, MOTIF[motif_on][n].note);
}

static void chord_commit(uint32_t s, int character)   /* at the change, and only there */
{
    const uint32_t bar = s / 16u, grid = 8u - bar % 8u;
    gen_now = gen_nxt;
    ch_now = ch_nxt;
    h_pool = prep_pool;
    h_root = prep_root;
    ch_steps = (ch_now.bars < grid ? ch_now.bars : grid) * 16u;   /* (the grid holds whatever has been done to the song's counter) */
    ch_left = ch_steps;
    ch_count++;
    motif_choose(bar);
    (void)character;
#if defined EN_HOST || defined PLAT_HOST
    if (en_trace_chord)
        en_trace_chord(s, &ch_now, character);
#endif
}

/* for the tests: a motif's note, its step in the chord, and whether it plays on a chord of this length */
int en_motif_step(int m, int n) { return MOTIF[m & 1][n & 3].step; }
int en_motif_fits(int m, int n, uint32_t steps) { return motif_fits(m, n, steps); }
int en_motif_note_ch(int m, int n, const en_hchord_t *ch) { return nearest_tone(ch->tones, MOTIF[m & 1][n & 3].note); }

static void harmony_restart(void)                /* the top of the song */
{
    en_part_t p;
    part_at(0u, &p);
    en_gen_start(&gen_now);
    ch_count = 0u;
    motif_base = 0u;
    motif_turn = 0xFFFFFFFFu;
    motif_due = 0u;
    chord_prepare(0u);
    chord_commit(0u, p.character);
}

static void song_step(uint32_t s)
{
    uint32_t cs;
    int i, flags = 0, fresh_chord = 0;
    float k;

    if (!(s & 15u))
        arr_bar(s);
    if (s == 0u) {
        inner_set = 0;
        h_pass = pass_at(0u);
        harmony_restart();
        fresh_chord = 1;
    } else if (!ch_left) {
        en_part_t p;
        const uint32_t pass = pass_at(s);
        part_at(s, &p);
        if (prep_pool != pool || prep_root != par[P_ROOT])   /* changed in the very last step */
            chord_prepare(s);
        if (pass != h_pass) {                    /* a new pass: the motifs' table from its top */
            h_pass = pass;
            motif_base = s / 16u;
            motif_turn = 0xFFFFFFFFu;
        }
        chord_commit(s, p.character);
        fresh_chord = 1;
    }
    cs = ch_steps - ch_left;
    if (ch_left == 8u)
        chord_prepare(s + ch_left);
    else if (ch_left < 8u && (prep_pool != pool || prep_root != par[P_ROOT]))
        chord_prepare(s + ch_left);              /* changed since: this change takes it */
    if (!(s & 15u)) {                            /* on the bar: what the arranger wants of it */
        const uint32_t bar = s / 16u;
        uint32_t in = 0u;
        en_part_t p;
        int want;
        part_at(s, &p);
        if (s == 0u || memcmp_part(&p, &part_now))
            level_set(&p, s == 0u);
        part_now = p;
        part_at(s + 16u, &part_nxt);
        want = en_arr_break(bar, song_level, arr_style, par[P_BREAKS], song_seed, &in);
        if (want) {                              /* a break leads to a downbeat: not when the next phrase has none */
            en_part_t after;
            part_at((bar + EN_ARR_PHRASE - bar % EN_ARR_PHRASE) * 16u, &after);
            if (after.level < 2u)
                want = 0;
        }
        if (!s)
            brk_gen_start(&rhythm_gen);
        brk_gen_bar(&rhythm_gen, en_style_rhythm(arr_style), song_level, par[P_ENERGY], par[P_EVOLVE], song_seed, bar, want, in, &brk_bar);
        en_section = p.section;
        en_drum_level = p.level;
        en_break_bars = (uint8_t)want;
#if defined EN_HOST || defined PLAT_HOST
        if (en_trace)
            en_trace(s, p.section, p.level, p.character, want);
#endif
    }
    /* the kit holds its breath in the last bar before a higher level, with the air rising into it */
    if ((part_nxt.level == 2 || part_nxt.level == 3) && part_now.level != part_nxt.level) {
        const uint32_t to = 16u - (s & 15u);
        if (part_now.level >= 2)
            flags |= BRK_DROP;
        if (to == 12u)
            flags |= BRK_SWELL;
    }
    k = brk_pattern_step(s, flags, song_seed);
    if (k > 0.0f)
        duck = fm_maxf(duck, k);
    if (brk_hat_hit != '.')
        hat_hit(brk_hat_hit, brk_hat_vel, irand(40));

    /* the pad: on the change the voices move to their new notes, gliding, and the whole swells */
    if (fresh_chord) {
        for (i = 0; i < EN_NPAD; i++) {
            pad[i].note = ch_now.pad_v[i];
            pad[i].tgt = 1.0f;
        }
        publish_chord();
        pad_swell_t = 1.0f;
    } else if (cs == (ch_steps >= 32u ? 24u : 12u)) {
        pad_swell_t = 0.80f;
    }
    pad_gate = 1.0f;

    /* the piano */
    if (fresh_chord) {
        inner_set = part_now.character;
        ep_lone = 0u;
        ep_chord(0.44f + 0.04f * frand(), bass_song_t <= 0.0f);
    }
    for (i = 0; i < INNER_MAX; i++) {
        const inner_t *g = &INNER[inner_set][ch_count >> 2 & 1u][i];
        /* each chord its own place: a little later along the way, so no two are the same; never right at the next chord */
        const uint32_t at = (uint32_t)g->step + 2u * (ch_count & 3u);
        if (g->vel && at == cs && at + 4u <= ch_steps)
            ep_inner(g);
    }
    if (fresh && !(s & 1u))
        ep_fresh();

    /* the bass */
    {
        const int beat = song_level == 2 || song_level == 3, move = par[P_BMOVE];
        /* with a beat: the style's figures, by Move, a figure at a time. The breakdown and the way out: long held roots, unless
         * Move asks for more than the style's own; then a chord at a time */
        const int set = beat ? en_bass_move(STYLE[arr_style].bass[song_level == 3], move, hhash(song_seed, ch_count, 43u + cs / 32u))
                             : en_bass_move(EN_BASS_LONG, move > 50 ? move : 50, hhash(song_seed, ch_count, 44u));
        if (beat || (bass_song_t > 0.0f && set != EN_BASS_LONG)) {
            int len = 0;
            /* into a new key the bass does not lead: the next root's step would mean nothing in this one */
            const en_hchord_t *nx = ch_left <= 8u && prep_root == h_root ? &ch_nxt.ch : &ch_now.ch;
            const int off = en_bass_ch(&ch_now.ch, nx, h_pool, ch_count, cs, ch_steps, set, song_seed, &len);
            if (off >= 0)
                bass_note(off, (uint32_t)len);
        } else if (bass_song_t > 0.0f && fresh_chord) {
            bass_note(bass_off(ch_now.ch.bass), ch_steps);
        }
    }

    /* the motifs */
    if (motif_on >= 0) {
        int n;
        for (n = 0; n < 4; n++)
            if (MOTIF[motif_on][n].step == cs && motif_fits(motif_on, n, ch_steps)) {
                const int note = live_note(72 + h_root + motif_now[n]) + 12 * par[P_MOCT];
                bell_strike(motif_on, note, (float)MOTIF[motif_on][n].vel * 0.01f, irand(180));
                en_loop_pos[motif_on] = (uint8_t)n;
            }
    }
    en_drift = (float)cs * (1.0f / (float)ch_steps);
    ch_left--;
}

/* the grid: one call a sixteenth (MIDI clock: 6 ticks) */
static uint32_t sixteenth;
static void tick(void)
{
    if (!en_frozen) {
        song_step(song);
        song++;
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
    head2_v = fm_flush(head2_v + (want - head2_v) * fm_minf((float)n * (1.0f / EN_SR), 1.0f));
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

#if defined EN_HOST || defined PLAT_HOST
volatile uint32_t en_test_nans;
float en_test_peak_in;
float en_test_bass_pitch(void) { return bass.inc; }
float en_test_bass_target(void) { return bass.inc_t; }
void en_test_ticks(uint32_t n)
{
    while (n--)
        tick();
}
void en_test_jump(uint32_t step)
{
    song = step;
    arr_start = step;
}
#endif
static void tempo_set(void) { step_len = EN_SR * 60.0f / ((float)par[P_TEMPO] * 4.0f); }

/* the key or the pool changed. Playing: nothing now, the next chord takes it (song_step). Stopped: the next chord at once,
 * the time it has left unchanged, so the screen shows it and the music goes on from it */
static void harmony(void)
{
    if (par[P_TEMPO] <= 0)
        return;                                  /* not yet (en_init applies in order) */
    if (!song) {
        harmony_restart();
    } else if (!en_playing) {
        chord_prepare(song);
        {
            const uint32_t left = ch_left, steps = ch_steps;
            en_part_t p;
            part_at(song, &p);
            chord_commit(song, p.character);
            ch_left = left;
            ch_steps = steps;
        }
    }
    publish_chord();
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
    case P_KEYS: ep_vol = pad_vol = (float)(v * v) * 0.0001f; break;   /* the pad follows the piano's level, one to one */
    case P_MOTIFS: bell_vol = (float)(v * v) * 0.0001f; break;
    case P_BASS: bass_vol = (float)(v * v) * 0.0001f; break;
    case P_DRUMS:                                /* over its default it climbs slower: at 100 the kick is 10 dB up, not 14, and stays a kick */
        break_vol = v <= 45 ? (float)(v * v) * 0.0001f : (float)(45 * 45) * 0.0001f + (float)(v - 45) * (1.0f / 55.0f) * 0.42f;
        break;
    case P_BLOOM:
        bloom_att = knob_curve(v, 50, 0.2f, 1.0f, 16.0f);          /* the chord's attack: 15 ms .. 75 ms .. 1.2 s */
        bloom_open = 0.00009f / knob_curve(v, 50, 0.25f, 1.0f, 6.0f);   /* the body opens: 60 ms .. 0.25 s .. 1.5 s */
        bloom_close = knob_curve(v, 50, 0.5f, 1.0f, 2.5f);         /* and closes: 0.85 s .. 1.7 s .. 4.2 s */
        break;
    case P_BRIGHT:
        bright_body = knob_curve(v, 50, 0.25f, 1.0f, 4.0f);
        bright_tine = knob_curve(v, 50, 0.0f, 1.0f, 4.0f);
        bright_bus = knob_curve(v, 50, 0.20f, 0.40f, 0.62f);       /* 1.6 kHz .. 3.6 kHz .. 6.8 kHz */
        break;
    case P_GLIDE: bass_glide = 0.0012f / knob_curve(v, 10, 0.06f, 1.0f, 16.0f); break;   /* 1 ms .. 19 ms .. 300 ms, a time constant */
    case P_HATS: hat_vol = (float)(v * v) / 2500.0f; break;        /* 1 at 50 */
    case P_KIT: brk_set_kit(v); break;
    case P_STYLE: en_style = (uint8_t)v; break;
    case P_ECHO: echo_send = (float)(v * v) * 0.0001f; break;
    case P_ETIME: echo_time(); break;
    case P_REVERB: rev_send = (float)(v * v) * 0.0001f; break;
    case P_SIZE:
        pl_decay_t = 0.6f + 0.258f * (float)v * 0.01f;     /* shorter than AMB's: there is a beat to leave room for */
        echo_fb = 0.5f + 0.22f * (float)v * 0.01f;
        break;
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
    tape_acc = tape_lp = tape_hp = tape_p1 = tape_p2 = tape_c1 = tape_c2 = 0.0f;
    plex_lp = plex_lp2 = fb_lean = 0.0f;
    ep_trem = pulse_left = 0.0f;
    pulse = 0;
    sixteenth = 0u;
    en_playing = en_frozen = en_ext = 0u;
    hat_ph[0] = hat_ph[1] = hat_ph[2] = hat_ph[3] = 0u;
    hat[0].lp1 = hat[0].lp2 = hat[1].lp1 = hat[1].lp2 = 0.0f;
    bass.ph = bass.inc = bass.inc_t = 0.0f;
    rev_in_gain = 1.0f;
    q_r = q_w = 0;
#if defined EN_HOST || defined PLAT_HOST
    en_test_nans = 0u;
    en_hat_hits = 0u;
    en_test_peak_in = 0.0f;
#endif
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
        pad[i].inc[0] = pad[i].inc[1] = 0.0f;
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
    song_seed = EN_HARM_SEED;
    rng = RNG_START;
    hat_noise = 0x6C8E9CF5u;
    head_left = 0u;
    arr_cur.pass = arr_nxt.pass = 0u;
    arr_cur.bars = arr_nxt.bars = 0u;
    arr_cur.section = arr_nxt.section = 0u;
    arr_start = 0u;
    arr_style = 0;
    part_now.level = part_nxt.level = 0u;
    h_pass = motif_base = 0u;
    motif_on = -1;
    en_section = en_drum_level = en_break_bars = 0u;
    brk_init();                                  /* before apply: it resets what apply sets */
    for (i = 0; i < P_NPARAMS; i++)              /* the tempo last: until it is set, what needs all of them waits (echo_time, harmony) */
        if (i != P_TEMPO)
            apply(i, PARAMS[i].def);
    apply(P_TEMPO, PARAMS[P_TEMPO].def);
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
        case C_NEXT: break;                      /* (Task 4 gives it a meaning) */
        case C_FREEZE: en_frozen = (uint8_t)(a != 0); break;
        case C_SEED:
            song_seed = (uint32_t)a << 16 | (uint32_t)(b & 0xFFFF);
            rng = RNG_START ^ song_seed * 2654435761u;   /* the small errors of time and touch follow it too */
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
    const float g = pad_vol * PAD_TEST_GAIN * 0.20f * pad_swell * (1.0f + 0.30f * lift);
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

static void render_keys(uint32_t n)
{
    int i;
    uint32_t j;
    const float g = ep_vol * 0.62f * (1.0f + 0.18f * lift), cl = bright_bus + 0.12f * lift;         /* the bus's one pole: at the default ~3.6 kHz */
    float trem0, trem1, dt, tr, gd, dg;
    const float ibo = bloom_open;
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
                ib += (ibt - ib) * ibo;          /* opens over about a quarter of a second, by Bloom */
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
                ib += (ibt - ib) * ibo;
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
        inc += (bass.inc_t - inc) * bass_glide;  /* the glide: at the default about 50 ms */
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
        const float keep = v->ifl;
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
            float m = sine(mph) * (idx + keep), x;
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
    const float g = break_vol * hat_lvl * 3.3f * hat_vol;
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
    duck = fm_flush(duck * fm_expf(-(float)n * (1.0f / (0.11f * EN_SR))));
    duck_s = fm_flush(duck_s);                   /* no kick for minutes: never down among the denormals */
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
#if defined EN_HOST || defined PLAT_HOST
        if (l != l || r != r)
            en_test_nans = en_test_nans + 1u;
        else {
            const float al = l < 0.0f ? -l : l, ar = r < 0.0f ? -r : r, a = al > ar ? al : ar;
            if (a > en_test_peak_in)
                en_test_peak_in = a;
        }
#endif
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
    brk_fc += (brk_fc_t - brk_fc) * fm_minf(slow * (lift_t > 0.0f ? 4.0f : 1.0f), 1.0f);
    pad_fc += (pad_fc_t - pad_fc) * fm_minf(slow * (lift_t > 0.0f ? 4.0f : 1.0f), 1.0f);
    bass_song = fm_flush(bass_song + (bass_song_t - bass_song) * slow);
    pad_swell += (pad_swell_t - pad_swell) * fm_minf((float)n * (1.0f / (1.1f * EN_SR)), 1.0f);
    lift = fm_flush(lift + (lift_t - lift) * (lift_t > lift ? (float)n * (1.0f / (0.8f * EN_SR)) : slow * 0.75f));
    pl_decay += ((en_frozen ? 0.9995f : pl_decay_t) - pl_decay) * 0.002f;
    pl_dd2 = fm_clampf(pl_decay + 0.15f, 0.25f, 0.5f);
    rev_in_gain += ((en_frozen ? 0.0f : 1.0f) - rev_in_gain) * 0.01f;
    for (i = 0; i < EN_NLOOP; i++)
        en_loop_glow[i] = fm_flush(en_loop_glow[i] * 0.985f);
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
