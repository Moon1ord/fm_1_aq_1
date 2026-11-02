/* SPDX-License-Identifier: MPL-2.0 */
/* The break (brk.h). */
#include "brk.h"
#include "brk_data.h"

#define XF (1.0f / 88.0f)                /* a 2 ms fade */
#define TOL_INT ((float)BRK_RATE * 0.002f)
#define TOL_EXT ((float)BRK_RATE * 0.008f)

static const int16_t STEP[89] = {
    7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50, 55, 60, 66, 73, 80, 88, 97, 107, 118, 130, 143,
    157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658, 724, 796, 876, 963, 1060, 1166, 1282, 1411, 1552,
    1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871, 5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487,
    12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767};
static const int8_t INDEX[16] = {-1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8};

typedef struct {
    uint32_t pos;                        /* the next source sample to decode */
    int32_t pred, idx, left;             /* left: source samples before this slice is over */
    float frac, s0, s1, g, dg, vel;      /* vel: this hit's gain */
    int8_t slice, kit;
    uint8_t on;
} voice_t;

typedef struct {
    voice_t v[2];
    uint8_t cur;
    float lvl, tgt, ramp, rem;           /* ramp: a level a sample; rem: samples before tgt is reached */
} stem_t;

static stem_t stem[BRK_NSTEM];
static int kit;
static float inc = BRK_RATE / 44100.0f, evolve, busy, tol = TOL_INT;
static float xf_out = XF;                /* how fast what sounds gets out of a new hit's way */
static float next_vel = 1.0f;            /* the gain of the next voice started */
static uint8_t fill;
static uint32_t rs = 0x2545F491u;

brk_bar_t brk_bar;
volatile uint32_t brk_hits, brk_jumps, brk_decoded, brk_kicks, brk_snares;
volatile char brk_hit_core, brk_hit_top, brk_hat_hit;
volatile float brk_hat_vel;
volatile int8_t brk_st_core, brk_st_top;

static uint32_t rnd(void)
{
    rs ^= rs << 13;
    rs ^= rs >> 17;
    rs ^= rs << 5;
    return rs;
}
static float frand(void) { return (float)(rnd() >> 8) * (1.0f / 16777216.0f); }

static inline int32_t ima(const uint8_t *d, uint32_t p, int32_t *pred, int32_t *idx)
{
    uint32_t code = (uint32_t)d[p >> 1] >> ((p & 1u) * 4u) & 15u;
    int32_t st = STEP[*idx], dq = st >> 3, x;
    if (code & 4u) dq += st;
    if (code & 2u) dq += st >> 1;
    if (code & 1u) dq += st >> 2;
    x = *pred + (code & 8u ? -dq : dq);
    x = x > 32767 ? 32767 : x < -32768 ? -32768 : x;
    *pred = x;
    *idx += INDEX[code];
    *idx = *idx < 0 ? 0 : *idx > 88 ? 88 : *idx;
    return x;
}

static inline void next_sample(voice_t *v, int st)
{
    uint32_t p = v->pos;
    if (p >= BRK_NSAMPLES) {             /* 31 -> 0: the table's state */
        p = 0u;
        v->pred = BRK_AT[v->kit][st][0].pred;
        v->idx = BRK_AT[v->kit][st][0].idx;
    }
    v->s0 = v->s1;
    v->s1 = (float)ima(BRK_DATA[v->kit][st], p, &v->pred, &v->idx) * (1.0f / 32768.0f);
    v->pos = p + 1u;
    v->left--;
}

static void voice_start(voice_t *v, int st, int slice, float t)
{
    const brk_ima_t *a = &BRK_AT[kit][st][slice];
    v->pos = (uint32_t)slice * BRK_SLICE;
    v->pred = a->pred;
    v->idx = a->idx;
    v->slice = (int8_t)slice;
    v->kit = (int8_t)kit;
    v->left = (int32_t)((float)BRK_SLICE + t) + 2;
    v->s0 = v->s1 = 0.0f;
    v->frac = 0.0f;
    next_sample(v, st);
    next_sample(v, st);
    brk_decoded += 2u;
    v->g = 0.0f;
    v->dg = XF;
    v->vel = next_vel;
    v->on = 1u;
}

