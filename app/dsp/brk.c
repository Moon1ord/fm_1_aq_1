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
    float late;                          /* source samples: how far behind the grid it was started */
    int32_t wait;                        /* output samples before it starts */
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
static int32_t next_wait;                /* and how late, in output samples */
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
    v->wait = next_wait;
    v->late = (float)next_wait * inc;
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
        float want = (float)((uint32_t)slice * BRK_SLICE), at = (float)v->pos - 2.0f + v->frac + v->late, d = want - at;
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
    next_wait = 0;
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

/* ---- the patterns ----
 * A stem is a bank of sounds (tools/gen_breaks.c lays them out; these maps must match it). A bar names a sound on a step;
 * the following steps ask for its following slices, so the sound rings out over stem_step's seamless path and no join is
 * heard; and when the sound is over the stem goes quiet. One grid, 170 BPM sixteenths. The hats are named here too, but
 * played by the engine: brk_hat_hit tells it which, and how hard. The bars themselves are composed below. */
#define PAT_XF (1.0f / 1400.0f)          /* a new hit takes over from the tail it cuts over about 30 ms */
#define PAT_CHOKE (1.0f / 500.0f)        /* the top: about 11 ms */
#define PAT_N 16u
typedef struct {
    char c;
    uint8_t slice, len;
} bank_t;
static const bank_t BANK_CORE[] = {{'K', 0, 4}, {'k', 4, 4}, {'S', 8, 5}, {'s', 13, 5}, {'c', 18, 2}, {'t', 20, 4}, {0, 0, 0}};
static const bank_t BANK_TOP[] = {{'g', 0, 2}, {'f', 2, 2}, {'e', 4, 2}, {'r', 6, 2}, {'~', 20, 12}, {0, 0, 0}};

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

/* ---- the rhythm: bars composed one at a time ----
 * A pure function of its state and what it is told. Everything is whole numbers, from hashes of the seed; nothing here is
 * a table of bars. What is fixed is the genre: the backbone, where kicks may fall and how strongly, where ghosts may, how
 * many of each. A groove is two bars that stay a while, with small changes that go away again (so they cannot wander),
 * then another. A break is built by rules, element by element, inside a budget of hits that rises to its last bar. */
#define BIT(p) (1u << (p))
#define BACK_TWO (BIT(4) | BIT(12))
#define BACK_HALF BIT(8)
#define HAT_OFF (BIT(2) | BIT(6) | BIT(10) | BIT(14))
#define HAT_ON (BIT(0) | BIT(4) | BIT(8) | BIT(12))

static uint32_t rn(uint32_t *x)
{
    uint32_t v = *x;
    v ^= v << 13;
    v ^= v >> 17;
    v ^= v << 5;
    *x = v;
    return v;
}
static int pct(uint32_t *x, int p) { return (int)((rn(x) >> 8) % 100u) < p; }
static uint32_t rn_seed(uint32_t h) { return h ? h : 0x9E3779B1u; }
static int popc(uint32_t m)
{
    int n = 0;
    for (; m; m &= m - 1u)
        n++;
    return n;
}
static int clamp100(int v) { return v < 0 ? 0 : v > 100 ? 100 : v; }

/* so many of something, on average, in thousandths: the whole part, and the rest by chance */
static int how_many(uint32_t *x, int thousandths)
{
    return thousandths / 1000 + ((int)((rn(x) >> 8) % 1000u) < thousandths % 1000);
}

/* A choice among positions with weights, syncopation favouring the odd sixteenths. Two-step and rollers: after 0 and 10, the
 * kick pushes the bar across the middle (7, then 6), answers just before the backbeat (11) or picks up the next bar (14). 8 and 2
 * are the straighter, breakbeat ones, and 3, 5, 9 and 13 rarer colours. Half time: 10 is the answer to the snare on 8, 6 and 14 its
 * neighbours. Never 1 or 15: next to the downbeats. */
static const uint8_t KICK_POS[10] = {2, 3, 5, 6, 7, 8, 9, 11, 13, 14}, KICK_W[10] = {8, 3, 2, 20, 25, 8, 3, 20, 2, 15};
static const uint8_t HKICK_POS[8] = {3, 5, 6, 7, 10, 11, 13, 14}, HKICK_W[8] = {14, 4, 26, 10, 48, 18, 4, 22};
/* Ghosts: weak sixteenths, not next to the backbeat */
static const uint8_t GHOST_POS[4] = {1, 7, 9, 15}, GHOST_W[4] = {8, 40, 36, 28};
static const uint8_t HGHOST_POS[6] = {1, 3, 5, 11, 13, 15}, HGHOST_W[6] = {10, 24, 20, 30, 20, 26};
/* The hat's sixteenths, around its off-beats */
static const uint8_t HAT16_POS[6] = {3, 5, 7, 11, 13, 15}, HAT16_W[6] = {12, 6, 30, 14, 8, 30};

