/* SPDX-License-Identifier: MPL-2.0 */
/* The melody. The generator by itself, en_mel_next, over every pool and root: what makes each note right. The generator in the engine over
 * hours (en_test_ticks: no time passing): what makes it a melody and how much of it there is. The knobs. The voices, measured. The keys out
 * of its way. Its level against them, and its room. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include "amb.h"
#include "brk.h"

static int fails;
#define CHECK(c, ...)                                                                                                  \
    do {                                                                                                               \
        if (!(c)) {                                                                                                    \
            if (fails < 60) {                                                                                          \
                printf("FAIL %s:%d: ", __FILE__, __LINE__);                                                            \
                printf(__VA_ARGS__);                                                                                   \
                printf("\n");                                                                                          \
            }                                                                                                          \
            fails++;                                                                                                   \
        }                                                                                                              \
    } while (0)

/* MIDI, a ledger of channel 2 and what the keys say */
static int mel_down, mel_most, mel_bad_off;
static uint8_t mel_held[128];
static uint32_t mel_ons, key_ons, key_high;
static int key_watch;
void en_midi_out(uint32_t st, uint32_t d1, uint32_t d2)
{
    (void)d2;
    if (st == 0x91u) {
        mel_ons++;
        mel_down++;
        mel_held[d1 & 127u]++;
        mel_most = mel_down > mel_most ? mel_down : mel_most;
    } else if (st == 0x81u) {
        if (!mel_held[d1 & 127u])
            mel_bad_off++;
        else
            mel_held[d1 & 127u]--;
        mel_down--;
    } else if (st == 0x90u) {
        key_ons++;
        if (key_watch && (int)d1 >= key_watch)
            key_high++;
    }
}
static void ledger_reset(void)
{
    mel_down = mel_most = mel_bad_off = 0;
    memset(mel_held, 0, sizeof mel_held);
    mel_ons = key_ons = key_high = 0u;
}

/* ------------------------------------------------------------------ the phrases as they come */
#define NPH 6000
#define NCH 30000
typedef struct {
    uint32_t at;
    en_phrase_t p;
    uint8_t section;
} ph_t;
static ph_t ph[NPH];
static int nph;
typedef struct {
    uint32_t step;
    en_gchord_t g;
} ch_t;
static ch_t chs[NCH];
static int nch;
static uint32_t bars_in_section[EN_NSECTIONS];
static uint8_t section_now;
static void on_phrase(uint32_t s, const en_phrase_t *p)
{
    if (nph < NPH) {
        ph[nph].at = s;
        ph[nph].p = *p;
        ph[nph].section = section_now;
        nph++;
    }
}
static void on_chord(uint32_t s, const en_gchord_t *g, int c)
{
    (void)c;
    if (nch < NCH) {
        chs[nch].step = s;
        chs[nch].g = *g;
        nch++;
    }
}
static void on_bar(uint32_t s, int section, int level, int character, int brk)
{
    (void)s, (void)level, (void)character, (void)brk;
    section_now = (uint8_t)section;
    bars_in_section[section]++;
}
static const ch_t *chord_at(uint32_t step)
{
    int a = 0, b = nch - 1;
    while (a < b) {
        const int m = (a + b + 1) / 2;
        if (chs[m].step <= step)
            a = m;
        else
            b = m - 1;
    }
    return nch && chs[a].step <= step ? &chs[a] : 0;
}
static uint32_t abs_tones(const en_hchord_t *c)
{
    const int t = c->tonic;
    return ((uint32_t)c->tones << t | (uint32_t)c->tones >> (12 - t)) & 0xFFFu;
}
static int bars_of(double minutes) { return (int)(minutes * 170.0 / 4.0 + 0.5); }

static void engine(uint32_t pool, int root, uint32_t seed, int style)
{
    static int32_t b[512];
    en_init();
    en_trace_mel = on_phrase;
    en_trace_chord = on_chord;
    en_trace = on_bar;
    en_seed(seed);
    en_set(P_ROOT, root);
    en_pool(pool);
    en_set(P_STYLE, style);
    en_render(b, 16, 1024);
    nph = nch = 0;
    memset(bars_in_section, 0, sizeof bars_in_section);
    en_play(1);
    en_render(b, 16, 1024);
}
static void set_now(int p, int v)                /* (ticks do not read the commands: a block does) */
{
    static int32_t b[512];
    en_set(p, v);
    en_render(b, 16, 1024);
}
static void engine_off(void)
{
    en_trace_mel = 0;
    en_trace_chord = 0;
    en_trace = 0;
}
static void play_bars(int bars)
{
    int i;
    for (i = 0; i < bars; i++)
        en_test_ticks(16u);
}

/* a phrase's own checks: its notes against chords it was given */

static void phrase_shape(const en_phrase_t *p, const char *w)
{
    int i;
    CHECK(p->n >= 2 && p->n <= EN_MEL_NOTES, "%s: %d notes", w, p->n);
    CHECK(p->timbre < EN_NTIMBRES, "%s: timbre %d", w, p->timbre);
    for (i = 0; i < p->n; i++) {
        const en_mnote_t *o = &p->note[i];
        CHECK(o->len >= 2 && o->len <= 32, "%s: a note of %d steps", w, o->len);
        CHECK(o->vel >= 20 && o->vel <= 100, "%s: velocity %d", w, o->vel);
        if (i)
            CHECK(o->step >= p->note[i - 1].step + p->note[i - 1].len, "%s: note %d at %d over the last, at %d for %d: a line is one note at a time", w, i, o->step, p->note[i - 1].step, p->note[i - 1].len);
        CHECK(p->end >= o->step + o->len, "%s: it ends at %d, a note goes on to %d", w, p->end, o->step + o->len);
    }
    CHECK(p->note[0].step >= EN_MEL_LEAD - 6u && p->note[0].step <= EN_MEL_LEAD + 8u, "%s: its first note at %d of the call", w, p->note[0].step);
    /* no busy runs: not three quick notes running */
    for (i = 2; i < p->n; i++)
        CHECK(p->note[i].step - p->note[i - 2].step > 6, "%s: three notes in %d steps", w, p->note[i].step - p->note[i - 2].step);
}