static void stem_step(int st, int slice)
{
    stem_t *s = &stem[st];
    voice_t *v = &s->v[s->cur];
    if (s->lvl <= 0.0f && s->tgt <= 0.0f)
        return;                          /* silent: nothing to decode */
    if (slice < 0) {
        if (v->on && v->dg >= 0.0f)
            v->dg = -XF;                 /* quickly: the next slice is the next sound's head (the bank's sounds have died by now) */
        return;
    }
    if (v->on && v->dg >= 0.0f && v->kit == kit && slice == (v->slice + 1) % (int)BRK_NSTEP) {
        float want = (float)((uint32_t)slice * BRK_SLICE), at = (float)v->pos - 2.0f + v->frac, d = want - at;
        if (d > (float)BRK_NSAMPLES * 0.5f)
            d -= (float)BRK_NSAMPLES;
        else if (d < -(float)BRK_NSAMPLES * 0.5f)
            d += (float)BRK_NSAMPLES;
        if (d >= -tol && d <= tol) {     /* on its way there already: no join to hear */
            v->slice = (int8_t)slice;
            v->left = (int32_t)((float)BRK_SLICE + tol + d) + 2;
            return;
        }
    }
    {
        voice_t *o = &s->v[s->cur ^ 1u];
        if (o->on && o->g > (v->on ? v->g : 0.0f)) {   /* the other still sounds louder: do not start over it */
            voice_start(v, st, slice, tol);
        } else {
            if (v->on)
                v->dg = -xf_out;
            s->cur ^= 1u;
            voice_start(o, st, slice, tol);
        }
    }
    brk_jumps++;
}

static struct {
    int8_t cur;                          /* the slice playing, -1 none */
    uint8_t left;                        /* slices of this sound still to come */
} pat[BRK_NSTEM];
static uint8_t swelling;                 /* steps until the rising air has arrived */

void brk_init(void)
{
    int i;
    for (i = 0; i < BRK_NSTEM; i++) {
        stem[i].v[0].on = stem[i].v[1].on = 0u;
        stem[i].cur = 0u;
        stem[i].lvl = stem[i].tgt = 0.0f;
        stem[i].ramp = 0.0f;
        stem[i].rem = 0.0f;
    }
    inc = BRK_RATE / 44100.0f;
    tol = TOL_INT;
    kit = 0;
    xf_out = XF;
    pat[0].left = pat[1].left = 0u;
    pat[0].cur = pat[1].cur = -1;
    swelling = 0u;
    fill = 0u;
    brk_hits = brk_jumps = brk_decoded = brk_kicks = brk_snares = 0u;
    next_vel = 1.0f;
    brk_st_core = brk_st_top = -1;
    for (i = 0; i < 16; i++)
        brk_bar.core[i] = brk_bar.top[i] = brk_bar.hat[i] = '.';
    brk_bar.is_break = 0u;
}

void brk_set_kit(int k)
{
    if ((unsigned)k > 1u)
        return;
    kit = k;                             /* a voice keeps its own kit; the next step crossfades to the new one */
}
void brk_set_tempo(float bpm) { inc = BRK_RATE * (1.0f / 44100.0f) * bpm * (1.0f / 170.0f); }
void brk_set_ext(int ext) { tol = ext ? TOL_EXT : TOL_INT; }
void brk_set_evolve(float e) { evolve = e; }
void brk_set_busy(float b) { busy = b; }