static int weighted(uint32_t *x, const uint8_t *pos, const uint8_t *w, int n, int sy, uint32_t bad)
{
    uint32_t t = 0u, r;
    int i;
    for (i = 0; i < n; i++)
        if (!(bad >> pos[i] & 1u))
            t += (uint32_t)w[i] * (uint32_t)(pos[i] & 1u ? 40 + sy : 40);
    if (!t)
        return -1;
    r = rn(x) % t;
    for (i = 0; i < n; i++)
        if (!(bad >> pos[i] & 1u)) {
            const uint32_t v = (uint32_t)w[i] * (uint32_t)(pos[i] & 1u ? 40 + sy : 40);
            if (r < v)
                return pos[i];
            r -= v;
        }
    return -1;
}

static int is_half(const brk_rhythm_t *r, int level) { return level == 4 || r->density < 35u; }

/* what the numbers come to at a level and an energy */
static int eff_density(const brk_rhythm_t *r, int level, int energy)
{
    static const uint8_t F[5] = {0, 40, 75, 100, 40};
    return clamp100((int)r->density * F[(unsigned)level < 5u ? level : 0] / 100 + (energy - 70) / 5);
}
static int eff_ghost(const brk_rhythm_t *r, int level, int energy)
{
    static const uint8_t F[5] = {0, 0, 60, 100, 30};
    if (!r->ghost)
        return 0;
    return clamp100((int)r->ghost * F[(unsigned)level < 5u ? level : 0] / 100 + (energy - 70) / 5);
}

void brk_gen_limits(const brk_rhythm_t *r, int level, int energy, int *kicks, int *snares_ghosts, int *break_hits)
{
    const int d = eff_density(r, level, energy), g = eff_ghost(r, level, energy), half = is_half(r, level);
    const int gm = g <= 0 ? 0 : g < 15 ? 1 : g < 40 ? 2 : g < 65 ? 3 : 4;
    int k = half ? 2 + (d >= 15) : 2 + (d >= 45) + (d >= 75) + (r->synco >= 60u);
    if (k > 4)
        k = 4;
    if (level < 2 || level > 4) {
        k = 0;
        *snares_ghosts = 0;
    } else {
        *snares_ghosts = (half ? 1 : 2) + gm;
    }
    *kicks = k;
    *break_hits = 6 + r->density / 25 + r->synco / 25;
}

/* --- the groove --- */
static uint32_t kick_bad(uint32_t m, uint32_t back)
{
    return m | m << 1 | m >> 1 | back | BIT(0) | BIT(1) | BIT(15);
}

static uint32_t ghost_bad(uint32_t g, uint32_t kick, uint32_t back) { return g | g << 1 | g >> 1 | kick | back; }

static void groove_make(brk_groove_t *o, uint32_t h, const brk_rhythm_t *r, int level, int energy)
{
    const int half = is_half(r, level), d = eff_density(r, level, energy), sy = r->synco;
    const int g = eff_ghost(r, level, energy);
    const uint32_t back = half ? BACK_HALF : BACK_TWO;
    const int ke = level == 4 ? d * 10 : half ? d * 25 : (d > 30 ? (d - 30) * 20 : 0) + (sy > 50 ? (sy - 50) * 8 : 0);
    const int ge = g * 6 + g * g * 26 / 100, he = d * d * 8 / 10;
    int kmax, gmax, bh, t, i;
    uint32_t x = rn_seed(h);
    brk_gen_limits(r, level, energy, &kmax, &gmax, &bh);
    gmax -= half ? 1 : 2;
    if (gmax < 0)
        gmax = 0;
    o->accent = (uint8_t)(rn(&x) >> 11);
    o->open = 0u;
    for (t = 0; t < 2; t++) {
        uint32_t m = 0u, gm = 0u, hm = 0u;
        if (kmax > 0) {
            int n;
            if (t == 0 || sy < 40 || !pct(&x, (sy - 40) / 3))
                m = BIT(0);
            else
                m = BIT(2);                      /* the second bar comes in late: the groove leans */
            if (!half) {
                if (pct(&x, t == 0 ? 98 - sy / 5 : 92 - sy / 3)) {
                    m |= BIT(10);
                } else {
                    const int alt = rn(&x) % 5u;
                    m |= BIT(alt < 2 ? 11 : alt < 4 ? 7 : 14);
                }
            }
            n = how_many(&x, t == 0 ? ke * 3 / 4 : ke * 5 / 4);
            for (i = 0; i < n && popc(m) < kmax; i++) {
                const int p = half ? weighted(&x, HKICK_POS, HKICK_W, 8, sy, kick_bad(m, back)) : weighted(&x, KICK_POS, KICK_W, 10, sy, kick_bad(m, back));
                if (p < 0)
                    break;
                m |= BIT(p);
            }
            /* the rare deliberate double: the Amen's two kicks before its snare */
            if (!half && t == 1 && sy >= 40 && popc(m) < kmax && pct(&x, 3 + (sy - 40) / 5)) {
                if ((m & BIT(10)) && !(m & BIT(11)))
                    m |= BIT(11);
                else if ((m & BIT(6)) && !(m & (BIT(7) | BIT(8))))
                    m |= BIT(7);
            }
        }
        if (gmax > 0) {
            int n = how_many(&x, ge);
            for (i = 0; i < n && popc(gm) < gmax; i++) {
                const int p = half ? weighted(&x, HGHOST_POS, HGHOST_W, 6, 0, ghost_bad(gm, m, back)) : weighted(&x, GHOST_POS, GHOST_W, 4, 0, ghost_bad(gm, m, back));
                if (p < 0)
                    break;
                gm |= BIT(p);
            }
        }
        if (level >= 1 && level <= 4) {
            int n = how_many(&x, he);
            hm = HAT_OFF;
            if (d >= 40 && pct(&x, 12))
                hm |= HAT_ON;                    /* eighths, the on-beats soft */
            for (i = 0; i < n && i < 5; i++) {
                const int p = weighted(&x, HAT16_POS, HAT16_W, 6, 0, hm);
                if (p < 0)
                    break;
                hm |= BIT(p);
            }
            if (t == 1 && d < 30 && pct(&x, 15))
                hm &= ~BIT(6);                   /* sparse: one off-beat left out */
            if (t == 1 && level >= 2 && pct(&x, 8 + d / 5)) {
                hm |= BIT(11);                   /* an open one just before the snare, shut by the next closed */
                o->open |= 1u;
            }
        }
        o->kick[t] = (uint16_t)m;
        o->ghost[t] = (uint16_t)gm;
        o->hat[t] = (uint16_t)hm;
    }
}