/* every pool of three notes or more, every root: the generator with chords the harmony would give */
static void t_all_pools(void)
{
    uint32_t mask, phrases = 0u, notes = 0u, ops_most = 0u;
    int key;
    for (mask = 1u; mask <= 4095u; mask += 2u) {
        if (__builtin_popcount(mask) < 3)
            continue;
        for (key = 0; key < 12; key++) {
            en_gen_t gs;
            en_mel_t st;
            en_gchord_t g[EN_MEL_CHORDS + 12];
            const int lo = EN_MEL_LO(key), hi = lo + EN_MEL_RANGE > EN_MEL_TOP ? EN_MEL_TOP : lo + EN_MEL_RANGE;
            const uint32_t seed = mask * 7u + (uint32_t)key;
            int r, c = 0, k;
            uint32_t pool_abs = (mask << key | mask >> (12 - key)) & 0xFFFu;
            en_gen_start(&gs);
            en_mel_start(&st);
            memset(&st, 0, sizeof st);
            for (k = 0; k < EN_MEL_CHORDS + 12; k++)
                en_gen_next(&gs, mask, key, seed, 50, k & 3, EN_MASK_ALL, &g[k]);
            for (r = 0; r < 6; r++) {
                en_mel_in_t in;
                en_phrase_t p;
                char w[64];
                memset(&in, 0, sizeof in);
                int i, total = 0;
                uint32_t ops0;
                in.pool = mask;
                in.seed = seed;
                in.key = key;
                in.section = (int)((seed + (uint32_t)r) % EN_NSECTIONS);
                in.evolve = 50;
                in.rate = 50;
                in.tone = 50;
                in.echo_steps = EN_ECHO_STEPS;
                in.nchords = EN_MEL_CHORDS;
                for (k = 0; k < EN_MEL_CHORDS; k++) {
                    const en_gchord_t *q = &g[(c + k) % (EN_MEL_CHORDS + 12)];
                    in.chord[k].tones = (uint16_t)abs_tones(&q->ch);
                    in.chord[k].root = (uint8_t)((q->ch.tonic + q->ch.root) % 12);
                    in.chord[k].steps = (uint16_t)(q->bars * 16u);
                    total += in.chord[k].steps;
                }
                c = (c + 1) % (EN_MEL_CHORDS + 12);
                ops0 = en_mel_ops;
                {
                    /* pure: from a copy of the state, the same phrase and the same state */
                    en_mel_t again = st;
                    en_phrase_t q;
                    en_mel_next(&again, &in, &q);
                    en_mel_ops = ops0;
                    en_mel_next(&st, &in, &p);
                    CHECK(!memcmp(&p, &q, sizeof p) && !memcmp(&again, &st, sizeof st), "pool %03x key %d: not a pure function", mask, key);
                }
                ops_most = en_mel_ops - ops0 > ops_most ? en_mel_ops - ops0 : ops_most;
                if (!p.n)
                    continue;                    /* (no room: nothing to judge) */
                phrases++;
                snprintf(w, sizeof w, "pool %03x key %d", mask, key);
                phrase_shape(&p, w);
                for (i = 0; i < p.n; i++) {
                    const en_mnote_t *o = &p.note[i];
                    const int t = o->step;
                    int e = 0, ci;
                    uint32_t chord = 0u;
                    notes++;
                    CHECK(o->note >= lo && o->note <= hi, "%s: note %d outside %d..%d", w, o->note, lo, hi);
                    for (ci = 0; ci < in.nchords; ci++) {
                        e += in.chord[ci].steps;
                        if (t < e)
                            break;
                    }
                    CHECK(ci < in.nchords, "%s: a note at %d past the chords known (%d)", w, t, total);
                    if (ci >= in.nchords)
                        continue;
                    chord = in.chord[ci].tones;
                    CHECK(pool_abs >> (o->note % 12) & 1u || chord >> (o->note % 12) & 1u, "%s: note %d neither in the pool nor in its chord", w, o->note);
                    if (!(t & 7) || (t & ~7) + 8 < t + (int)o->len || o->len >= 6 || i == p.n - 1)
                        CHECK(chord >> (o->note % 12) & 1u, "%s: a note at %d for %d, not of its chord %03x: %d", w, t, o->len, chord, o->note);
                    if (t + (int)o->len > e && ci + 1 < in.nchords)
                        CHECK(in.chord[ci + 1].tones >> (o->note % 12) & 1u, "%s: note %d held over a change, not in the next chord", w, o->note);
                    /* its echoes */
                    if (o->echo) {
                        int rep;
                        for (rep = 1; rep <= 2; rep++) {
                            const int at = t + rep * in.echo_steps;
                            int e2 = 0, c2;
                            for (c2 = 0; c2 < in.nchords; c2++) {
                                e2 += in.chord[c2].steps;
                                if (at < e2)
                                    break;
                            }
                            if (c2 < in.nchords)
                                CHECK(in.chord[c2].tones >> (o->note % 12) & 1u, "%s: echo %d of note %d lands out of its chord", w, rep, o->note);
                        }
                    }
                }
            }
        }
    }
    printf("  every pool and root: %u phrases, %u notes; most work in one phrase %u\n", phrases, notes, ops_most);
    CHECK(phrases > 50000u, "only %u phrases", phrases);
    /* The bound: a phrase's work is fixed loops, whatever the pool: about 400 steps at the most, measured, each a few instructions */
    CHECK(ops_most < 1000u, "the generator's work: %u", ops_most);
}

/* the cost in time, on this machine, against the block */
static void t_cost(void)
{
    en_gen_t gs;
    en_mel_t st;
    en_gchord_t g;
    en_mel_in_t in;
    en_phrase_t p;
    struct timespec a, b;
    double worst = 0.0, sum = 0.0;
    int i;
    en_gen_start(&gs);
    en_mel_start(&st);
    in.pool = EN_MODE_POOL[MODE_LYDIAN];
    in.seed = 7u;
    in.key = 2;
    in.section = EN_SEC_FULL;
    in.evolve = 100;                             /* the most work: a new motif every time, against all it has seen */
    in.rate = 50;
    in.tone = 50;
    in.echo_steps = EN_ECHO_STEPS;
    in.nchords = EN_MEL_CHORDS;
    for (i = 0; i < 4000; i++) {
        int k;
        double t;
        for (k = 0; k < EN_MEL_CHORDS; k++) {
            en_gen_next(&gs, in.pool, in.key, in.seed, 50, 1, EN_MASK_ALL, &g);
            in.chord[k].tones = (uint16_t)abs_tones(&g.ch);
            in.chord[k].root = (uint8_t)((g.ch.tonic + g.ch.root) % 12);
            in.chord[k].steps = (uint16_t)(g.bars * 16u);
        }
        clock_gettime(CLOCK_MONOTONIC, &a);
        en_mel_next(&st, &in, &p);
        clock_gettime(CLOCK_MONOTONIC, &b);
        t = (double)(b.tv_sec - a.tv_sec) * 1e6 + (double)(b.tv_nsec - a.tv_nsec) * 1e-3;
        sum += t;
        worst = t > worst ? t : worst;
    }
    printf("  one phrase here: %.2f us on average, %.1f us at worst; the block is 5800 us\n", sum / 4000.0, worst);
    /* (a loose bound, for a loaded machine: the deterministic one is the count above) */
    CHECK(sum / 4000.0 < 100.0, "a phrase takes %.1f us", sum / 4000.0);
}

/* ------------------------------------------------------------------ what makes it a melody, in the engine */
typedef struct {
    uint32_t notes, phrases, steps1, steps2, leaps, leaps_back, unique_top, top_first, end_stable, ends_s, open_a, ends_a;
    uint32_t same, chord_strong, strong, held_over, held_ok, longs, pick, rests, phrase_steps, silent_steps;
} melstat_t;

static int scale_dist(uint32_t pool_abs, int a, int b)
{
    int lo = a < b ? a : b, hi = a < b ? b : a, n = 0, m;
    for (m = lo + 1; m <= hi; m++)
        n += pool_abs >> (m % 12) & 1u;
    return n;
}