void brk_levels(float core, float top, float ramp_samples)
{
    const float r = ramp_samples < 1.0f ? 1.0f : ramp_samples;
    const float t[BRK_NSTEM] = {core, top};
    int i;
    for (i = 0; i < BRK_NSTEM; i++) {    /* each stem takes ramp_samples to get there, from wherever it is */
        float d = t[i] - stem[i].lvl;
        stem[i].tgt = t[i];
        stem[i].rem = r;
        stem[i].ramp = (d < 0.0f ? -d : d) / r;
    }
}

void brk_step(uint32_t sixteenth)
{
    const uint32_t step = sixteenth % BRK_NSTEP, bs = sixteenth % 16u;
    const int filling = fill && bs >= 13u && sixteenth / 16u % 8u == 7u;
    int core = (int)step, top = (int)step;
    if (bs == 0u)
        fill = 0u;
    else if (bs == 12u)
        fill = (uint8_t)(sixteenth / 16u % 8u == 7u && frand() < evolve);
    if (filling)
        core = (int)((step & ~15u) + 12u);   /* the snare again */
    if (frand() >= 0.4f + 0.6f * busy)
        top = -1;
    else if (frand() < evolve * 0.5f)
        top = (int)((rnd() >> 8) % 8u * 4u + step % 4u);
    brk_st_core = (int8_t)core;
    brk_st_top = (int8_t)top;
    stem_step(BRK_CORE, core);
    stem_step(BRK_TOP, top);
    if ((stem[BRK_CORE].lvl > 0.0f || stem[BRK_CORE].tgt > 0.0f) &&
        (bs == 0u || bs == 4u || bs == 10u || bs == 12u || filling))
        brk_hits++;
}

/* ---- the demo's patterns ----
 * A stem is a bank of sounds (tools/gen_breaks.c lays them out; these maps must match it). A pattern names a sound on
 * a step; the following steps ask for its following slices, so the sound rings out over stem_step's seamless path and
 * no join is heard; and when the sound is over the stem goes quiet. One grid, 170 BPM sixteenths. The hats are named
 * here too, but played by the engine: brk_hat_hit tells it which, and how hard.
 *
 * Light drum and bass: a plain two-step, kept plain. When the arranger wants a break, one or two bars (sometimes four) of a
 * chopped, rolling one from a few hand-written ones, chosen by the seed. All of it is a pure function of what it is told. */
#define PAT_XF (1.0f / 1400.0f)          /* a new hit takes over from the tail it cuts over about 30 ms */
#define PAT_CHOKE (1.0f / 500.0f)        /* the top: about 11 ms */
#define PAT_N 16u
#define BLOCK_BARS 8u                     /* a phrase: a break is chosen by it, an open hat falls at its end */
#define BLOCK_STEPS (BLOCK_BARS * PAT_N)
typedef struct {
    char c;
    uint8_t slice, len;
} bank_t;
static const bank_t BANK_CORE[] = {{'K', 0, 4}, {'k', 4, 4}, {'S', 8, 5}, {'s', 13, 5}, {'c', 18, 2}, {'t', 20, 4}, {0, 0, 0}};
static const bank_t BANK_TOP[] = {{'g', 0, 2}, {'f', 2, 2}, {'e', 4, 2}, {'r', 6, 2}, {'~', 20, 12}, {0, 0, 0}};

/* one bar. Level 0 nothing, 1 hats alone, 2 the light two-step, 3 the fuller one, 4 half time, a colour for the breakdown */
static const char PAT_CORE[5][PAT_N + 1] = {
    "................",
    "................",
    "K...S.....k.S...",
    "K...S.....k.S...",
    "K.......s.......",
};
static const char PAT_HAT[5][PAT_N + 1] = {
    "................",
    "..i...i...i...i.",
    "..h...h...h...h.",
    "..h...hi..h...hi",
    "..i...h...i...h.",
};
/* the little that makes the styles heard until the rhythm is generated: density's half-time pulse (levels 2, 3),
 * syncopation's displaced kick, density's rolling hats */