/* one small change. The kick on the downbeat stays, and so does what the bar is known by, the kick on 10 */
static void groove_mutate(brk_groove_t *c, uint32_t *x, const brk_rhythm_t *r, int level, int energy)
{
    const int half = is_half(r, level), sy = r->synco;
    const uint32_t back = half ? BACK_HALF : BACK_TWO;
    const int t = (int)(rn(x) >> 9 & 1u), what = (int)(rn(x) >> 8) % 100;
    int kmax, gmax, bh, p;
    brk_gen_limits(r, level, energy, &kmax, &gmax, &bh);
    gmax -= half ? 1 : 2;
    if (what < 40 && gmax > 0 && kmax > 0) {     /* a ghost moved, or one more, one less */
        uint32_t g = c->ghost[t];
        if (g) {
            g &= g - 1u;                         /* the first one goes */
            if (!(rn(x) & 3u) && popc(c->ghost[t]) > 0) {
                c->ghost[t] = (uint16_t)g;
                return;
            }
        }
        p = half ? weighted(x, HGHOST_POS, HGHOST_W, 6, 0, ghost_bad(g, c->kick[t], back) | c->ghost[t]) : weighted(x, GHOST_POS, GHOST_W, 4, 0, ghost_bad(g, c->kick[t], back) | c->ghost[t]);
        if (p >= 0 && popc(g) < gmax)
            c->ghost[t] = (uint16_t)(g | BIT(p));
        return;
    }
    if (what < 70 && kmax > 0) {                 /* one kick moved, or added within the limit */
        const uint32_t m0 = c->kick[t], fixed = BIT(0) | BIT(10);
        uint32_t m = m0;
        const uint32_t extra = m0 & ~fixed & ~(BIT(2) | BIT(3));
        if (extra && (m0 & BIT(11) ? !(m0 & BIT(10)) : 1)) {
            m &= ~(extra & (0u - extra));        /* the first extra goes, and comes back elsewhere */
        } else if (popc(m0) >= kmax) {
            c->accent ^= (uint8_t)BIT(rn(x) & 7u);
            return;
        }
        p = half ? weighted(x, HKICK_POS, HKICK_W, 8, sy, kick_bad(m, back) | m0) : weighted(x, KICK_POS, KICK_W, 10, sy, kick_bad(m, back) | m0);
        if (p >= 0) {
            c->kick[t] = (uint16_t)(m | BIT(p));
            c->ghost[t] &= (uint16_t)~BIT(p);
        }
        return;
    }
    c->accent ^= (uint8_t)BIT(rn(x) & 7u);       /* a hat accent */
    if (level >= 1 && rn(x) & 1u) {
        p = weighted(x, HAT16_POS, HAT16_W, 6, 0, BIT(11));
        if (p >= 0) {                            /* and a sixteenth of it, in or out */
            c->hat[t] ^= (uint16_t)BIT(p);
        }
    }
}

static int same_rhythm(const brk_gen_t *g, const brk_rhythm_t *r, int level, int energy)
{
    return g->level == (uint8_t)level && g->density == (uint8_t)eff_density(r, level, energy) && g->ghostn == (uint8_t)eff_ghost(r, level, energy) &&
           g->synco == r->synco && g->half == (uint8_t)is_half(r, level);
}

/* how long a groove stays. Evolve under 25 keeps it long, up to 128 bars. From there on, four to eight bars, even numbers of
 * them, so a two-bar pattern is whole, and most of them run out at a phrase's start. High Evolve leans to the shorter ones. */