static void melody_stats(uint32_t pool, int root, melstat_t *s, const char *w)
{
    const uint32_t pool_abs = (pool << root | pool >> (12 - root)) & 0xFFFu;
    int i, j;
    for (i = 0; i < nph; i++) {
        const en_phrase_t *p = &ph[i].p;
        const uint32_t at = ph[i].at;
        int top = 0, ntop = 0, topi = 0;
        phrase_shape(p, w);
        s->phrases++;
        if (i + 1 < nph)
            CHECK(ph[i + 1].at + ph[i + 1].p.note[0].step >= at + p->end, "%s: the phrase at %u runs to %u, the next starts at %u", w, at, at + p->end, ph[i + 1].at + ph[i + 1].p.note[0].step);
        for (j = 0; j < p->n; j++) {
            const en_mnote_t *o = &p->note[j];
            const ch_t *c = chord_at(at + o->step);
            const uint32_t t = at + o->step;
            CHECK(c != 0, "%s: no chord", w);
            if (!c)
                continue;
            s->notes++;
            top = o->note > top ? o->note : top;
            if (!(t & 7u)) {
                s->strong++;
                s->chord_strong += abs_tones(&c->g.ch) >> (o->note % 12) & 1u;
            }
            CHECK(pool_abs >> (o->note % 12) & 1u || c->g.ch.borrowed, "%s: note %d out of the pool, step %u", w, o->note, t);
            if (o->len >= 8u)
                s->longs++;
            if (j) {
                const int d = scale_dist(pool_abs, p->note[j - 1].note, o->note);
                s->same += d == 0;
                s->steps1 += d <= 1;
                s->steps2 += d <= 2;
                if (abs((int)o->note - (int)p->note[j - 1].note) >= 7) {
                    s->leaps++;
                    if (j + 1 < p->n) {
                        const int up = o->note > p->note[j - 1].note;
                        const int nx = (int)p->note[j + 1].note - (int)o->note;
                        s->leaps_back += up ? nx <= 0 : nx >= 0;
                    } else {
                        s->leaps_back++;
                    }
                }
            }
            {
                /* held over a chord change: both chords have it */
                const ch_t *c2 = chord_at(t + o->len - 1u);
                if (c2 && c2 != c) {
                    s->held_over++;
                    s->held_ok += abs_tones(&c2->g.ch) >> (o->note % 12) & 1u;
                    CHECK(abs_tones(&c2->g.ch) >> (o->note % 12) & 1u, "%s: note %d held from %u over the change at %u: not in the next chord", w, o->note, t, c2->step);
                }
            }
            /* the echoes where the engine's chords were as foreseen */
            if (o->echo) {
                int r;
                CHECK(abs_tones(&c->g.ch) >> (o->note % 12) & 1u, "%s: a note out of its chord in the echo", w);
                for (r = 1; r <= 2; r++) {
                    const ch_t *e = chord_at(t + (uint32_t)r * EN_ECHO_STEPS);
                    if (e && e->step <= t + (uint32_t)r * EN_ECHO_STEPS && e < &chs[nch - 1])
                        CHECK(abs_tones(&e->g.ch) >> (o->note % 12) & 1u, "%s: echo %d of %d at %u lands off the chord there", w, r, o->note, t);
                }
            }
        }
        for (j = 0; j < p->n; j++)
            if (p->note[j].note == top) {
                ntop++;
                topi = j;
            }
        s->unique_top += ntop == 1;
        s->top_first += ntop == 1 && topi == 0;
        {
            const en_mnote_t *l = &p->note[p->n - 1];
            const ch_t *c = chord_at(at + l->step);
            if (c) {
                const int r = (c->g.ch.tonic + c->g.ch.root) % 12, pc = l->note % 12, iv = (pc - r + 12) % 12;
                if (p->role == EN_MEL_ANSWER) {
                    s->ends_a++;
                    s->open_a += iv != 0;
                } else {
                    s->ends_s++;
                    s->end_stable += iv == 0 || iv == 3 || iv == 4 || iv == 7;
                }
            }
        }
    }
}

static void stat_checks(const melstat_t *s, const char *w)
{
    const double intervals = (double)(s->notes - s->phrases);
    printf("  %-9s %u phrases, %.2f notes each; strong beats on the chord %.1f%%; intervals: a step %.0f%%, a third or less %.0f%%; %u leaps, turned back %.0f%%; the same note again %.0f%%; one high point %.0f%% (first note %.0f%%); ends stable %.0f%%, answers open %.0f%%; held over a change %u; long notes %.0f%%\n",
           w, s->phrases, (double)s->notes / s->phrases, 100.0 * s->chord_strong / (s->strong ? s->strong : 1u), 100.0 * s->steps1 / intervals, 100.0 * s->steps2 / intervals, s->leaps, 100.0 * s->leaps_back / (s->leaps ? s->leaps : 1u), 100.0 * s->same / intervals,
           100.0 * s->unique_top / s->phrases, 100.0 * s->top_first / s->phrases, 100.0 * s->end_stable / (s->ends_s ? s->ends_s : 1u), 100.0 * s->open_a / (s->ends_a ? s->ends_a : 1u), s->held_over, 100.0 * s->longs / s->notes);
    CHECK(s->chord_strong == s->strong, "%s: %u of %u strong beats on the chord", w, s->chord_strong, s->strong);
    /* mostly stepwise: chord tones on the strong beats make thirds of many of the steps, so both are asked */
    CHECK((double)s->steps1 / intervals > 0.40, "%s: steps %.2f", w, (double)s->steps1 / intervals);
    CHECK((double)s->steps2 / intervals > 0.80, "%s: a third or less %.2f", w, (double)s->steps2 / intervals);
    CHECK((double)s->same / intervals < 0.10, "%s: the same note again in %.0f%% of the intervals: drones", w, 100.0 * s->same / intervals);
    CHECK(s->leaps_back * 100u >= s->leaps * 90u, "%s: %u of %u leaps turned back", w, s->leaps_back, s->leaps);
    CHECK(s->unique_top * 100u >= s->phrases * 85u, "%s: one high point in %u of %u", w, s->unique_top, s->phrases);
    CHECK(s->top_first * 100u <= s->phrases * 45u, "%s: the high point first in %u of %u", w, s->top_first, s->phrases);
    CHECK(s->end_stable * 100u >= s->ends_s * 90u, "%s: %u of %u ends stable", w, s->end_stable, s->ends_s);
    CHECK(s->open_a * 100u >= s->ends_a * 70u, "%s: %u of %u answers open", w, s->open_a, s->ends_a);
    CHECK(s->held_over > 0u, "%s: never over a chord change", w);
    CHECK(s->held_ok == s->held_over, "%s: %u of %u held notes in both chords", w, s->held_ok, s->held_over);
}

static uint32_t rhythm_of(const en_phrase_t *p)
{
    uint32_t h = 2166136261u;
    int j;
    for (j = 0; j < p->n; j++)
        h = (h ^ (uint32_t)(p->note[j].step - p->note[0].step)) * 16777619u;
    return h;
}
static uint32_t rhythm_len_of(const en_phrase_t *p)   /* with its lengths */
{
    uint32_t h = rhythm_of(p);
    int j;
    for (j = 0; j < p->n; j++)
        h = (h ^ p->note[j].len) * 16777619u;
    return h;
}
static uint32_t contour_of(const en_phrase_t *p)   /* the ups and downs */
{
    uint32_t h = 2166136261u;
    int j;
    for (j = 1; j < p->n; j++) {
        const int d = (int)p->note[j].note - (int)p->note[j - 1].note;
        h = (h ^ (uint32_t)(d > 0 ? 1 : d < 0 ? 2 : 3)) * 16777619u;
    }
    return h;
}
static uint32_t core_of(const en_phrase_t *p)      /* the ups and downs of the motif: without the note it comes to rest on, which is the chord's */
{
    uint32_t h = 2166136261u;
    int j;
    for (j = 1; j < p->n - 1; j++) {
        const int d = (int)p->note[j].note - (int)p->note[j - 1].note;
        h = (h ^ (uint32_t)(d > 0 ? 1 : d < 0 ? 2 : 3)) * 16777619u;
    }
    return h;
}
static uint32_t phrase_of(const en_phrase_t *p)    /* the notes themselves */
{
    uint32_t h = rhythm_of(p);
    int j;
    for (j = 0; j < p->n; j++)
        h = (h ^ p->note[j].note) * 16777619u;
    return h;
}
static int distinct(const uint32_t *v, int n)
{
    static uint32_t s[NPH];
    int i, j, d = 0;
    for (i = 0; i < n; i++) {
        for (j = 0; j < d && s[j] != v[i]; j++)
            ;
        if (j == d)
            s[d++] = v[i];
    }
    return d;
}