static const char PAT_HALF[2][PAT_N + 1] = {"K.......S.......", "K.....k.S.....k."};
static const char PAT_SYNCO[PAT_N + 1] = "K...S..k..k.S...";
static const char HAT_ROLL[PAT_N + 1] = "..hi..hi..hi.ihi";
/* the bar's accent, on the hats */
static const uint8_t HAT_ACC[16] = {90, 60, 88, 62, 100, 60, 94, 66, 90, 60, 84, 62, 100, 60, 96, 70};
/* a few ghosts at the fuller level, by bar */
#define NGHOST 8
#define B1(a) (1u << (a))
static const uint16_t GHOSTS[NGHOST] = {0u, 0u, B1(7), 0u, B1(15), 0u, B1(7), B1(3)};
#undef B1

/* the breaks: two bars each, core and ghosts, ours and hand-written. Chopped, displaced, rolling, quiet. A one-bar break
 * is the second bar of one of these, four bars are two of them */
#define NBREAK 6
#define BREAK_LEN 32u
static const char BREAK_CORE[NBREAK][BREAK_LEN + 1] = {
    "K...S.....K.S...K.K.S.....k.S.c.",
    "K.....S...K.S..kK...S..k..K.S...",
    "K...S..c..k.S.c.K.c.S.....k.S.c.",
    "K..k..S...K...S.K..k..S...k.S.s.",
    "K...S.k...K.S.k.K...S.k..kK.S...",
    "K.....S...k.S...K.....S.k...S.c.",
};
static const char BREAK_TOP[NBREAK][BREAK_LEN + 1] = {
    ".......g.....f.......f.g.....e..",
    "...f.....g.....e...g....e.......",
    ".....f.g.......g.....f..........",
    "..g......f.....e...g......f.....",
    ".g.....f.....e...g.....f.....e..",
    "...g.f.....g.f.....e.f.....g...f",
};

static uint32_t hmix(uint32_t seed, uint32_t what, uint32_t a)
{
    uint32_t x = (seed + what * 0x9E3779B1u) ^ (a * 2654435761u);
    x ^= x >> 15;
    x *= 0x85EBCA77u;
    x ^= x >> 13;
    x *= 0xC2B2AE3Du;
    return x ^ x >> 16;
}
static float hfrac(uint32_t h) { return (float)(h >> 8) * (1.0f / 16777216.0f); }   /* 0..1 */

uint32_t brk_hash(uint32_t seed, uint32_t what, uint32_t a) { return hmix(seed, what, a); }

/* what the bar is, from what is wanted of it */
void brk_drums_for(const brk_rhythm_t *r, int level, int energy, uint32_t seed, uint32_t bar, int break_bars, uint32_t break_in)
{
    static const uint8_t DISPLACED[3] = {1, 3, 4};   /* the breaks across the beat */
    const int lv = (unsigned)level < 5u ? level : 0, two = lv == 2 || lv == 3;
    const char *core = PAT_CORE[lv], *hat = PAT_HAT[lv];
    uint32_t i;
    if (two) {
        if (r->density < 35u)
            core = PAT_HALF[lv - 2];
        else if (r->synco >= 60u)
            core = PAT_SYNCO;
        if (r->density >= 70u)
            hat = HAT_ROLL;
    }
    brk_bar.is_break = 0u;
    for (i = 0u; i < PAT_N; i++) {
        brk_bar.core[i] = core[i];
        brk_bar.hat[i] = hat[i];
        brk_bar.top[i] = '.';
    }
    if (break_bars > 0) {
        const uint32_t h = hmix(seed, 5u, bar / BLOCK_BARS), pos = break_bars == 1 ? 16u : (break_in & 1u) * 16u;
        const uint32_t pick = h >> 24;
        const uint32_t second = break_bars == 4 && break_in >= 2u;
        const int b = r->synco >= 60u ? DISPLACED[(pick + second) % 3u] : (int)((pick % NBREAK + second) % NBREAK);
        brk_bar.is_break = 1u;
        for (i = 0u; i < PAT_N; i++) {
            brk_bar.core[i] = BREAK_CORE[b][pos + i];
            brk_bar.top[i] = BREAK_TOP[b][pos + i];
        }
        return;
    }
    if (lv == 3 && r->ghost >= 20u) {
        const uint32_t m = GHOSTS[hmix(seed, 3u, bar) % NGHOST];
        for (i = 0u; i < PAT_N; i++)
            if (m >> i & 1u)
                brk_bar.top[i] = 'G';
    }
    if (two && r->ghost >= 60u) {                /* more of them, on the off sixteenths, as far as the energy goes */
        const uint32_t chance = (uint32_t)(r->ghost - 40u) * (uint32_t)(energy > 0 ? energy : 0) / 100u;
        for (i = 1u; i < PAT_N; i += 2u)
            if (brk_bar.core[i] == '.' && hmix(seed, 7u, bar * PAT_N + i) % 100u < chance)
                brk_bar.top[i] = 'G';
    }
}