static uint32_t groove_life(uint32_t *x, uint32_t bar, int evolve)
{
    const uint32_t pos = bar % BRK_PHRASE;
    if (evolve < 25) {
        const int e = 25 - evolve;
        const uint32_t n = 8u + (uint32_t)(e * e * 120 / 625);
        return n + (BRK_PHRASE - (pos + n) % BRK_PHRASE) % BRK_PHRASE;
    } else {
        static const uint8_t LIFE[3] = {4, 6, 8};
        uint32_t w[3], t = 0u, v;
        int i;
        for (i = 0; i < 3; i++) {
            w[i] = (pos + LIFE[i]) % BRK_PHRASE == 0u ? 12u : 1u;
            if (evolve >= 75 && i == 2)
                w[i] = w[i] / 2u + 1u;
            t += w[i];
        }
        v = rn(x) % t;
        for (i = 0; i < 3; i++) {
            if (v < w[i])
                return LIFE[i];
            v -= w[i];
        }
        return 8u;
    }
}

static void groove_new(brk_gen_t *g, uint32_t seed, const brk_rhythm_t *r, int level, int energy, uint32_t bar, int evolve)
{
    uint32_t x = rn_seed(hmix(seed, 31u, g->gno));
    groove_make(&g->base, hmix(seed, 30u, g->gno), r, level, energy);
    g->cur = g->base;
    g->start = bar;
    g->life = (uint16_t)groove_life(&x, bar, evolve);
    g->level = (uint8_t)level;
    g->density = (uint8_t)eff_density(r, level, energy);
    g->ghostn = (uint8_t)eff_ghost(r, level, energy);
    g->synco = r->synco;
    g->half = (uint8_t)is_half(r, level);
}

void brk_gen_start(brk_gen_t *g)
{
    uint32_t i;
    uint8_t *p = (uint8_t *)g;
    for (i = 0u; i < sizeof *g; i++)
        p[i] = 0u;
}

/* --- a bar of it --- */
static void bar_clear(brk_bar_t *o)
{
    int i;
    for (i = 0; i < 16; i++) {
        o->core[i] = o->top[i] = o->hat[i] = '.';
        o->vcore[i] = o->vtop[i] = o->vhat[i] = o->late[i] = 0u;
    }
    o->is_break = 0u;
    o->rest = 0u;
}

static const uint8_t HAT_ACC[16] = {90, 60, 88, 62, 100, 60, 94, 66, 90, 60, 84, 62, 100, 60, 96, 70};

/* a hat: the off-beats' shade (a little later each, and not the same twice: the swing) at the levels that are firm; the rest soft */
static void hat_put(brk_bar_t *o, int i, char c, uint32_t *x, int shape)
{
    const uint32_t z = rn(x);
    o->hat[i] = c == 'h' ? "hjjl"[z >> 8 & 3u] : c;
    o->vhat[i] = (uint8_t)((int)HAT_ACC[i] * (86 + (int)(z >> 12) % 29) * shape / 10000);
}

static void plain_bar(const brk_gen_t *g, int level, uint32_t seed, uint32_t bar, brk_bar_t *o)
{
    const uint32_t t = (bar - g->start) & 1u, pos = bar % BRK_PHRASE;
    const brk_groove_t *c = &g->cur;
    uint32_t x = rn_seed(hmix(seed, 3u, bar));
    const uint32_t back = level < 2 || level > 4 ? 0u : g->half ? BACK_HALF : BACK_TWO;
    uint32_t kick = c->kick[t], ghost = c->ghost[t], hat = c->hat[t];
    int i;
    if (level >= 2 && level <= 4 && pos == 0u) {
        kick |= BIT(0);                          /* a phrase starts on it, always */
        kick &= ~BIT(1);
    }
    for (i = 0; i < 16; i++) {
        const uint32_t b = BIT(i);
        const uint32_t z = rn(&x), v = z >> 8 & 0xFFFFu;
        if (back & b) {
            o->core[i] = level == 4 || z % 9u == 0u ? 's' : 'S';
            o->vcore[i] = (uint8_t)((i == 12 ? 88u : 96u) + v % 11u);
        } else if (kick & b) {
            o->core[i] = i == 0 ? 'K' : 'k';
            o->vcore[i] = (uint8_t)(95u + v % 11u);
        } else if (ghost & b) {
            o->top[i] = z >> 5 & 1u ? 'f' : 'e';
            o->vtop[i] = (uint8_t)(72u + v % 43u);
            o->late[i] = (uint8_t)(2u + (hmix(seed, 32u, g->gno) + (uint32_t)i) % 5u);
        }
        if (hat & b) {
            const int off = (int)(HAT_OFF & b ? 1u : 0u);
            char hc = 'i';
            int shape = 100;
            if (off) {
                const int acc = (c->accent >> ((uint32_t)(i >> 2) + 4u * t)) & 1;
                shape = acc ? 108 : 94;
                hc = level == 1 || (level == 4 && (i == 2 || i == 10)) ? 'i' : 'h';
            } else if ((c->open & 1u) && t == 1u && i == 11) {
                hc = 'o';
            }
            hat_put(o, i, hc, &x, shape);
        }
    }
    /* the phrase's end: sometimes the open one, now and then a rim or a tom to lead over */
    if (level >= 2 && level <= 4) {
        const uint32_t w = hmix(seed, 33u, bar);
        if (o->hat[14] != '.' && ((pos == BRK_PHRASE - 1u && (w & 1u)) || (level == 3 && bar % 4u == 3u && !(w >> 1 & 3u)))) {
            o->hat[14] = 'o';
            if (o->hat[11] == 'o')
                o->hat[11] = 'i';                /* one at a time: each is shut by a closed one */
        }
        if (pos == BRK_PHRASE - 1u) {
            const int roll = (int)((w >> 8) % 100u);
            const int p = 13 + (int)(w >> 3 & 1u) + (int)(w >> 4 & 1u);
            if (roll < 7 && o->top[p] == '.' && o->core[p] == '.') {
                o->top[p] = 'r';
                o->vtop[p] = (uint8_t)(72u + (w >> 16) % 30u);
            } else if (roll >= 7 && roll < 11 && o->core[14] == '.' && o->top[14] == '.') {
                o->core[14] = 't';
                o->vcore[14] = (uint8_t)(80u + (w >> 16) % 20u);
            }
        }
    }
}