/* one hour, in each mode: the shape of it all, how varied it is, how much of it there is */
static void t_hour(void)
{
    static const struct {
        int mode, root;
    } M[] = {{MODE_IONIAN, 2}, {MODE_LYDIAN, 2}, {MODE_AEOLIAN, 9}, {MODE_DORIAN, 4}, {MODE_PENT_MIN, 7}, {MODE_PHRYGIAN, 11}};
    unsigned m;
    for (m = 0u; m < sizeof M / sizeof M[0]; m++) {
        static uint32_t r[NPH], c[NPH], f[NPH], sg[NPH];
        const uint32_t pool = EN_MODE_POOL[M[m].mode];
        melstat_t s;
        int i, tb[EN_NTIMBRES] = {0}, sec_ph[EN_NSECTIONS] = {0}, runs = 0, run_len = 0, runs_sum = 0, ndist_tb = 0;
        char w[32];
        memset(&s, 0, sizeof s);
        engine(pool, M[m].root, 5000u + m, EN_ST_LIQUID);
        play_bars(bars_of(60.0));
        snprintf(w, sizeof w, "%s", EN_MODE_NAME[M[m].mode]);
        melody_stats(pool, M[m].root, &s, w);
        stat_checks(&s, w);
        for (i = 0; i < nph; i++) {
            r[i] = rhythm_of(&ph[i].p);
            c[i] = contour_of(&ph[i].p);
            f[i] = phrase_of(&ph[i].p);
            sg[i] = ph[i].p.sig;
            sec_ph[ph[i].section]++;
            tb[ph[i].p.timbre]++;
            if (i && ph[i].p.timbre != ph[i - 1].p.timbre) {
                runs++;
                runs_sum += run_len;
                run_len = 0;
            }
            run_len++;
        }
        for (i = 0; i < EN_NTIMBRES; i++)
            ndist_tb += tb[i] > 0;
        printf("    an hour: %d phrases: %d different, %d rhythms, %d contours, %d motifs; timbres %d (%d %d %d %d %d %d), each %.1f phrases; a minute of each section:", nph, distinct(f, nph), distinct(r, nph), distinct(c, nph), distinct(sg, nph), ndist_tb, tb[0], tb[1], tb[2], tb[3], tb[4], tb[5], runs ? (double)runs_sum / runs : 0.0);
        for (i = 0; i < EN_NSECTIONS; i++) {
            const double mins = (double)bars_in_section[i] * 16.0 / 680.0;
            printf(" %s %.1f", EN_SECTION_NAME[i], mins > 0.0 ? sec_ph[i] / mins : 0.0);
        }
        printf("\n");
        /* An hour heard, the thresholds. Two hundred phrases or so an hour. A period's statement and return differ only as the chords
         * under them do, and motifs return on purpose, so not every phrase is new: most should be. Rhythm is what is kept most, so the
         * fewest of those; still, a piece with a dozen rhythms in an hour would be one tune */
        CHECK(nph >= 120 && nph <= 500, "%s: %d phrases in an hour", w, nph);
        CHECK(distinct(f, nph) * 10 >= nph * 7, "%s: %d different phrases of %d", w, distinct(f, nph), nph);
        CHECK(distinct(r, nph) >= 25, "%s: %d rhythms in an hour", w, distinct(r, nph));
        CHECK(distinct(c, nph) >= 25, "%s: %d contours in an hour", w, distinct(c, nph));
        CHECK(distinct(sg, nph) >= 12, "%s: %d motifs in an hour", w, distinct(sg, nph));
        CHECK(ndist_tb == EN_NTIMBRES, "%s: %d timbres in an hour", w, ndist_tb);
        CHECK(runs > 8 && runs_sum >= 2 * runs, "%s: a timbre lasts %d phrases over %d changes: it is to last several", w, runs_sum, runs);
        {
            /* more of it in a full section than a calm one; low in the calm ones, higher in the full */
            const double mins_full = (double)bars_in_section[EN_SEC_FULL] * 16.0 / 680.0, mins_intro = (double)bars_in_section[EN_SEC_INTRO] * 16.0 / 680.0, mins_down = (double)bars_in_section[EN_SEC_DOWN] * 16.0 / 680.0;
            double avg[EN_NSECTIONS] = {0}, cnt[EN_NSECTIONS] = {0};
            int j;
            CHECK(sec_ph[EN_SEC_FULL] / mins_full > 2.0 * sec_ph[EN_SEC_INTRO] / mins_intro, "%s: full %.1f a minute, intro %.1f", w, sec_ph[EN_SEC_FULL] / mins_full, sec_ph[EN_SEC_INTRO] / mins_intro);
            CHECK(sec_ph[EN_SEC_FULL] / mins_full > 2.0 * sec_ph[EN_SEC_DOWN] / mins_down, "%s: full %.1f a minute, breakdown %.1f", w, sec_ph[EN_SEC_FULL] / mins_full, sec_ph[EN_SEC_DOWN] / mins_down);
            for (i = 0; i < nph; i++)
                for (j = 0; j < ph[i].p.n; j++) {
                    avg[ph[i].section] += ph[i].p.note[j].note;
                    cnt[ph[i].section] += 1.0;
                }
            CHECK(avg[EN_SEC_FULL] / cnt[EN_SEC_FULL] > avg[EN_SEC_INTRO] / cnt[EN_SEC_INTRO] + 1.0, "%s: full's mean note %.1f, intro's %.1f", w, avg[EN_SEC_FULL] / cnt[EN_SEC_FULL], avg[EN_SEC_INTRO] / cnt[EN_SEC_INTRO]);
        }
        {
            /* density: phrases with room around them; notes a bar when it plays, and how much of the time it is silent */
            uint32_t sound = 0u, gaps_long = 0u;
            for (i = 0; i < nph; i++) {
                sound += ph[i].p.end - ph[i].p.note[0].step;
                CHECK(ph[i].p.n * 16 <= 4 * (ph[i].p.end - ph[i].p.note[0].step), "%s: %d notes in %d steps", w, ph[i].p.n, ph[i].p.end - ph[i].p.note[0].step);
                if (i + 1 < nph && ph[i + 1].at - ph[i].at - ph[i].p.end >= 16u * 16u)
                    gaps_long++;
            }
            printf("    sounding %.0f%% of the hour; %u silences of 16 bars or more\n", 100.0 * sound / (bars_of(60.0) * 16.0), gaps_long);
            CHECK(sound < (uint32_t)bars_of(60.0) * 16u * 7u / 10u, "%s: it sounds %u of the steps: it has to breathe", w, sound);
            CHECK(sound > (uint32_t)bars_of(60.0) * 16u / 8u, "%s: it sounds only %u of the steps", w, sound);
            CHECK(gaps_long >= 20u, "%s: %u long silences", w, gaps_long);
        }
    }
    engine_off();
}