static const bank_t *bank_find(const bank_t *b, char c)
{
    for (; b->c; b++)
        if (b->c == c)
            return b;
    return 0;
}

static void pat_play(int st, const bank_t *hit, float vel)
{
    if (hit) {
        pat[st].cur = (int8_t)hit->slice;
        pat[st].left = (uint8_t)(hit->len - 1u);
        next_vel = vel;
        stem_step(st, hit->slice);
        next_vel = 1.0f;
    } else if (pat[st].left) {
        pat[st].cur++;
        pat[st].left--;
        stem_step(st, pat[st].cur);
    } else if (pat[st].cur >= 0) {
        pat[st].cur = -1;
        stem_step(st, -1);
    }
}

float brk_pattern_step(uint32_t step, int level, int flags, uint32_t seed)
{
    const uint32_t bs = step % PAT_N, bar = step / PAT_N, blk = step % BLOCK_STEPS;
    const int lv = (unsigned)level < 5u ? level : 0;
    const uint32_t h = hmix(seed, 2u, step);
    const int plain = !(flags & (BRK_DROP | BRK_SWELL));
    char cc = flags & BRK_DROP ? '.' : brk_bar.core[bs];
    char tc = flags & BRK_SWELL ? '~' : '.', hc = flags & BRK_DROP ? '.' : brk_bar.hat[bs];
    const bank_t *bc;
    float vc, vt = 0.72f + 0.42f * hfrac(hmix(seed, 4u, step));
    /* the firm and the lighter snare, and a stable accent: the second backbeat a little under the first */
    if (cc == 'S' && h % 9u == 0u)
        cc = 's';
    vc = cc == 'K' || cc == 'k' ? 0.95f + 0.10f * hfrac(h) : (bs == 12u ? 0.88f : 0.96f) + 0.10f * hfrac(h);
    if (plain) {
        tc = brk_bar.top[bs];
        if (brk_bar.is_break)
            vc *= 0.92f;                         /* a break is not louder */
        else if (tc == 'G')
            tc = (h >> 5) & 1u ? 'f' : 'e';
    }
    /* the hats: three shades by position; at a block's end, sometimes the open one */
    if (hc == 'h') {
        hc = "hjjl"[h >> 3 & 3u];        /* the off sixteenths: mostly a little later, and not the same twice */
        if (lv >= 2 && blk == BLOCK_STEPS - 2u && (h & 1u))
            hc = 'o';
    } else if (lv == 3 && hc == '.' && bs == 14u && bar % 4u == 3u && !(h & 3u)) {
        hc = 'o';
    }
    if (flags & BRK_SWELL)
        swelling = 12u;
    else if (swelling) {                 /* the rising air has the top to itself until it has arrived */
        swelling--;
        tc = '.';
    }
    brk_hat_hit = hc;
    brk_hat_vel = (float)HAT_ACC[bs] * 0.01f * (0.86f + 0.28f * hfrac(hmix(seed, 6u, step)));
    bc = bank_find(BANK_CORE, cc);
    brk_st_core = (int8_t)(bc ? bc->slice : -1);
    brk_hit_core = cc;
    brk_hit_top = tc;
    xf_out = PAT_XF;
    pat_play(BRK_CORE, bc, vc);
    xf_out = PAT_CHOKE;
    pat_play(BRK_TOP, bank_find(BANK_TOP, tc), vt);
    if (cc == 'K' || cc == 'k')
        brk_kicks++;
    if (cc == 'S' || cc == 's' || cc == 'c')
        brk_snares++;
    if (bc && stem[BRK_CORE].tgt > 0.0f)
        brk_hits++;
    return cc == 'K' ? vc : cc == 'k' ? 0.7f * vc : 0.0f;
}