/* --- a break --- */
typedef struct {
    brk_bar_t *o;
    int n, first, cap, runmax, budget, hmax, tail, last;
} bk_t;

static int bk_occ(const brk_bar_t *o, int p) { return o->core[p] != '.' || o->top[p] != '.'; }

static int bk_run_with(const bk_t *b, int p)     /* the hits in a row, if p were one; the next bar's kick counts, in the break */
{
    int n = 1, i;
    for (i = p - 1; i >= 0 && bk_occ(b->o, i); i--)
        n++;
    if (i < 0)
        n += b->tail;
    for (i = p + 1; i < 16 && bk_occ(b->o, i); i++)
        n++;
    if (i == 16 && !b->last)
        n++;
    return n;
}

static int bk_put(bk_t *b, int p, char c, int vel, int force)
{
    brk_bar_t *o = b->o;
    const int top = c == 'g' || c == 'f' || c == 'e' || c == 'r';
    if (p < 0 || p > 15 || bk_occ(o, p) || b->n >= b->hmax || bk_run_with(b, p) > b->runmax)
        return 0;
    if (!force && (b->n >= b->budget || (p < 8 && b->first >= b->cap)))
        return 0;
    if (top) {
        o->top[p] = c;
        o->vtop[p] = (uint8_t)vel;
    } else {
        o->core[p] = c;
        o->vcore[p] = (uint8_t)(vel * 92 / 100);  /* a break is not louder */
    }
    b->n++;
    b->first += p < 8;
    return 1;
}

static int is_kick(char c) { return c == 'K' || c == 'k'; }

/* never next to another kick in a break either, never on 1 or 15, next to the downbeats */
static int kick_fits(const brk_bar_t *o, int p)
{
    return p > 1 && p < 15 && !is_kick(o->core[p - 1]) && !is_kick(o->core[p + 1]);
}

static void bk_kick(bk_t *b, int p, uint32_t *x)
{
    if (kick_fits(b->o, p))
        bk_put(b, p, 'k', 95 + (int)(rn(x) >> 8) % 11, 0);
}

enum { EL_DISPLACE, EL_ROLL, EL_GHOSTS, EL_CHOP, EL_HATS, EL_N };

/* elements: the run of ghosts, soft to firmer; the roll, chopped snares rising; the chop: kick and snare in threes and twos */
static void el_ghosts(bk_t *b, uint32_t *x, int gh, int end, int firm)
{
    int k = 2 + gh / 30, i;
    if (k > b->runmax)
        k = b->runmax;
    for (i = 0; i < k; i++) {
        const int stage = (i * 3) / k;
        const char c = stage == 0 ? 'e' : stage == 1 || !firm ? 'f' : 'g';
        if (bk_put(b, end - k + 1 + i, c, 76 + i * 10, 1)) {
            b->o->late[end - k + 1 + i] = (uint8_t)(rn(x) >> 8 & 3u);
        }
    }
}

static void el_roll(bk_t *b, uint32_t *x, int sy, int end)
{
    const int close = sy >= 40;                  /* mild: every other step; else each */
    int k = 2 + (int)(rn(x) >> 8 & 1u) + (sy >= 60 && (rn(x) >> 8 & 1u)), i;
    if (k > (close ? b->runmax : 3))
        k = close ? b->runmax : 3;
    for (i = 0; i < k; i++) {
        const int p = end - (k - 1 - i) * (close ? 1 : 2);
        bk_put(b, p, 'c', 56 + i * 48 / (k > 1 ? k - 1 : 1), 1);
    }
}