/* a recognised return: statement and return have the rhythm, and go up and down together. Evolve: 0 one motif, 100 none again, the default both */
static void t_memory(void)
{
    uint32_t sd;
    for (sd = 0u; sd < 3u; sd++) {
        const uint32_t pool = EN_MODE_POOL[sd == 0u ? MODE_LYDIAN : sd == 1u ? MODE_AEOLIAN : MODE_IONIAN];
        static uint32_t v[NPH];
        int i, j;
        /* Evolve 0: one motif for good, heard from the rhythm itself and not only from what the generator says */
        engine(pool, 2, 70u + sd, EN_ST_LIQUID);
        set_now(P_EVOLVE, 0);
        play_bars(bars_of(30.0));
        {
            int same = 0, st = 0, ctr = 0;
            uint32_t first = 0u, first_c = 0u;
            for (i = 0; i < nph; i++) {
                v[i] = ph[i].p.sig;
                if (ph[i].p.role == EN_MEL_STATEMENT || ph[i].p.role == EN_MEL_RETURN) {
                    if (!st) {
                        first = rhythm_of(&ph[i].p);
                        first_c = core_of(&ph[i].p);
                    }
                    st++;
                    same += rhythm_of(&ph[i].p) == first;
                    ctr += core_of(&ph[i].p) == first_c;
                }
            }
            printf("  Evolve 0: %d phrases, %d motif; statements and returns %d, the first's rhythm %d, its contour %d\n", nph, distinct(v, nph), st, same, ctr);
            CHECK(nph > 40, "Evolve 0: %d phrases", nph);
            CHECK(distinct(v, nph) == 1, "Evolve 0: %d motifs", distinct(v, nph));
            CHECK(same * 100 >= st * 95, "Evolve 0: %d of %d statements have the rhythm", same, st);
            CHECK(ctr * 100 >= st * 50, "Evolve 0: %d of %d statements have the contour", ctr, st);
        }
        /* Evolve 100: never again, within what it can remember, by the notes as well as by what it says */
        engine(pool, 2, 80u + sd, EN_ST_LIQUID);
        set_now(P_EVOLVE, 100);
        play_bars(bars_of(60.0));
        {
            int dup = 0, dup_rc = 0;
            for (i = 0; i < nph; i++)
                for (j = i + 1; j < nph && j <= i + EN_MEL_SEEN; j++) {
                    dup += ph[i].p.sig == ph[j].p.sig;
                    dup_rc += rhythm_len_of(&ph[i].p) == rhythm_len_of(&ph[j].p) && contour_of(&ph[i].p) == contour_of(&ph[j].p);
                }
            for (i = 0; i < nph; i++)
                v[i] = ph[i].p.sig;
            printf("  Evolve 100: %d phrases, %d motifs; the same motif within %d: %d; the same rhythm and contour: %d\n", nph, distinct(v, nph), EN_MEL_SEEN, dup, dup_rc);
            CHECK(dup == 0, "Evolve 100: %d repeats", dup);
            CHECK(dup_rc * 100 <= nph * 3, "Evolve 100: %d phrases with a rhythm and contour already heard", dup_rc);
            CHECK(distinct(v, nph) * 100 >= nph * 90, "Evolve 100: %d motifs in %d phrases", distinct(v, nph), nph);
        }
        /* the default: a period's motif often is an old one, and the gap to its last hearing is minutes */
        engine(pool, 2, 90u + sd, EN_ST_LIQUID);
        play_bars(bars_of(60.0));
        {
            int periods = 0, returns = 0, far = 0, new_m = 0, statement_return = 0, st_ret_ok = 0, st_ret_c = 0;
            double gap_sum = 0.0;
            for (i = 0; i < nph; i++) {
                if (ph[i].p.role != EN_MEL_STATEMENT)
                    continue;
                periods++;
                if (ph[i].p.recalled) {
                    double gap = -1.0;
                    for (j = i - 1; j >= 0 && gap < 0.0; j--)
                        if (ph[j].p.sig == ph[i].p.sig)
                            gap = (double)(ph[i].at - ph[j].at) / 680.0;
                    returns++;
                    if (gap >= 0.0) {
                        gap_sum += gap;
                        far += gap >= 1.0;
                    }
                } else {
                    new_m++;
                }
                /* its return in this period: the same rhythm, the same ups and downs */
                for (j = i + 1; j < nph && ph[j].p.role != EN_MEL_STATEMENT; j++)
                    if (ph[j].p.role == EN_MEL_RETURN) {
                        statement_return++;
                        st_ret_ok += rhythm_of(&ph[j].p) == rhythm_of(&ph[i].p);
                        st_ret_c += core_of(&ph[j].p) == core_of(&ph[i].p);
                    }
            }
            printf("  Evolve 50: %d phrases, %d periods: new motifs %d, returning %d, %d of those after a minute or more, on average after %.1f min; a return has the statement's rhythm %d of %d, its contour %d\n", nph, periods, new_m, returns, far, returns ? gap_sum / returns : 0.0, st_ret_ok, statement_return, st_ret_c);
            CHECK(returns * 100 >= periods * 20 && returns * 100 <= periods * 60, "Evolve 50: %d returns in %d periods", returns, periods);
            CHECK(far * 2 >= returns, "Evolve 50: %d of %d returns from far enough to be a return", far, returns);
            CHECK(new_m >= 8, "Evolve 50: %d new motifs", new_m);
            CHECK(st_ret_ok * 100 >= statement_return * 90, "%d of %d returns with their rhythm", st_ret_ok, statement_return);
            CHECK(st_ret_c * 100 >= statement_return * 50, "%d of %d returns with their contour", st_ret_c, statement_return);
        }
    }
    /* a new seed, a new piece: its own motif, Evolve 0 or not */
    {
        static int32_t b[512];
        uint32_t first;
        engine(EN_MODE_POOL[MODE_LYDIAN], 2, 123u, EN_ST_LIQUID);
        set_now(P_EVOLVE, 0);
        play_bars(200);
        CHECK(nph > 0, "no phrases");
        first = ph[0].p.sig;
        en_seed(321u);
        en_render(b, 16, 1024);
        nph = 0;
        play_bars(200);
        CHECK(nph > 0 && ph[nph - 1].p.sig != first, "a new piece with the old one's motif");
    }
    engine_off();
}