void brk_stop(void)
{
    int i, j;
    for (i = 0; i < BRK_NSTEM; i++)
        for (j = 0; j < 2; j++)
            if (stem[i].v[j].on)
                stem[i].v[j].dg = -XF;
}

void brk_render_stem(int st, float *out, uint32_t n)
{
    int w;
    uint32_t dec = 0u;
    stem_t *s = &stem[st];
    const float l0 = s->lvl;
    float l1 = s->tgt, dl;
    uint32_t j;
    if (!n)
        return;
    /* the level moves with time, sounding or not: l0 -> l1 over this block, linearly */
    if ((float)n < s->rem) {
        l1 = l0 + (s->tgt > l0 ? (float)n * s->ramp : -(float)n * s->ramp);
        s->rem -= (float)n;
    } else {
        s->rem = 0.0f;
    }
    dl = (l1 - l0) * (1.0f / (float)n);
    for (w = 0; w < 2; w++) {
        voice_t *v = &s->v[w];
        float lv = l0;
        if (!v->on)
            continue;
        for (j = 0; j < n; j++) {
            float x;
            v->frac += inc;
            while (v->frac >= 1.0f) {
                v->frac -= 1.0f;
                next_sample(v, st);
                dec++;
            }
            if (v->left <= 0 && v->dg >= 0.0f)
                v->dg = -XF;
            v->g += v->dg;
            if (v->g >= 1.0f) {
                v->g = 1.0f;
                v->dg = 0.0f;
            } else if (v->g <= 0.0f) {
                v->on = 0u;
                break;
            }
            x = v->s0 + (v->s1 - v->s0) * v->frac;
            lv += dl;
            out[j] += x * v->g * lv * v->vel;
        }
    }
    s->lvl = l1;
    if (s->lvl <= 0.0f && s->tgt <= 0.0f)
        s->v[0].on = s->v[1].on = 0u;
    brk_decoded += dec;
}

void brk_render(float *out, uint32_t n)
{
    int st;
    for (st = 0; st < BRK_NSTEM; st++)
        brk_render_stem(st, out, n);
}

void brk_decode_range(int k, int st, uint32_t start, uint32_t n, int16_t *o)
{
    const brk_ima_t *a = &BRK_AT[k][st][start / BRK_SLICE];
    int32_t pred = a->pred, idx = a->idx;
    uint32_t i;
    for (i = 0u; i < n; i++)
        o[i] = (int16_t)ima(BRK_DATA[k][st], start + i, &pred, &idx);
}

uint32_t brk_stem_hash(int k, int st)
{
    int32_t pred = BRK_AT[k][st][0].pred, idx = BRK_AT[k][st][0].idx;
    uint32_t i, h = 2166136261u;
    for (i = 0u; i < BRK_NSAMPLES; i++)
        h = (h ^ (uint32_t)(uint16_t)ima(BRK_DATA[k][st], i, &pred, &idx)) * 16777619u;
    return h;
}
uint32_t brk_stem_expected(int k, int st) { return BRK_HASH[k][st]; }