static void el_chop(bk_t *b, uint32_t *x, int from)
{
    int p = from, snare = 0;
    while (p < 15) {
        if (snare)
            bk_put(b, p, rn(x) & 1u ? 's' : 'c', 80 + (p - from) * 2, 0);
        else
            bk_kick(b, p, x);
        snare = !snare;
        p += rn(x) >> 8 & 3u ? 3 : 2;            /* threes, a two now and then */
    }
}

static void bk_snares(bk_t *b, uint32_t *x, int half, int sy, int last)
{
    const int v = 96 + (int)(rn(x) >> 8) % 11;
    if (half) {
        static const int8_t OUT[3] = {6, 10, 11};
        bk_put(b, pct(x, sy + 10) ? OUT[rn(x) >> 8 & 1u ? 0 : (rn(x) >> 8 & 1u) + 1] : 8, 'S', v, 1);
    } else {
        static const int8_t A[3] = {6, 7, 3}, B[4] = {10, 11, 14, 13};
        const int d2 = pct(x, sy + 10 + (last ? 10 : 0)), d1 = pct(x, sy / 2) && (sy >= 40 || !d2);
        bk_put(b, d1 ? A[(rn(x) >> 8) % 3u] : 4, d1 && (rn(x) & 1u) ? 's' : 'S', v, 1);
        bk_put(b, d2 ? B[(rn(x) >> 8) % 4u] : 12, d2 && (rn(x) & 1u) ? 's' : 'S', 88 + (int)(rn(x) >> 8) % 11, 1);
    }
}

/* The hats of a break: the off-beats go on (a rest takes them too); an element may run sixteenths into the end, rising */
static void bk_hats(brk_bar_t *o, uint32_t *x, int fill_from, int rest_from, int rest_to, int last, int d)
{
    int i;
    for (i = 2; i < 16; i += 4)
        if (i < rest_from || i >= rest_to)
            hat_put(o, i, 'h', x, 100);
    if (d >= 60)
        for (i = 7; i < 16; i += 8)
            if ((i < rest_from || i >= rest_to) && rn(x) & 1u)
                hat_put(o, i, 'i', x, 100);
    if (fill_from >= 0)
        for (i = fill_from; i < 16; i++)
            if (i < rest_from || i >= rest_to) {
                hat_put(o, i, i & 1 ? 'i' : 'h', x, 100);
                o->vhat[i] = (uint8_t)(50 + (i - fill_from) * 11);
            }
    if (rest_from > 15 && fill_from < 0) {
        if (last && pct(x, 35))
            o->hat[14] = 'o';
        else if ((o->core[12] == 'S' || o->core[12] == 's') && pct(x, 15) )
            o->hat[10] = 'o';
    }
}