/* ------------------------------------------------------------------ the knobs */
static uint32_t phrases_at(int p, int v, uint32_t seed, uint32_t *notes)
{
    int i;
    engine(EN_MODE_POOL[MODE_LYDIAN], 2, seed, EN_ST_LIQUID);
    set_now(p, v);
    play_bars(bars_of(30.0));
    *notes = 0u;
    for (i = 0; i < nph; i++)
        *notes += ph[i].p.n;
    return (uint32_t)nph;
}
static void t_knobs(void)
{
    uint32_t n0 = 0u, n50 = 0u, n100 = 0u, a, sd;
    for (sd = 0u; sd < 3u; sd++) {
        phrases_at(P_MRATE, 0, 100u + sd, &a);
        n0 += a;
        phrases_at(P_MRATE, 50, 100u + sd, &a);
        n50 += a;
        phrases_at(P_MRATE, 100, 100u + sd, &a);
        n100 += a;
    }
    printf("  Rate 0, 50, 100: %u, %u, %u notes in 90 minutes\n", n0, n50, n100);
    CHECK(n0 > 50u && n0 * 2u < n50, "Rate 0: %u against %u", n0, n50);
    CHECK(n100 * 10u > n50 * 14u, "Rate 100: %u against %u", n100, n50);
    /* Tone: the softest voices at one end, the brightest at the other, all of them between */
    {
        static const struct {
            int tone, lo, hi;
        } T[] = {{0, EN_TB_FLUTE, EN_TB_VIBES}, {50, EN_TB_FLUTE, EN_TB_GLASS}, {100, EN_TB_MARIMBA, EN_TB_GLASS}};
        unsigned t;
        for (t = 0u; t < 3u; t++) {
            int i, seen = 0;
            phrases_at(P_MTONE, T[t].tone, 300u + t, &a);
            for (i = 0; i < nph; i++) {
                CHECK(ph[i].p.timbre >= T[t].lo && ph[i].p.timbre <= T[t].hi, "Tone %d: timbre %s", T[t].tone, EN_TIMBRE_NAME[ph[i].p.timbre]);
                seen |= 1 << ph[i].p.timbre;
            }
            CHECK(seen == ((1 << (T[t].hi + 1)) - 1) >> T[t].lo << T[t].lo, "Tone %d: timbres %x", T[t].tone, seen);
        }
    }
    /* Octave: the note, an octave up, as MIDI has it; nothing else moved. In the ledger as well: t_ledger */
    {
        static uint8_t base[3000];
        uint32_t nb = 0u, i;
        /* (the generator does not know the knob: same phrases, and the engine adds it) */
        engine(EN_MODE_POOL[MODE_LYDIAN], 2, 400u, EN_ST_LIQUID);
        play_bars(400);
        for (i = 0u; i < (uint32_t)nph; i++) {
            int j;
            for (j = 0; j < ph[i].p.n && nb < 3000u; j++)
                base[nb++] = ph[i].p.note[j].note;
        }
        engine(EN_MODE_POOL[MODE_LYDIAN], 2, 400u, EN_ST_LIQUID);
        set_now(P_MOCT, 1);
        play_bars(400);
        {
            uint32_t k = 0u;
            for (i = 0u; i < (uint32_t)nph; i++) {
                int j;
                for (j = 0; j < ph[i].p.n && k < nb; j++)
                    CHECK(base[k++] == ph[i].p.note[j].note, "Octave moved the phrase itself");
            }
            CHECK(k == nb && nb > 10u, "Octave: %u notes, %u before", k, nb);
        }
    }
    engine_off();
}

/* the engine: chords foreseen as they come; the ledger clean; one key down at a time; voices no more than they were */
static void t_engine(void)
{
    static int32_t b[512];
    int i, most_voices = 0;
    uint32_t r0, e0, flagged = 0u, played = 0u;
    en_test_mel_refits = 0u;
    en_midi_out(0x7Fu, 0u, 0u);
    ledger_reset();
    engine(EN_MODE_POOL[MODE_LYDIAN], 2, 1001u, EN_ST_LIQUID);
    en_set(P_MRATE, 100);
    r0 = en_test_mel_refits;
    e0 = en_test_bell_echoes;
    for (i = 0; i < (int)(44100.0f * 600.0f / 256.0f); i++) {
        en_render(b, 256, 1024);
        CHECK(mel_down == en_test_bells_on(), "MIDI says %d melody notes down, the engine %d", mel_down, en_test_bells_on());
        CHECK(en_test_bells_on() <= 1, "%d melody keys down at once", en_test_bells_on());
        if (nph && en_bars > ph[nph - 1].at + ph[nph - 1].p.end + 1u)
            CHECK(mel_down == 0, "a melody key down past its phrase's end, step %u", en_bars);
        most_voices = en_test_bell_voices() > most_voices ? en_test_bell_voices() : most_voices;
    }
    printf("  10 min, Rate 100: %u melody notes; at most %d key down, %d voices; a voice taken at level %.3f at the loudest; %u off a chord foreseen\n", mel_ons, mel_most, most_voices, (double)en_test_bell_stolen(), en_test_mel_refits - r0);
    CHECK(mel_ons > 100u, "%u melody notes", mel_ons);
    /* the notes that go to the echo are those the phrase says may: the sound follows the flag. (The last phrase may be under way) */
    for (i = 0; i < nph; i++) {
        int j;
        for (j = 0; j < ph[i].p.n; j++) {
            played++;
            flagged += ph[i].p.note[j].echo;
        }
    }
    printf("  of %u notes planned, %u for the echo; %u went to it\n", played, flagged, en_test_bell_echoes - e0);
    CHECK(flagged > 0u && flagged < played, "%u of %u notes for the echo: some are passing tones", flagged, played);
    CHECK(en_test_bell_echoes - e0 <= flagged && en_test_bell_echoes - e0 + 7u >= flagged, "%u went to the echo, %u should", en_test_bell_echoes - e0, flagged);
    CHECK(mel_bad_off == 0, "%d note-offs for notes not down", mel_bad_off);
    /* no more than the old bells had */
    CHECK(en_test_bell_limit() == 4 && most_voices <= 4, "%d voices of %d", most_voices, en_test_bell_limit());
    CHECK(en_test_mel_refits == r0, "the chords were not as foreseen %u times, with nothing changed", en_test_mel_refits - r0);
    /* a rest: no new notes, the key lets go; growing again it goes on; panic leaves nothing down */
    en_play(0);
    for (i = 0; i < 1500; i++)
        en_render(b, 256, 1024);
    {
        const uint32_t m0 = mel_ons;
        for (i = 0; i < 300; i++)
            en_render(b, 256, 1024);
        CHECK(mel_ons == m0, "melody notes in a rest");
        CHECK(mel_down == 0, "a melody note left down in a rest");
    }
    en_play(1);
    {
        const uint32_t m0 = mel_ons;
        for (i = 0; i < 44100 * 120 / 256; i++)
            en_render(b, 256, 1024);
        CHECK(mel_ons > m0, "no melody after the rest");
    }
    en_panic();
    en_render(b, 256, 1024);
    CHECK(mel_down == 0 && en_test_bells_on() == 0 && en_test_bell_voices() == 0, "panic: %d down, %d voices", mel_down, en_test_bell_voices());
    en_restore();
    engine_off();
}

/* a key or a knob moved while it plays: it moves with the pool at once, the chord it finds is obeyed */
static void t_live(void)
{
    static int32_t b[512];
    int i;
    uint32_t r0;
    engine(EN_MODE_POOL[MODE_LYDIAN], 2, 2002u, EN_ST_LIQUID);
    en_set(P_MRATE, 100);
    r0 = en_test_mel_refits;
    play_bars(100);
    while (nph == 0)
        en_test_ticks(16u);
    /* the Evolve knob moved between planning and the chord: the chords change under it, so the phrase is moved */
    for (i = 0; i < 40; i++) {
        en_set(P_EVOLVE, i & 1 ? 100 : 0);
        en_render(b, 16, 1024);
        en_test_ticks(32u);
    }
    printf("  the knob moved under it: %u phrases moved\n", en_test_mel_refits - r0);
    CHECK(en_test_mel_refits > r0, "a chord not foreseen, and nothing was moved");
    engine_off();
}

/* ------------------------------------------------------------------ the voices, measured */
static double goertzel(const float *x, int n, double f)
{
    const double w = 2.0 * M_PI * f / 44100.0, c = 2.0 * cos(w);
    double a = 0.0, b = 0.0;
    int i;
    for (i = 0; i < n; i++) {
        const double win = 0.5 - 0.5 * cos(2.0 * M_PI * i / (n - 1)), y = (double)x[i] * win + c * a - b;
        b = a;
        a = y;
    }
    return sqrt(a * a + b * b - c * a * b) / ((double)n * 0.25);
}
static double db(double a) { return a > 1e-12 ? 20.0 * log10(a) : -240.0; }

