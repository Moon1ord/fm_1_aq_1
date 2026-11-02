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
    float frac, s0, s1, g, dg;
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
static uint8_t fill;
static uint32_t rs = 0x2545F491u;

volatile uint32_t brk_hits, brk_jumps, brk_decoded;
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
    brk_hits = brk_jumps = brk_decoded = 0u;
    brk_st_core = brk_st_top = -1;
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
 * no join is heard; and when the sound is over the stem goes quiet. One grid, 170 BPM sixteenths; the levels differ
 * only in what they choose. A new hit takes over from the tail it cuts over about 45 ms, not 2 ms. */
#define PAT_XF (1.0f / 2048.0f)
#define PAT_N 64u
typedef struct {
    char c;
    uint8_t slice, len;
} bank_t;
static const bank_t BANK_CORE[] = {{'K', 0, 6}, {'k', 6, 6}, {'S', 12, 10}, {'s', 22, 10}, {0, 0, 0}};
static const bank_t BANK_TOP[] = {{'h', 0, 2},  {'i', 2, 2},  {'o', 4, 6},  {'g', 10, 3}, {'f', 13, 3},
                                  {'r', 16, 2}, {'x', 18, 2}, {'~', 20, 12}, {0, 0, 0}};

/* four bars of 170: at level 2 sixteen steps are a half-time bar (85 BPM: kick, then the one backbeat snare) */
static const char PAT_CORE[4][PAT_N + 1] = {
    "................................................................",
    "................................................................",
    "K.......S.......K.......S.......K.......S.......K.....k.S.......",
    "K...S.....k.s...K...S.....k.s...K...S.....k.s...K...S.....k.s...",
};
static const char PAT_TOP[4][PAT_N + 1] = {
    "................................................................",
    "....i.......f...............i.......i.........f.............o...",
    "....h.......h.i.....h.....g.h.......h.......h.i.....h.......o...",
    "..h...i...h...i...h...i.g.h...i...h...if..h...i...h...i.g.h...o.",
};

static const bank_t *bank_find(const bank_t *b, char c)
{
    for (; b->c; b++)
        if (b->c == c)
            return b;
    return 0;
}

static void pat_play(int st, const bank_t *hit)
{
    if (hit) {
        pat[st].cur = (int8_t)hit->slice;
        pat[st].left = (uint8_t)(hit->len - 1u);
        stem_step(st, hit->slice);
    } else if (pat[st].left) {
        pat[st].cur++;
        pat[st].left--;
        stem_step(st, pat[st].cur);
    } else if (pat[st].cur >= 0) {
        pat[st].cur = -1;
        stem_step(st, -1);
    }
}

float brk_pattern_step(uint32_t step, int level, int flags)
{
    const uint32_t s = step % PAT_N;
    const char *core = PAT_CORE[(unsigned)level < 4u ? level : 0], *top = PAT_TOP[(unsigned)level < 4u ? level : 0];
    const char cc = flags & BRK_DROP ? '.' : core[s];
    char tc = flags & BRK_SWELL ? '~' : flags & BRK_DROP ? '.' : top[s];
    const bank_t *hc = bank_find(BANK_CORE, cc);
    if (flags & BRK_SWELL)
        swelling = 12u;
    else if (swelling) {                 /* the rising air has the top to itself until it has arrived */
        swelling--;
        tc = '.';
    }
    xf_out = PAT_XF;
    brk_st_core = (int8_t)(hc ? hc->slice : -1);
    pat_play(BRK_CORE, hc);
    pat_play(BRK_TOP, bank_find(BANK_TOP, tc));
    if (hc && stem[BRK_CORE].tgt > 0.0f)
        brk_hits++;
    return cc == 'K' ? 1.0f : cc == 'k' ? 0.7f : 0.0f;
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
            out[j] += x * v->g * lv;
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