static void break_bar(brk_gen_t *g, const brk_rhythm_t *r, int energy, int evolve, uint32_t seed, uint32_t bar, int len, uint32_t in, brk_bar_t *o)
{
    const int d = clamp100((int)r->density + (energy - 70) / 5), sy = r->synco;
    const int gh = !r->ghost ? 0 : clamp100((int)r->ghost + (energy - 70) / 5);
    const int half = r->density < 35u, last = in + 1u == (uint32_t)len;
    /* low Evolve: the same break comes back */
    const uint32_t id = evolve < 25 ? g->gno * 8u + (uint32_t)len : bar - in;
    uint32_t xb = rn_seed(hmix(seed, 40u, id)), x = rn_seed(hmix(seed, 41u, id * 4u + in));
    const int drop_pct = 8 + (100 - d) / 5;
    const int drop_roll = (int)(rn(&xb) >> 8) % 100, drop_bar = (int)(rn(&xb) >> 8) % len;
    const int lo = 3 + (int)r->density / 25 + sy / 30, span = 1 + ((int)r->density >= 30) + (sy >= 50 || gh >= 60);
    int kmax, sm, hmax, i, rest_from = 16, rest_to = 16, fill = -1;
    bk_t b;
    brk_gen_limits(r, 3, energy, &kmax, &sm, &hmax);
    b.o = o;
    b.n = b.first = 0;
    b.tail = in ? g->tail : 0;
    b.runmax = 3 + (sy >= 50 || gh >= 60);
    b.budget = lo + span * (int)(in + 1u) / len + (energy >= 85) - (energy < 40);
    if (b.budget < 3)
        b.budget = 3;
    if (in && b.budget < g->brk_hits)            /* it never falls back, whatever the last bar's elements came to */
        b.budget = g->brk_hits;
    if (b.budget >= hmax)
        b.budget = hmax - 1;                     /* room for what leads to the downbeat */
    b.cap = b.budget / 2;
    b.hmax = last ? hmax : hmax - 1;
    b.last = last;
    o->is_break = 1u;
    /* the frame: the bar begins on its kick, a later bar sometimes comes in leaning */
    if (in == 0u || !pct(&x, sy / 4)) {
        bk_put(&b, 0, 'K', 95 + (int)(rn(&x) >> 8) % 11, 1);
    } else {
        bk_put(&b, 2 + (int)(rn(&x) >> 8 & 1u), 'k', 95 + (int)(rn(&x) >> 8) % 11, 1);
    }
    if (drop_roll < drop_pct && drop_bar == (int)in) {
        /* the rest: a hit or two, then a big gap, then either the downbeat itself or a snare out of nowhere */
        rest_from = 5 + (int)(rn(&x) >> 8) % 3;
        bk_put(&b, 4, 'S', 96 + (int)(rn(&x) >> 8) % 11, 1);
        if (last) {
            rest_to = 16;
            if (pct(&x, 40) && gh > 0) {
                bk_put(&b, 15, 'e', 72 + (int)(rn(&x) >> 8) % 20, 1);
                rest_to = 15;
            }
        } else {
            rest_to = rest_from + 6 + (int)(rn(&x) >> 8) % 3;
            bk_put(&b, rest_to, 'S', 92 + (int)(rn(&x) >> 8) % 11, 1);
            if (rest_to + 2 < 16)
                bk_put(&b, rest_to + 2, 'c', 80, 1);
        }
        o->rest = (uint8_t)(rest_to - rest_from);
    } else {
        int w[EL_N], t, v, e;
        w[EL_DISPLACE] = 10;
        w[EL_ROLL] = sy / 4 + d / 10;
        w[EL_GHOSTS] = gh / 2;
        w[EL_CHOP] = sy > 25 ? sy / 2 : 0;
        w[EL_HATS] = 14;
        t = 0;
        for (e = 0; e < EL_N; e++)
            t += w[e];
        v = (int)(rn(&x) >> 8) % t;
        for (e = 0; e < EL_N - 1 && v >= w[e]; e++)
            v -= w[e];
        if (e != EL_CHOP)
            bk_snares(&b, &x, half, sy, last);
        else
            bk_put(&b, 4, 'S', 96 + (int)(rn(&x) >> 8) % 11, 1);
        switch (e) {
        case EL_ROLL: el_roll(&b, &x, sy, last ? 15 : 11 + (int)(rn(&x) >> 8 & 1u) * 4); break;
        case EL_GHOSTS: el_ghosts(&b, &x, gh, last ? 15 : 8 + (int)(rn(&x) >> 8 & 7u), sy >= 40 || gh >= 60); break;
        case EL_CHOP: el_chop(&b, &x, 6 + (int)(rn(&x) >> 8 & 1u)); break;
        case EL_HATS: fill = 10 + (int)((rn(&x) >> 8) % 3u); break;
        default: break;
        }
        /* up to the budget: kicks where they go, ghosts on the weak steps */
        for (i = 0; i < 24 && b.n < b.budget; i++) {
            if (gh > 0 && pct(&x, 20 + gh / 2)) {
                const int p = (int)(rn(&x) >> 8 & 7u) * 2 + 1;
                if (bk_put(&b, p, rn(&x) & 1u ? 'f' : 'e', 72 + (int)(rn(&x) >> 8) % 43, 0))
                    o->late[p] = (uint8_t)(2u + (rn(&x) >> 8) % 5u);
            } else {
                const int p = half ? weighted(&x, HKICK_POS, HKICK_W, 8, sy, 0u) : pct(&x, 45) ? 10 : weighted(&x, KICK_POS, KICK_W, 10, sy, 0u);
                bk_kick(&b, p, &x);
            }
        }
        for (i = 15; i >= 2 && b.n < b.budget; i--) {  /* whatever is left: from the end back */
            if (i & 1 && gh > 0)
                bk_put(&b, i, 'e', 72 + (int)(rn(&x) >> 8) % 30, 0);
            else if (i < 15)
                bk_kick(&b, i, &x);
        }
        for (i = 15; i >= 2 && b.n < b.budget; i--)  /* still short of it: anywhere that does not make a run */
            bk_put(&b, i, gh > 0 ? 'e' : 'c', 72 + (int)(rn(&x) >> 8) % 20, 1);
        /* the last bar's second half is the busier */
        for (i = 15; last && i >= 8 && b.first > b.n - b.first; i--) {
            if (i & 1 && gh > 0)
                bk_put(&b, i, 'e', 72 + (int)(rn(&x) >> 8) % 30, 1);
            else if (i < 15 && kick_fits(o, i))
                bk_put(&b, i, 'k', 95, 1);
            else if (i < 15)
                bk_put(&b, i, 'c', 72, 1);
        }
        /* the last bar leads to the downbeat: something in its last three steps, not a kick right before it */
        if (last && !bk_occ(o, 13) && !bk_occ(o, 14) && !bk_occ(o, 15)) {
            const int p = 14 + (int)(rn(&x) >> 8 & 1u);
            const int kind = (int)(rn(&x) >> 8) % 100;
            if (kind < 12)
                bk_put(&b, p, 'r', 74 + (int)(rn(&x) >> 8) % 26, 1);
            else if (kind < 20 && p == 14)
                bk_put(&b, 14, 't', 86, 1);
            else if (gh > 0 && kind < 60)
                bk_put(&b, p, 'f', 80 + (int)(rn(&x) >> 8) % 26, 1);
            else
                bk_put(&b, p, 'c', 74 + (int)(rn(&x) >> 8) % 26, 1);
        }
    }
    bk_hats(o, &x, fill, rest_from, rest_to, last, d);
    b.tail = 0;
    for (i = 15; i >= 0 && bk_occ(o, i); i--)
        b.tail++;
    g->tail = (uint8_t)b.tail;
    if (!o->rest)
        g->brk_hits = (uint8_t)b.n;
    else if (!in)
        g->brk_hits = 0u;
}