static void t_voices(void)
{
    static const int NOTE[2] = {74, 86};
    static int32_t buf[2 * 256];
    static float x[44100];
    int tb, nt;
    double centroid[EN_NTIMBRES];
    printf("  timbres, note 74 (D5), Tone 50: partials 1..8 in dB against the first, at 20 ms and 400 ms; worst between the partials; ratio; t to -20 dB:\n");
    for (tb = 0; tb < EN_NTIMBRES; tb++) {
        en_mel_voice_t v;
        en_mel_voice(tb, 50, &v);
        CHECK(v.ratio == 1.0f || v.ratio == 2.0f || v.ratio == 3.0f, "%s: ratio %f", EN_TIMBRE_NAME[tb], (double)v.ratio);
        CHECK(v.index + v.floor_idx < 1.7f, "%s: index %f", EN_TIMBRE_NAME[tb], (double)(v.index + v.floor_idx));
        for (nt = 0; nt < 2; nt++) {
            const double f = 440.0 * pow(2.0, (NOTE[nt] - 69) / 12.0);
            double a[2][9], inh = -240.0, e_hi = 0.0, e_all = 0.0;
            int total = 0, i, w, k, t20 = 0;
            double peak = 0.0;
            en_init();
            en_set(P_KEYS, 0);
            en_set(P_BASS, 0);
            en_set(P_DRUMS, 0);
            en_set(P_ECHO, 0);
            en_set(P_REVERB, 0);
            en_render(buf, 16, 4096);
            en_test_bell_note(tb, NOTE[nt], 0.7f);
            while (total < 44100) {
                en_render(buf, 256, 512);        /* (well under the limiter's knee) */
                for (i = 0; i < 256 && total < 44100; i++)
                    x[total++] = (float)(buf[2 * i] + buf[2 * i + 1]) / 16600000.0f;
            }
            for (w = 0; w < 2; w++) {
                const int at = w ? 44100 * 4 / 10 : 44100 * 2 / 100, len = 4096;
                for (k = 1; k <= 8; k++)
                    a[w][k] = goertzel(x + at, len, f * k);
                if (a[w][1] > 1e-9) {
                    for (k = 1; k <= 8; k++) {
                        const double b1 = goertzel(x + at, len, f * (k + 0.5));
                        inh = db(b1 / a[w][1]) > inh ? db(b1 / a[w][1]) : inh;
                    }
                }
            }
            for (i = 0; i < 44100; i += 64) {
                double m = 0.0;
                for (k = 0; k < 64 && i + k < 44100; k++)
                    m = fabs((double)x[i + k]) > m ? fabs((double)x[i + k]) : m;
                if (m > peak)
                    peak = m;
                if (m > peak * 0.1)
                    t20 = i;
            }
            for (k = 1; k <= 8; k++) {
                e_all += a[0][k] * a[0][k];
                if (k >= 5)
                    e_hi += a[0][k] * a[0][k];
            }
            if (nt == 0) {
                double sum = 0.0, wt = 0.0;
                printf("    %-9s %g:1  ", EN_TIMBRE_NAME[tb], (double)v.ratio);
                for (w = 0; w < 2; w++) {
                    for (k = 1; k <= 8; k++)
                        printf(" %4.0f", k == 1 ? 0.0 : db(a[w][k] / a[w][1]));
                    printf(w ? "" : "  /");
                }
                printf("; between %.0f; -20 dB at %.2f s\n", inh, (double)t20 / 44100.0);
                for (k = 1; k <= 8; k++) {
                    sum += k * a[0][k] * a[0][k];
                    wt += a[0][k] * a[0][k];
                }
                centroid[tb] = sum / wt;
            }
            /* no strong inharmonic partial: between the partials only what the window and the envelope leak */
            CHECK(inh < -38.0, "%s note %d: %.0f dB between the partials", EN_TIMBRE_NAME[tb], NOTE[nt], inh);
            /* warm: the fundamental leads; no metallic ringing: nothing above the fourth partial stands up in the attack, and it is gone after it */
            for (k = 2; k <= 8; k++)
                CHECK(a[0][k] < a[0][1] * 1.01, "%s note %d: partial %d %.1f dB over the first", EN_TIMBRE_NAME[tb], NOTE[nt], k, db(a[0][k] / a[0][1]));
            CHECK(e_hi / e_all < 0.05, "%s note %d: %.1f%% of the attack above the fourth partial: it rings", EN_TIMBRE_NAME[tb], NOTE[nt], 100.0 * e_hi / e_all);
            if (a[1][1] > 1e-5)
                for (k = 5; k <= 8; k++)
                    CHECK(a[1][k] < a[1][1] * 0.03, "%s note %d: partial %d at 400 ms %.1f dB: it rings", EN_TIMBRE_NAME[tb], NOTE[nt], k, db(a[1][k] / a[1][1]));
            /* not the hollow, reedy colour: a third partial loud with no second under it */
            CHECK(a[0][3] < 0.35 * a[0][1] || a[0][2] > 0.5 * a[0][3], "%s note %d: hollow: third %.1f dB, second %.1f dB", EN_TIMBRE_NAME[tb], NOTE[nt], db(a[0][3] / a[0][1]), db(a[0][2] / a[0][1]));
            CHECK(t20 > 44100 / 20, "%s: gone at once", EN_TIMBRE_NAME[tb]);
        }
    }
    /* distinct, and softest to brightest: the ends at least */
    printf("    brightness (the partials' centre, at 20 ms):");
    for (tb = 0; tb < EN_NTIMBRES; tb++)
        printf(" %.2f", centroid[tb]);
    printf("\n");
    CHECK(centroid[EN_TB_FLUTE] < centroid[EN_TB_BOX] && centroid[EN_TB_FLUTE] < centroid[EN_TB_GLASS], "the palette is not softest to brightest");
    /* Tone moves the index, not the ratio */
    for (tb = 0; tb < EN_NTIMBRES; tb++) {
        en_mel_voice_t a, b, c;
        en_mel_voice(tb, 0, &a);
        en_mel_voice(tb, 50, &b);
        en_mel_voice(tb, 100, &c);
        CHECK(a.ratio == b.ratio && b.ratio == c.ratio, "%s: Tone moved the ratio", EN_TIMBRE_NAME[tb]);
        CHECK(a.index < b.index && b.index < c.index, "%s: Tone does not open it", EN_TIMBRE_NAME[tb]);
        CHECK(c.index + c.floor_idx < 2.4f, "%s at Tone 100: index %f", EN_TIMBRE_NAME[tb], (double)(c.index + c.floor_idx));
    }
}

/* ------------------------------------------------------------------ the keys out of its way */
static void t_keys(void)
{
    /* the hand: never in the melody's register, on any pool and key; still spread, and it still moves */
    uint32_t mask, over = 0u, chords = 0u, over7 = 0u;
    int key, most = -99;
    for (mask = 1u; mask <= 4095u; mask += 2u) {
        const int pn = __builtin_popcount(mask);
        if (pn < 3)
            continue;
        for (key = 0; key < 12; key++) {
            en_gen_t st;
            en_gchord_t g;
            int i;
            en_gen_start(&st);
            for (i = 0; i < 10; i++) {
                en_gen_next(&st, mask, key, mask + (uint32_t)key, 50, i & 3, EN_MASK_ALL, &g);
                chords++;
                if (g.keys_v[3] >= EN_MEL_LO(key)) {
                    over++;
                    over7 += pn >= 7;
                    most = (int)g.keys_v[3] - EN_MEL_LO(key) > most ? (int)g.keys_v[3] - EN_MEL_LO(key) : most;
                }
            }
        }
    }
    printf("  the hand's top in the melody's register: %u of %u chords, %u in pools of seven notes or more; at most %d over its floor\n", over, chords, over7, most);
    /* Only where a chord of two or three tones in a sparse pool needs four notes, a few semitones over: four voices have to go somewhere */
    CHECK(over7 == 0u, "the hand in the melody's register: %u chords in full pools", over7);
    CHECK(over * 50u <= chords, "the hand in the melody's register: %u of %u chords", over, chords);
    CHECK(most <= 3, "the hand %d over the melody's floor", most);
    /* in the engine, the lone notes too: while a phrase is out, nothing of the keys over its lowest note */
    {
        static const int MODE[3] = {MODE_LYDIAN, MODE_IONIAN, MODE_AEOLIAN};
        int m;
        for (m = 0; m < 3; m++) {
            static int32_t b[512];
            uint32_t on_ph = 0u, phrases, singles0, frames = 0u;
            engine(EN_MODE_POOL[MODE[m]], 2, 600u + (uint32_t)m, EN_ST_LIQUID);
            en_set(P_MRATE, 100);
            ledger_reset();
            singles0 = en_ep_singles;
            key_watch = EN_MEL_LO(2);
            while (frames < 44100u * 240u) {
                uint32_t k0 = key_high;
                const uint32_t sb = en_bars;
                en_render(b, 256, 1024);
                frames += 256u;
                /* during a phrase */
                if (key_high != k0 && nph) {
                    const ph_t *p = &ph[nph - 1];
                    if (sb >= p->at + p->p.note[0].step && sb < p->at + p->p.end)
                        on_ph++;
                }
            }
            phrases = (uint32_t)nph;
            key_watch = 0;
            printf("  %s: %u phrases; keys notes over the melody's floor: %u, during a phrase %u; lone notes %u, chord notes %u\n", EN_MODE_NAME[MODE[m]], phrases, key_high, on_ph, en_ep_singles - singles0, key_ons);
            CHECK(on_ph == 0u, "%s: %u keys notes in the melody's register during a phrase", EN_MODE_NAME[MODE[m]], on_ph);
            CHECK(phrases > 10u, "%u phrases", phrases);
        }
    }
    /* the keys keep flowing with the melody on: lone notes a chord, against the melody at Rate 0 */
    {
        static int32_t b[512];
        uint32_t n[2], c[2], under[2] = {0u, 0u};
        int r, i;
        for (r = 0; r < 2; r++) {
            uint32_t s0;
            engine(EN_MODE_POOL[MODE_LYDIAN], 2, 700u, EN_ST_LIQUID);
            en_set(P_MRATE, r ? 100 : 0);
            ledger_reset();
            s0 = en_ep_singles;
            for (i = 0; i < 44100 * 300 / 256; i++) {
                const uint32_t s1 = en_ep_singles, sb = en_bars;
                en_render(b, 256, 1024);
                if (nph && sb >= ph[nph - 1].at + ph[nph - 1].p.note[0].step && sb < ph[nph - 1].at + ph[nph - 1].p.end)
                    under[r] += en_ep_singles - s1;
            }
            n[r] = en_ep_singles - s0;
            c[r] = key_ons;
        }
        printf("  the keys in 5 min, Rate 0 and Rate 100: lone notes %u %u (at Rate 100, %u during a phrase), all notes %u %u\n", n[0], n[1], under[1], c[0], c[1]);
        CHECK(under[1] >= 5u, "the keys wait for the melody: %u lone notes during its phrases", under[1]);
        CHECK(n[1] * 10u >= n[0] * 6u, "the keys stop flowing round the melody: lone notes %u against %u", n[1], n[0]);
        CHECK(c[1] * 10u >= c[0] * 8u, "the keys stop flowing round the melody: notes %u against %u", c[1], c[0]);
    }
    engine_off();
}

/* ------------------------------------------------------------------ its level, its room */
static double rms_when(int melody_only, int space, int rate, uint32_t seed, double *peak)
{
    static int32_t b[512];
    double e = 0.0;
    uint32_t n = 0u, frames = 0u;
    int i;
    engine(EN_MODE_POOL[MODE_LYDIAN], 2, seed, EN_ST_LIQUID);
    en_set(P_MRATE, rate);
    en_set(P_BASS, 0);
    en_set(P_DRUMS, 0);
    if (melody_only)
        en_set(P_KEYS, 0);
    else
        en_set(P_MOTIFS, 0);
    if (!space) {
        en_set(P_ECHO, 0);
        en_set(P_REVERB, 0);
    }
    *peak = 0.0;
    /* the same stretch: when the melody is down, or for the keys, the whole of it */
    while (frames < 44100u * 200u) {
        const int on = !melody_only || en_test_bells_on() > 0;
        en_render(b, 256, 512);                  /* (under the limiter's knee) */
        frames += 256u;
        if (on)
            for (i = 0; i < 512; i++) {
                const double x = (double)b[i] / 8300000.0;
                e += x * x;
                n++;
                *peak = fabs(x) > *peak ? fabs(x) : *peak;
            }
    }
    return n ? sqrt(e / n) : 0.0;
}
static void t_level(void)
{
    double pk_m, pk_k, m_dry, k_dry, m_wet;
    m_dry = rms_when(1, 0, 100, 1001u, &pk_m);
    k_dry = rms_when(0, 0, 100, 1001u, &pk_k);
    printf("  dry, while it sounds: melody %.1f dB (peak %.1f), the keys and pad %.1f dB (peak %.1f): %+.1f dB\n", db(m_dry), db(pk_m), db(k_dry), db(pk_k), db(m_dry / k_dry));
    CHECK(db(m_dry / k_dry) > -2.0 && db(m_dry / k_dry) < 5.0, "the melody against the keys: %+.1f dB: on a par or a little forward", db(m_dry / k_dry));
    m_wet = rms_when(1, 1, 100, 1001u, &pk_m);
    {
        const double w2 = m_wet * m_wet - m_dry * m_dry, room = w2 > 0.0 ? sqrt(w2) : 0.0;
        printf("  the melody's room, Echo and Reverb at their defaults: %.1f dB against the dry note\n", db(room / m_dry));
        CHECK(db(room / m_dry) < -7.0, "the room %+.1f dB against the dry note: modest", db(room / m_dry));
    }
    engine_off();
}

/* the styles' numbers: Jungle calmer than it was, no other touched, its chopped character kept */
static void t_styles(void)
{
    static const brk_rhythm_t R[EN_NSTYLES] = {{50, 20, 80, 50, 30}, {80, 30, 110, 50, 80}, {54, 75, 140, 65, 50}, {20, 20, 72, 20, 30}, {10, 10, 32, 10, 0}, {38, 20, 40, 20, 10}};
    int st;
    for (st = 0; st < EN_NSTYLES; st++)
        CHECK(!memcmp(en_style_rhythm(st), &R[st], sizeof R[st]), "%s: its rhythm's numbers", EN_STYLE_NAME[st]);
    CHECK(R[EN_ST_JUNGLE].synco > R[EN_ST_ROLLER].synco && R[EN_ST_JUNGLE].break_rate > R[EN_ST_ROLLER].break_rate, "Jungle no longer the choppiest");
}

int main(void)
{
    t_styles();
    t_all_pools();
    t_cost();
    t_hour();
    t_memory();
    t_knobs();
    t_engine();
    t_live();
    t_voices();
    t_keys();
    t_level();
    printf(fails ? "mel: %d FAILED\n" : "mel: ok\n", fails);
    return fails != 0;
}