void brk_gen_bar(brk_gen_t *g, const brk_rhythm_t *r, int level, int energy, int evolve, uint32_t seed, uint32_t bar, int break_bars, uint32_t break_in, brk_bar_t *o)
{
    const int lv = (unsigned)level < 5u ? level : 0;
    const uint32_t pos = bar % BRK_PHRASE;
    const int len = break_bars == 1 || break_bars == 2 || break_bars == 3 || break_bars == 4 ? break_bars : 0;
    /* only what ends with its phrase is a break: the rest of what is asked is not */
    const int is_brk = len && break_in < (uint32_t)len && pos + (uint32_t)len - break_in == BRK_PHRASE && lv >= 2 && lv <= 3;
    if (!g->on || g->seed != seed || bar != g->next) {   /* a start, a jump, another piece */
        g->on = 1u;
        g->seed = seed;
        g->gno = bar;
        groove_new(g, seed, r, lv, energy, bar, evolve);
    } else if (bar - g->start >= g->life && !((bar - g->start) & 1u)) {
        g->gno++;
        groove_new(g, seed, r, lv, energy, bar, evolve);
    } else {
        const uint32_t start = g->start, life = g->life;
        if (!same_rhythm(g, r, lv, energy)) {    /* the level or the style changed: the same groove, as it would be here */
            groove_new(g, seed, r, lv, energy, bar, evolve);
            g->start = start;
            g->life = (uint16_t)life;
        }
        if (bar != g->start && !((bar - g->start) & 1u)) {   /* the pattern comes round: it may differ a little, from what it was */
            uint32_t x = rn_seed(hmix(seed, 34u, bar));
            const int p = evolve <= 0 ? 2 : 2 + evolve * 106 / 100;
            g->cur = g->base;
            if (pct(&x, p)) {
                groove_mutate(&g->cur, &x, r, lv, energy);
                if (evolve >= 90)
                    groove_mutate(&g->cur, &x, r, lv, energy);
            }
        }
    }
    g->next = bar + 1u;
    bar_clear(o);
    if (is_brk)
        break_bar(g, r, energy, evolve, seed, bar, len, break_in, o);
    else
        plain_bar(g, lv, seed, bar, o);
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

float brk_pattern_step(uint32_t step, int flags, uint32_t seed)
{
    const uint32_t bs = step % PAT_N;
    const int plain = !(flags & (BRK_DROP | BRK_SWELL));
    const char cc = flags & BRK_DROP ? '.' : brk_bar.core[bs];
    char tc = flags & BRK_SWELL ? '~' : '.';
    const char hc = flags & BRK_DROP ? '.' : brk_bar.hat[bs];
    const bank_t *bc = bank_find(BANK_CORE, cc);
    const float vc = (float)brk_bar.vcore[bs] * 0.01f;
    float vt = 0.72f + 0.42f * hfrac(hmix(seed, 4u, step));
    if (plain) {
        tc = brk_bar.top[bs];
        vt = (float)brk_bar.vtop[bs] * 0.01f;
    }
    if (flags & BRK_SWELL)
        swelling = 12u;
    else if (swelling) {                 /* the rising air has the top to itself until it has arrived */
        swelling--;
        tc = '.';
    }
    brk_hat_hit = hc;
    brk_hat_vel = (float)brk_bar.vhat[bs] * 0.01f;
    brk_st_core = (int8_t)(bc ? bc->slice : -1);
    brk_hit_core = cc;
    brk_hit_top = tc;
    xf_out = PAT_XF;
    pat_play(BRK_CORE, bc, vc);
    xf_out = PAT_CHOKE;
    next_wait = plain ? (int32_t)brk_bar.late[bs] * 44 : 0;
    pat_play(BRK_TOP, bank_find(BANK_TOP, tc), vt);
    next_wait = 0;
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
        uint32_t j0 = 0u;
        if (!v->on)
            continue;
        if (v->wait > 0) {
            if ((uint32_t)v->wait >= n) {
                v->wait -= (int32_t)n;
                continue;
            }
            j0 = (uint32_t)v->wait;
            v->wait = 0;
            lv = l0 + dl * (float)j0;
        }
        for (j = j0; j < n; j++) {
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
