/* SPDX-License-Identifier: MPL-2.0 */
/* The rhythm generator: the genre's fixed points as invariants, over 100 000 bars a style at several energies, as the arranger
 * asks for them and with every level and break forced; groove memory; breaks; what the style numbers do; that the engine plays
 * it. Run with --chart for bars to read. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "amb.h"
#include "brk.h"

static int fails;
#define CHECK(c, ...)                                                                                                  \
    do {                                                                                                               \
        if (!(c)) {                                                                                                    \
            if (fails < 30) {                                                                                          \
                printf("FAIL %s:%d: ", __FILE__, __LINE__);                                                            \
                printf(__VA_ARGS__);                                                                                   \
                printf("\n");                                                                                          \
            }                                                                                                          \
            fails++;                                                                                                   \
        }                                                                                                              \
    } while (0)

void en_midi_out(uint32_t st, uint32_t d1, uint32_t d2)
{
    (void)st;
    (void)d1;
    (void)d2;
}

#define NBARS 100000u
#define DEF_ENERGY 70
#define DEF_BREAKS 50
#define DEF_EVOLVE 50
#define PHRASE 8u
static const int ENERGIES[4] = {10, 35, 70, 100};

/* the limits, written out here on purpose rather than asked of the generator: at any energy. {level 2, 3, 4} */
static const uint8_t MAX_KICKS[EN_NSTYLES][3] = {{2, 3, 3}, {3, 4, 3}, {4, 4, 3}, {3, 3, 2}, {2, 3, 2}, {2, 2, 3}};
static const uint8_t MAX_SNG[EN_NSTYLES][3] = {{4, 4, 3}, {5, 6, 3}, {4, 5, 3}, {3, 3, 3}, {1, 1, 1}, {3, 4, 3}};
/* a break's bar: the most hits, the longest run */
static const uint8_t MAX_BRK[EN_NSTYLES] = {8, 10, 11, 6, 6, 7};
static const uint8_t MAX_RUN[EN_NSTYLES] = {3, 4, 4, 3, 3, 3};

static int is_kick(char c) { return c == 'K' || c == 'k'; }
static int is_snare(char c) { return c == 'S' || c == 's' || c == 'c'; }
static int is_ghost(char c) { return c == 'g' || c == 'f' || c == 'e'; }
static int is_half(const brk_rhythm_t *r, int level) { return level == 4 || r->density < 35u; }

typedef struct {
    uint64_t bars, kicks, sng, ghosts, hats, hat16, kick10, oddkicks, doubles, opens, rims, toms, phrases_end, kickpos[16];
} plain_stat_t;
typedef struct {
    uint64_t bars, hits, ghosts, displaced, rests, breaks, kicks, snares, maxrun, maxhits;
} brk_stat_t;

/* --- a plain bar --- */
static void check_plain(const brk_bar_t *o, const brk_gen_t *g, int st, int lv, uint32_t bar, plain_stat_t *ps)
{
    const brk_rhythm_t *r = en_style_rhythm(st);
    const uint32_t pos = bar % PHRASE;
    const char *nm = EN_STYLE_NAME[st];
    int i, k = 0, sg = 0, off = 0, opens = 0;
    CHECK(!o->is_break && !o->rest, "%s: a plain bar called a break", nm);
    for (i = 0; i < 16; i++) {
        const char c = o->core[i], t = o->top[i], h = o->hat[i];
        CHECK(c == '.' || c == 'K' || c == 'k' || c == 'S' || c == 's' || c == 't', "%s: core %c at %d", nm, c, i);
        CHECK(t == '.' || t == 'f' || t == 'e' || t == 'r', "%s: top %c at %d", nm, t, i);
        CHECK(h == '.' || h == 'h' || h == 'j' || h == 'l' || h == 'i' || h == 'o', "%s: hat %c at %d", nm, h, i);
        CHECK(c == '.' || o->vcore[i] > 50u, "%s: a silent hit", nm);
        CHECK(c == '.' || t == '.', "%s: core and top on one step, %d", nm, i);
        CHECK(!o->late[i] || is_ghost(t), "%s: lateness on anything but a ghost, step %d", nm, i);
        CHECK(o->late[i] <= 6u, "%s: %u ms late", nm, o->late[i]);
        if (lv < 2 || lv > 4)
            CHECK(c == '.' && t == '.', "%s level %d: %c %c at %d", nm, lv, c, t, i);
        if (lv == 0)
            CHECK(h == '.', "%s level 0: a hat", nm);
        if (is_kick(c)) {
            k++;
            CHECK(i != 15 && i != 1, "%s: a kick at %d, next to a downbeat", nm, i);
            CHECK(c != 'K' || i == 0, "%s: the firm kick at %d", nm, i);
            if (i < 15 && is_kick(o->core[i + 1])) {
                CHECK(i == 10 || i == 6, "%s: two kicks at %d, not a deliberate double", nm, i);
                CHECK(!is_half(r, lv), "%s: a double in half time", nm);
                ps->doubles++;
            }
            ps->kickpos[i]++;
            ps->oddkicks += (uint64_t)(i & 1);
            ps->kick10 += i == 10;
        }
        if (is_snare(c))
            sg++;
        if (is_ghost(t)) {
            sg++;
            ps->ghosts++;
            CHECK(i & 1, "%s: a ghost on a strong step, %d", nm, i);
            if (is_half(r, lv))
                CHECK(i != 7 && i != 9, "%s: a ghost next to the half-time snare, %d", nm, i);
            else
                CHECK(i != 3 && i != 5 && i != 11 && i != 13, "%s: a ghost next to the backbeat, %d", nm, i);
            CHECK(t != 'g', "%s: a loud ghost in a plain bar", nm);
            CHECK(o->vtop[i] >= 60u && o->vtop[i] <= 114u, "%s: a ghost at %u", nm, o->vtop[i]);
        }
        if (c == 't' || t == 'r') {
            CHECK(pos == PHRASE - 1u && i >= 13 && lv >= 2, "%s: a pickup at bar %u of the phrase, step %d", nm, pos, i);
            ps->rims += t == 'r';
            ps->toms += c == 't';
        }
        if (h != '.') {
            ps->hats++;
            CHECK(o->vhat[i] > 0u && o->vhat[i] <= 125u, "%s: a hat at %u", nm, o->vhat[i]);
            if (i % 4 == 2)
                off++;
            else
                ps->hat16++;
            if (i % 4 != 2 && h != 'o')
                CHECK(h == 'i', "%s: %c, a firm hat off the off-beats, %d", nm, h, i);
        }
        if (h == 'o') {
            opens++;
            ps->opens++;
            CHECK(i == 11 || i == 14, "%s: an open hat at %d", nm, i);
            CHECK(lv >= 2, "%s: an open hat at level %d", nm, lv);
            if (i == 14)
                CHECK(pos == PHRASE - 1u || bar % 4u == 3u, "%s: an open hat at 14 in phrase bar %u", nm, pos);
            if (i == 11) {
                CHECK(o->hat[12] != 'o' && o->hat[13] != 'o', "%s: two open hats", nm);
            }
        }
    }
    CHECK(opens <= 2, "%s: %d open hats in a bar", nm, opens);
    if (lv >= 1 && lv <= 4)
        CHECK(off >= 3, "%s level %d: %d off-beats in the hats", nm, lv, off);
    if (lv >= 2 && lv <= 4) {
        const int half = is_half(r, lv);
        if (half) {
            CHECK(o->core[8] == 'S' || o->core[8] == 's', "%s level %d: no half-time snare on 8", nm, lv);
            CHECK(!is_snare(o->core[4]) && !is_snare(o->core[12]), "%s level %d: a two-step snare in half time", nm, lv);
        } else {
            CHECK(o->core[4] == 'S' || o->core[4] == 's', "%s: no snare on 4", nm);
            CHECK(o->core[12] == 'S' || o->core[12] == 's', "%s: no snare on 12", nm);
            CHECK(!is_snare(o->core[8]), "%s: a snare on 8", nm);
        }
        if (pos == 0u || !((bar - g->start) & 1u))
            CHECK(o->core[0] == 'K' && !o->late[0], "%s level %d bar %u: no kick on the groove's start", nm, lv, bar);
        CHECK(k <= MAX_KICKS[st][lv - 2], "%s level %d: %d kicks", nm, lv, k);
        CHECK(sg <= MAX_SNG[st][lv - 2], "%s level %d: %d snares and ghosts", nm, lv, sg);
        CHECK(k >= 1, "%s level %d: no kick in the bar", nm, lv);
        ps->kicks += (uint64_t)k;
        ps->sng += (uint64_t)sg;
    }
    if (pos == PHRASE - 1u)
        ps->phrases_end++;
    ps->bars++;
}

/* --- a break's bar. Where it stands in its break, and what came before --- */
typedef struct {
    int last_hits, trail;
} brk_walk_t;

static void check_break(const brk_bar_t *o, int st, uint32_t bar, int len, uint32_t in, brk_walk_t *w, brk_stat_t *bs)
{
    const char *nm = EN_STYLE_NAME[st];
    const int last = in + 1u == (uint32_t)len;
    int i, hits = 0, run = in ? w->trail : 0, mrun = 0, first = 0, hrun = 0, disp = 0, gap = 0, mgap = 0;
    CHECK(o->is_break, "%s: a break asked for, not played", nm);
    CHECK(bar % PHRASE + (uint32_t)len - in == PHRASE, "%s: a break off the phrase's end", nm);
    for (i = 0; i < 16; i++) {
        const char c = o->core[i], t = o->top[i], h = o->hat[i];
        const int on = c != '.' || t != '.';
        CHECK(!(c != '.' && t != '.'), "%s: core and top on one step", nm);
        CHECK(c == '.' || is_kick(c) || is_snare(c) || c == 't', "%s: core %c", nm, c);
        CHECK(t == '.' || is_ghost(t) || t == 'r', "%s: top %c", nm, t);
        CHECK(!o->late[i] || is_ghost(t), "%s: lateness on anything but a ghost", nm);
        CHECK(o->late[i] <= 6u, "%s: %u ms late", nm, o->late[i]);
        CHECK(o->vcore[i] <= 98u, "%s: a break's hit at %u: not louder", nm, o->vcore[i]);
        if (!en_style_rhythm(st)->ghost)
            CHECK(!is_ghost(t), "%s: a ghost in a style without", nm);
        if (is_kick(c)) {
            CHECK(c != 'K' || i == 0, "%s: firm kick at %d of a break", nm, i);
            CHECK(i != 15 && i != 1, "%s: a kick at %d of a break", nm, i);
            CHECK(i == 15 || !is_kick(o->core[i + 1]), "%s: two kicks in a row in a break, %d", nm, i);
            bs->kicks++;
        }
        if (is_snare(c)) {
            bs->snares++;
            if (c != 'c' && i != 4 && i != 12 && i != 8)
                disp = 1;
        }
        if (is_ghost(t))
            bs->ghosts++;
        hits += on;
        first += on && i < 8;
        run = on ? run + 1 : 0;
        mrun = run > mrun ? run : mrun;
        if (h != '.') {
            hrun++;
            CHECK(hrun <= 6, "%s: hats, more than six in a row", nm);
            CHECK(o->vhat[i] > 0u && o->vhat[i] <= 125u, "%s: a hat at %u in a break", nm, o->vhat[i]);
        } else {
            hrun = 0;
        }
        if (h == 'o') {
            int j;
            for (j = i + 1; j < 16; j++)
                CHECK(o->hat[j] != 'o', "%s: two open hats in a break's bar", nm);
        }
        if (!on && h == '.') {
            gap++;
            mgap = gap > mgap ? gap : mgap;
        } else {
            gap = 0;
        }
    }
    CHECK(hits <= MAX_BRK[st], "%s: %d hits in a break's bar", nm, hits);
    CHECK(mrun <= MAX_RUN[st], "%s: %d hits in a row, no machine guns", nm, mrun);
    CHECK(hits >= 2, "%s: %d hits in a break's bar", nm, hits);
    if (o->rest) {
        CHECK(o->rest >= 6u, "%s: a rest of %u", nm, o->rest);
        CHECK(mgap >= (int)o->rest, "%s: a rest of %u is really %d", nm, o->rest, mgap);
        bs->rests++;
    } else {
        /* it rises: from bar to bar, and in the bar */
        if (in && w->last_hits >= 0)
            CHECK(hits >= w->last_hits, "%s: a break falls, %d hits after %d, bar %u of %d", nm, hits, w->last_hits, in, len);
        w->last_hits = hits;
        if (last)
            CHECK(first <= hits - first, "%s: the last bar's first half busier: %d of %d", nm, first, hits);
        if (last)
            CHECK(o->core[13] != '.' || o->core[14] != '.' || o->core[15] != '.' || o->top[13] != '.' || o->top[14] != '.' || o->top[15] != '.', "%s: the last bar leads nowhere", nm);
    }
    if (last)
        CHECK(!is_kick(o->core[15]), "%s: a kick just before the downbeat", nm);
    w->trail = run;
    bs->bars++;
    bs->hits += (uint64_t)hits;
    bs->displaced += (uint64_t)disp;
    bs->maxrun = (uint64_t)mrun > bs->maxrun ? (uint64_t)mrun : bs->maxrun;
    bs->maxhits = (uint64_t)hits > bs->maxhits ? (uint64_t)hits : bs->maxhits;
}

/* --- the walks: levels as the arranger has them; or each level in turn, breaks at every phrase --- */
static plain_stat_t pstat[EN_NSTYLES][5];
static brk_stat_t bstat[EN_NSTYLES];

static void walk(int st, int energy, int forced, uint32_t seed, uint32_t nbars)
{
    static const uint8_t ORDER[6] = {2, 3, 4, 1, 3, 0};
    const brk_rhythm_t *r = en_style_rhythm(st);
    brk_gen_t g;
    brk_bar_t o;
    brk_walk_t w = {-1, 0};
    en_arr_t a;
    uint32_t bar, in_sec = 0u;
    int was_brk = 0, prev_lv = 0;
    char prev_open = 0;
    brk_gen_start(&g);
    en_arr_first(&a, seed, st, energy);
    for (bar = 0u; bar < nbars; bar++) {
        en_part_t p;
        uint32_t in = 0u;
        int lv, want;
        if (forced) {
            lv = ORDER[bar / 32u % 6u];
            want = en_arr_break(bar, lv, st, 100, seed, &in);
            if (!want && (lv == 2 || lv == 3) && bar % PHRASE == PHRASE - 1u && !(bar / PHRASE % 3u)) {
                want = 1;
                in = 0u;
            }
        } else {
            while (in_sec >= a.bars) {
                in_sec -= a.bars;
                en_arr_next(&a, seed, st, energy);
            }
            en_arr_part(&a, in_sec, st, energy, &p);
            in_sec++;
            lv = p.level;
            want = en_arr_break(bar, lv, st, DEF_BREAKS, seed, &in);
            if (!energy)
                CHECK(lv <= 1 && !want, "energy 0: level %d, break %d", lv, want);
        }
        brk_gen_bar(&g, r, lv, energy, DEF_EVOLVE, seed, bar, want, in, &o);
        if (want) {
            if (!in) {
                w.last_hits = -1;
                bstat[st].breaks++;
            }
            check_break(&o, st, bar, want, in, &w, &bstat[st]);
        } else {
            check_plain(&o, &g, st, lv, bar, &pstat[st][lv]);
            /* after a break: on the grid, the plain kick, nothing else on the step */
            if (was_brk) {
                CHECK(bar % PHRASE == 0u, "%s: a break ended at bar %u of the phrase", EN_STYLE_NAME[st], bar % PHRASE);
                if (lv >= 2)
                    CHECK(o.core[0] == 'K' && o.top[0] == '.' && !o.late[0], "%s: the next phrase starts with %c%c", EN_STYLE_NAME[st], o.core[0], o.top[0]);
            }
        }
        /* an open hat: the next hat is a closed one, which shuts it */
        {
            int i;
            for (i = 0; i < 16; i++) {
                if (o.hat[i] == '.')
                    continue;
                if (prev_open)
                    CHECK(o.hat[i] != 'o', "%s: an open hat after an open hat", EN_STYLE_NAME[st]);
                prev_open = o.hat[i] == 'o';
            }
        }
        was_brk = want;
        prev_lv = lv;
    }
    (void)prev_lv;
}

static void invariants(void)
{
    int st, e;
    for (st = 0; st < EN_NSTYLES; st++)
        for (e = 0; e < 4; e++) {
            walk(st, ENERGIES[e], 0, 101u + (uint32_t)e, NBARS);
            walk(st, ENERGIES[e], 1, 202u + (uint32_t)e, NBARS);
        }
    /* energy 0 keeps its meaning */
    for (st = 0; st < EN_NSTYLES; st++)
        walk(st, 0, 0, 7u, NBARS);
    /* what happens: the kick on 10 is the strongest after the downbeat; doubles rare; pickups rare; hats mostly off-beats */
    for (st = 0; st < EN_NSTYLES; st++) {
        int lv;
        for (lv = 2; lv <= 3; lv++) {
            const plain_stat_t *ps = &pstat[st][lv];
            const char *nm = EN_STYLE_NAME[st];
            int i;
            if (!ps->bars)
                continue;
            if (!is_half(en_style_rhythm(st), lv)) {
                for (i = 1; i < 16; i++)
                    if (i != 10)
                        CHECK(ps->kickpos[10] > ps->kickpos[i], "%s level %d: step %d has more kicks than 10", nm, lv, i);
                CHECK(ps->doubles * 25u < ps->bars, "%s level %d: %llu doubles in %llu bars", nm, lv, (unsigned long long)ps->doubles, (unsigned long long)ps->bars);
            }
            CHECK((ps->rims + ps->toms) * 4u < ps->phrases_end, "%s level %d: %llu pickups in %llu phrases", nm, lv, (unsigned long long)(ps->rims + ps->toms), (unsigned long long)ps->phrases_end);
            CHECK(ps->hat16 * 3u < ps->hats * 2u, "%s level %d: the hats are not off-beat based", nm, lv);
            CHECK(ps->opens * 4u < ps->bars, "%s level %d: %llu open hats in %llu bars", nm, lv, (unsigned long long)ps->opens, (unsigned long long)ps->bars);
        }
    }
}

/* --- what a break is asked for, out of its place --- */
static void only_at_the_end(void)
{
    brk_gen_t g;
    brk_bar_t o;
    uint32_t bar;
    const brk_rhythm_t *r = en_style_rhythm(EN_ST_JUNGLE);
    brk_gen_start(&g);
    for (bar = 0u; bar < 4000u; bar++) {
        int len;
        uint32_t in;
        for (len = 1; len <= 4; len++)
            for (in = 0u; in < (uint32_t)len; in++) {
                const int good = bar % PHRASE + (uint32_t)len - in == PHRASE;
                brk_gen_t c = g;
                brk_gen_bar(&c, r, 3, 70, 50, 5u, bar, len, in, &o);
                CHECK(o.is_break == (uint8_t)good, "a break of %d at phrase bar %u, %u in: played %u", len, bar % PHRASE, in, o.is_break);
                c = g;
                brk_gen_bar(&c, r, 1, 70, 50, 5u, bar, len, in, &o);
                CHECK(!o.is_break, "a break at level 1");
                c = g;
                brk_gen_bar(&c, r, 4, 70, 50, 5u, bar, len, in, &o);
                CHECK(!o.is_break, "a break at level 4");
            }
        brk_gen_bar(&g, r, 3, 70, 50, 5u, bar, 0, 0u, &o);
    }
}

/* --- the groove --- */
typedef struct {
    uint16_t kick[2], ghost[2], hat[2];
} seen_t;

static void masks(const brk_bar_t *o, uint16_t *k, uint16_t *g, uint16_t *h)
{
    int i;
    *k = *g = *h = 0u;
    for (i = 0; i < 16; i++) {
        if (is_kick(o->core[i]))
            *k |= (uint16_t)(1u << i);
        if (is_ghost(o->top[i]))
            *g |= (uint16_t)(1u << i);
        if (o->hat[i] != '.')
            *h |= (uint16_t)(1u << i);
    }
}
static int popc16(uint32_t v)
{
    int n = 0;
    for (; v; v &= v - 1u)
        n++;
    return n;
}

typedef struct {
    uint64_t hist[260], grooves, at_phrase, cycles, changed, maxk, maxg, maxh;
    uint32_t lo, hi;
} groove_stat_t;

static void groove_walk(int st, int level, int evolve, int with_breaks, uint32_t seed, uint32_t nbars, groove_stat_t *s)
{
    const brk_rhythm_t *r = en_style_rhythm(st);
    brk_gen_t g;
    brk_bar_t o;
    seen_t first = {{0u, 0u}, {0u, 0u}, {0u, 0u}};
    uint32_t bar, last_gno = 0u, last_start = 0u, have = 0u;
    uint32_t cyc_k = 0u, cyc_g = 0u, cyc_h = 0u;
    brk_gen_start(&g);
    s->lo = 0xFFFFFFFFu;
    for (bar = 0u; bar < nbars; bar++) {
        uint32_t in = 0u;
        const int want = with_breaks ? en_arr_break(bar, level, st, 100, seed, &in) : 0;
        uint16_t k, gh, h;
        uint32_t t;
        brk_gen_bar(&g, r, level, DEF_ENERGY, evolve, seed, bar, want, in, &o);
        if (!have || g.gno != last_gno) {
            if (have) {
                const uint32_t len = bar - last_start;
                s->hist[len < 259u ? len : 259u]++;
                s->grooves++;
                s->at_phrase += bar % PHRASE == 0u;
                s->lo = len < s->lo ? len : s->lo;
                s->hi = len > s->hi ? len : s->hi;
            }
            have = 1u;
            last_gno = g.gno;
            last_start = bar;
            first.kick[0] = first.kick[1] = first.ghost[0] = first.ghost[1] = first.hat[0] = first.hat[1] = 0u;
        }
        t = bar - last_start;
        if (o.is_break)
            continue;
        masks(&o, &k, &gh, &h);
        if (t < 2u) {
            first.kick[t] = k;
            first.ghost[t] = gh;
            first.hat[t] = h;
        } else if (!with_breaks) {
            /* the phrase's start may force the downbeat: not the groove changing */
            const uint32_t dk = (uint32_t)popc16(k ^ first.kick[t & 1u]);
            const uint32_t dg = (uint32_t)popc16(gh ^ first.ghost[t & 1u]);
            const uint32_t dh = (uint32_t)popc16(h ^ first.hat[t & 1u]);
            if (!(t & 1u)) {
                cyc_k = dk;
                cyc_g = dg;
                cyc_h = dh;
            } else {
                cyc_k += dk;
                cyc_g += dg;
                cyc_h += dh;
                s->cycles++;
                s->changed += cyc_k || cyc_g || cyc_h || memcmp(&g.cur, &g.base, sizeof g.cur);
                s->maxk = cyc_k > s->maxk ? cyc_k : s->maxk;
                s->maxg = cyc_g > s->maxg ? cyc_g : s->maxg;
                s->maxh = cyc_h > s->maxh ? cyc_h : s->maxh;
            }
        }
    }
}

static void groove_checks(void)
{
    int st;
    printf("  groove length in bars at the default Evolve, level 3 (4 / 6 / 8, share ending at a phrase's start; changed repeats; largest change):\n");
    for (st = 0; st < EN_NSTYLES; st++) {
        static groove_stat_t s, sb;
        const char *nm = EN_STYLE_NAME[st];
        memset(&s, 0, sizeof s);
        memset(&sb, 0, sizeof sb);
        groove_walk(st, 3, DEF_EVOLVE, 0, 11u, NBARS, &s);
        groove_walk(st, 3, DEF_EVOLVE, 1, 12u, NBARS, &sb);
        printf("  %-8s %5.1f%% %5.1f%% %5.1f%%, %5.1f%% at the phrase; %5.1f%% changed; kick %llu ghost %llu hat %llu\n", nm, 100.0 * (double)s.hist[4] / (double)s.grooves, 100.0 * (double)s.hist[6] / (double)s.grooves,
               100.0 * (double)s.hist[8] / (double)s.grooves, 100.0 * (double)s.at_phrase / (double)s.grooves, 100.0 * (double)s.changed / (double)s.cycles, (unsigned long long)s.maxk, (unsigned long long)s.maxg, (unsigned long long)s.maxh);
        CHECK(s.lo >= 4u && s.hi <= 8u, "%s: grooves of %u to %u bars", nm, s.lo, s.hi);
        CHECK(sb.lo >= 4u && sb.hi <= 8u, "%s with breaks: grooves of %u to %u bars", nm, sb.lo, sb.hi);
        CHECK(s.hist[5] == 0u && s.hist[7] == 0u, "%s: an odd groove", nm);
        CHECK(s.at_phrase * 2u > s.grooves, "%s: %llu of %llu grooves change at a phrase's start", nm, (unsigned long long)s.at_phrase, (unsigned long long)s.grooves);
        CHECK(s.maxk <= 3u && s.maxg <= 3u && s.maxh <= 1u, "%s: a change of kick %llu, ghost %llu, hat %llu", nm, (unsigned long long)s.maxk, (unsigned long long)s.maxg, (unsigned long long)s.maxh);
        CHECK(s.changed * 5u > s.cycles && s.changed * 10u < s.cycles * 9u, "%s: %llu of %llu repeats changed", nm, (unsigned long long)s.changed, (unsigned long long)s.cycles);
        /* Evolve 0: long and all but still */
        memset(&s, 0, sizeof s);
        groove_walk(st, 3, 0, 0, 13u, NBARS, &s);
        CHECK(s.lo >= 64u, "%s Evolve 0: a groove of %u bars", nm, s.lo);
        CHECK(s.changed * 12u < s.cycles, "%s Evolve 0: %llu of %llu repeats changed", nm, (unsigned long long)s.changed, (unsigned long long)s.cycles);
        if (!st)
            printf("  Evolve 0: grooves of %u to %u bars, %.1f%% of repeats changed\n", s.lo, s.hi, 100.0 * (double)s.changed / (double)s.cycles);
        /* Evolve 100: still 4-8 bars, every repeat a little different, and no more than twice as much */
        memset(&s, 0, sizeof s);
        groove_walk(st, 3, 100, 0, 14u, NBARS, &s);
        CHECK(s.lo >= 4u && s.hi <= 8u, "%s Evolve 100: grooves of %u to %u bars", nm, s.lo, s.hi);
        CHECK(s.maxk <= 6u && s.maxg <= 6u && s.maxh <= 2u, "%s Evolve 100: changes of %llu, %llu, %llu", nm, (unsigned long long)s.maxk, (unsigned long long)s.maxg, (unsigned long long)s.maxh);
        if (!st)
            printf("  Evolve 100: grooves of %u to %u bars, %.1f%% of repeats changed\n", s.lo, s.hi, 100.0 * (double)s.changed / (double)s.cycles);
    }
}

/* same seed, same bars; other seeds, other grooves */
static uint64_t fnv(uint64_t h, const void *p, size_t n)
{
    const uint8_t *b = (const uint8_t *)p;
    size_t i;
    for (i = 0; i < n; i++)
        h = (h ^ b[i]) * 1099511628211ull;
    return h;
}

static uint64_t run_hash(int st, uint32_t seed, int energy, uint32_t nbars)
{
    brk_gen_t g;
    brk_bar_t o;
    uint32_t bar;
    uint64_t h = 14695981039346656037ull;
    brk_gen_start(&g);
    for (bar = 0u; bar < nbars; bar++) {
        uint32_t in = 0u;
        const int lv = 2 + (int)(bar / 64u % 3u);
        const int want = en_arr_break(bar, lv, st, 80, seed, &in);
        brk_gen_bar(&g, en_style_rhythm(st), lv, energy, 50, seed, bar, want, in, &o);
        h = fnv(h, &o, sizeof o);
    }
    return h;
}

#define NSET (1u << 18)
static uint64_t set[NSET];
static uint32_t set_n;
static void set_clear(void)
{
    memset(set, 0, sizeof set);
    set_n = 0u;
}
static void set_add(uint64_t h)
{
    uint32_t i = (uint32_t)h & (NSET - 1u);
    if (!h)
        h = 1u;
    while (set[i] && set[i] != h)
        i = (i + 1u) & (NSET - 1u);
    if (!set[i]) {
        set[i] = h;
        set_n++;
    }
}

static void seed_checks(void)
{
    int st;
    for (st = 0; st < EN_NSTYLES; st++) {
        const char *nm = EN_STYLE_NAME[st];
        uint32_t sd;
        CHECK(run_hash(st, 77u, 70, 20000u) == run_hash(st, 77u, 70, 20000u), "%s: same seed, other bars", nm);
        CHECK(run_hash(st, 77u, 70, 2000u) != run_hash(st, 78u, 70, 2000u), "%s: two seeds, the same bars", nm);
        /* the first grooves of 1000 seeds */
        set_clear();
        for (sd = 0u; sd < 1000u; sd++) {
            brk_gen_t g;
            brk_bar_t o;
            uint64_t h = 14695981039346656037ull;
            brk_gen_start(&g);
            brk_gen_bar(&g, en_style_rhythm(st), 3, 70, 50, sd, 0u, 0, 0u, &o);
            h = fnv(h, &g.base, sizeof g.base);
            set_add(h);
        }
        printf("  %-8s of 1000 seeds, %u different first grooves\n", nm, set_n);
        CHECK(set_n > 100u, "%s: only %u first grooves of 1000 seeds", nm, set_n);
    }
}

/* --- breaks: how many different --- */
static void break_variety(void)
{
    static const uint16_t MIN_DISTINCT[EN_NSTYLES] = {2000u, 4000u, 5000u, 500u, 100u, 1000u};
    int st;
    printf("  breaks: distinct bars of 10 000 (drums; with hats); bars with a displaced snare, hits, ghosts, rests a break:\n");
    for (st = 0; st < EN_NSTYLES; st++) {
        const char *nm = EN_STYLE_NAME[st];
        static brk_stat_t bs;
        uint32_t n = 0u, sd = 0u, drums;
        static uint64_t all[10000];
        memset(&bs, 0, sizeof bs);
        set_clear();
        while (n < 10000u) {
            brk_gen_t g;
            brk_bar_t o;
            uint32_t bar;
            sd++;
            brk_gen_start(&g);
            for (bar = 0u; bar < 800u && n < 10000u; bar++) {
                uint32_t in = 0u;
                int want = en_arr_break(bar, 3, st, 100, sd, &in);
                if (!want && bar % PHRASE == PHRASE - 1u) {
                    want = 1;
                    in = 0u;
                }
                brk_gen_bar(&g, en_style_rhythm(st), 3, DEF_ENERGY, 50, sd, bar, want, in, &o);
                if (o.is_break) {
                    uint64_t h = fnv(14695981039346656037ull, o.core, 32u);
                    set_add(h);
                    all[n++] = fnv(h, o.hat, 16u);
                    {
                        int i, hits = 0, disp = 0;
                        for (i = 0; i < 16; i++) {
                            hits += o.core[i] != '.' || o.top[i] != '.';
                            bs.ghosts += (uint64_t)is_ghost(o.top[i]);
                            if ((o.core[i] == 'S' || o.core[i] == 's') && i != 4 && i != 12 && i != 8)
                                disp = 1;
                        }
                        bs.hits += (uint64_t)hits;
                        bs.displaced += (uint64_t)disp;
                        bs.rests += o.rest != 0u;
                        bs.bars++;
                    }
                }
            }
        }
        drums = set_n;
        set_clear();
        for (n = 0u; n < 10000u; n++)
            set_add(all[n]);
        printf("  %-8s %5u; %5u; displaced %4.1f%%, %4.2f hits, %4.2f ghosts a bar, %4.1f%% of bars with a rest\n", nm, drums, set_n, 100.0 * (double)bs.displaced / (double)bs.bars, (double)bs.hits / (double)bs.bars, (double)bs.ghosts / (double)bs.bars,
               100.0 * (double)bs.rests / (double)bs.bars);
        CHECK(drums >= MIN_DISTINCT[st], "%s: %u different break bars of 10 000", nm, drums);
        bstat[st] = bs;
    }
    CHECK(bstat[EN_ST_JUNGLE].displaced * 2u > bstat[EN_ST_LIQUID].displaced * 3u, "Jungle not more displaced than Liquid");
    CHECK(bstat[EN_ST_JUNGLE].hits > bstat[EN_ST_LIQUID].hits, "Jungle not busier in its breaks than Liquid");
    CHECK(bstat[EN_ST_ROLLER].ghosts > bstat[EN_ST_LIQUID].ghosts && bstat[EN_ST_ROLLER].ghosts > bstat[EN_ST_JUNGLE].ghosts, "Roller's breaks not the ghostly ones");
    CHECK(bstat[EN_ST_HALF].hits < bstat[EN_ST_LIQUID].hits, "Half's breaks not spacious");
    CHECK(bstat[EN_ST_DRIFT].hits < bstat[EN_ST_LIQUID].hits, "Drift's breaks not minimal");
    CHECK(bstat[EN_ST_LOADING].hits < bstat[EN_ST_LIQUID].hits, "Loading's breaks not minimal");
    CHECK(bstat[EN_ST_DRIFT].ghosts == 0u, "Drift's breaks with ghosts");
    /* a rest happens; not every time */
    for (st = 0; st < EN_NSTYLES; st++) {
        CHECK(bstat[st].rests * 40u > bstat[st].bars, "%s: rests in %llu of %llu break bars", EN_STYLE_NAME[st], (unsigned long long)bstat[st].rests, (unsigned long long)bstat[st].bars);
        CHECK(bstat[st].rests * 3u < bstat[st].bars, "%s: rests in %llu of %llu break bars", EN_STYLE_NAME[st], (unsigned long long)bstat[st].rests, (unsigned long long)bstat[st].bars);
    }
}

/* --- what the numbers do, measured --- */
typedef struct {
    double kicks, sng, ghosts, hats, synco, vkick, vsnare, vghost, vhat;
} meas_t;
static void measure(const brk_rhythm_t *r, int level, int energy, meas_t *m)
{
    uint32_t sd, kicks = 0u, odd = 0u, sng = 0u, gh = 0u, hats = 0u, bars = 0u, snares = 0u, off = 0u;
    double vk = 0.0, vs = 0.0, vg = 0.0, vh = 0.0;
    for (sd = 1u; sd <= 40u; sd++) {
        brk_gen_t g;
        brk_bar_t o;
        uint32_t bar;
        brk_gen_start(&g);
        for (bar = 0u; bar < 500u; bar++) {
            int i;
            brk_gen_bar(&g, r, level, energy, 50, sd, bar, 0, 0u, &o);
            for (i = 0; i < 16; i++) {
                if (is_kick(o.core[i])) {
                    kicks++;
                    odd += (uint32_t)(i & 1);
                    vk += o.vcore[i];
                }
                if (is_snare(o.core[i])) {
                    sng++;
                    snares++;
                    vs += o.vcore[i];
                }
                if (is_ghost(o.top[i])) {
                    sng++;
                    gh++;
                    vg += o.vtop[i];
                }
                hats += o.hat[i] != '.';
                if (o.hat[i] != '.' && i % 4 == 2) {
                    off++;
                    vh += o.vhat[i];
                }
            }
            bars++;
        }
    }
    m->kicks = (double)kicks / bars;
    m->sng = (double)sng / bars;
    m->ghosts = (double)gh / bars;
    m->hats = (double)hats / bars;
    m->synco = kicks ? 100.0 * (double)odd / kicks : 0.0;
    m->vkick = kicks ? vk / kicks : 0.0;
    m->vsnare = snares ? vs / snares : 0.0;
    m->vghost = gh ? vg / gh : 0.0;
    m->vhat = off ? vh / off : 0.0;
}

static void number_checks(void)
{
    static meas_t m[EN_NSTYLES][2];
    brk_rhythm_t a, b;
    meas_t ma, mb;
    int st;
    printf("  measured, plain bars, default energy, levels 2 / 3: kicks, snares+ghosts, ghosts, hats a bar; kicks on odd sixteenths:\n");
    for (st = 0; st < EN_NSTYLES; st++) {
        const brk_rhythm_t *r = en_style_rhythm(st);
        measure(r, 2, DEF_ENERGY, &m[st][0]);
        measure(r, 3, DEF_ENERGY, &m[st][1]);
        printf("  %-8s (%3u %3u %3u) %4.2f/%4.2f  %4.2f/%4.2f  %4.2f/%4.2f  %5.2f/%5.2f  %4.1f%%/%4.1f%%\n", EN_STYLE_NAME[st], r->density, r->synco, r->ghost, m[st][0].kicks, m[st][1].kicks, m[st][0].sng, m[st][1].sng, m[st][0].ghosts, m[st][1].ghosts,
               m[st][0].hats, m[st][1].hats, m[st][0].synco, m[st][1].synco);
    }
    CHECK(m[EN_ST_ROLLER][1].ghosts > m[EN_ST_JUNGLE][1].ghosts && m[EN_ST_JUNGLE][1].ghosts > m[EN_ST_LIQUID][1].ghosts && m[EN_ST_LIQUID][1].ghosts > m[EN_ST_DRIFT][1].ghosts, "ghosts by style");
    CHECK(m[EN_ST_JUNGLE][1].synco > m[EN_ST_ROLLER][1].synco && m[EN_ST_JUNGLE][1].synco > m[EN_ST_LIQUID][1].synco, "syncopation by style");
    CHECK(m[EN_ST_ROLLER][1].hats > m[EN_ST_LIQUID][1].hats && m[EN_ST_LIQUID][1].hats > m[EN_ST_HALF][1].hats && m[EN_ST_HALF][1].hats > m[EN_ST_DRIFT][1].hats - 0.2, "hats by style");
    CHECK(m[EN_ST_ROLLER][1].kicks > m[EN_ST_LIQUID][1].kicks && m[EN_ST_JUNGLE][1].kicks > m[EN_ST_LIQUID][1].kicks, "kicks by style");
    CHECK(m[EN_ST_DRIFT][1].ghosts == 0.0, "Drift's ghosts");
    /* the kit's level is what it was: the demo's ranges (kick 95-105, snare 96-106 and 88-98, ghost 72-114, the off-beat hats 84-100 of a range 0.86-1.14) */
    for (st = 0; st < EN_NSTYLES; st++) {
        const meas_t *q = &m[st][1];
        printf("  %-8s velocity: kick %.1f, snare %.1f, ghost %.1f, off-beat hat %.1f\n", EN_STYLE_NAME[st], q->vkick, q->vsnare, q->vghost, q->vhat);
        CHECK(q->vkick > 97.0 && q->vkick < 103.0, "%s: the kick at %.1f", EN_STYLE_NAME[st], q->vkick);
        CHECK(q->vsnare > 90.0 && q->vsnare < 103.0, "%s: the snare at %.1f", EN_STYLE_NAME[st], q->vsnare);
        CHECK(!q->vghost || (q->vghost > 85.0 && q->vghost < 100.0), "%s: ghosts at %.1f", EN_STYLE_NAME[st], q->vghost);
        CHECK(q->vhat > 85.0 && q->vhat < 99.0, "%s: hats at %.1f", EN_STYLE_NAME[st], q->vhat);
    }
    /* each number by itself */
    a = *en_style_rhythm(EN_ST_LIQUID);
    b = a;
    b.synco = 90u;
    measure(&a, 3, DEF_ENERGY, &ma);
    measure(&b, 3, DEF_ENERGY, &mb);
    CHECK(mb.synco > ma.synco + 5.0, "syncopation does nothing: %.1f, %.1f", ma.synco, mb.synco);
    b = a;
    b.density = 90u;
    measure(&b, 3, DEF_ENERGY, &mb);
    CHECK(mb.kicks > ma.kicks + 0.2 && mb.hats > ma.hats + 1.0, "density does nothing: %.2f %.2f, %.2f %.2f", ma.kicks, mb.kicks, ma.hats, mb.hats);
    b = a;
    b.ghost = 90u;
    measure(&b, 3, DEF_ENERGY, &mb);
    CHECK(mb.ghosts > ma.ghosts + 0.5, "ghost does nothing: %.2f, %.2f", ma.ghosts, mb.ghosts);
    b = a;
    b.ghost = 0u;
    measure(&b, 3, DEF_ENERGY, &mb);
    CHECK(mb.ghosts == 0.0, "ghost 0: %.2f", mb.ghosts);
    /* energy: more of it, more going on, at the same level */
    measure(&a, 3, 20, &ma);
    measure(&a, 3, 100, &mb);
    CHECK(mb.kicks + mb.hats + mb.sng > ma.kicks + ma.hats + ma.sng, "energy does nothing");
    /* the levels: half time at level 4 in every style */
    for (st = 0; st < EN_NSTYLES; st++) {
        measure(en_style_rhythm(st), 4, DEF_ENERGY, &ma);
        CHECK(ma.kicks < 3.1, "%s level 4: %.2f kicks", EN_STYLE_NAME[st], ma.kicks);
    }
}

/* --- the engine plays what the generator composes --- */
static brk_gen_t shadow;
static uint32_t shadow_seed, shadow_bars, shadow_breaks;
static int shadow_style;
static void shadow_trace(uint32_t step, int section, int level, int character, int want)
{
    brk_bar_t o;
    uint32_t in = 0u;
    int w2;
    (void)section;
    (void)character;
    w2 = en_arr_break(step / 16u, level, shadow_style, 50, shadow_seed, &in);
    CHECK(w2 == want, "the engine's break %d, the arranger's %d", want, w2);
    brk_gen_bar(&shadow, en_style_rhythm(shadow_style), level, DEF_ENERGY, DEF_EVOLVE, shadow_seed, step / 16u, want, in, &o);
    CHECK(!memcmp(&o, &brk_bar, sizeof o), "%s bar %u: the engine plays another bar", EN_STYLE_NAME[shadow_style], step / 16u);
    shadow_bars++;
    shadow_breaks += o.is_break;
}

static void engine_checks(void)
{
    static const int ST[3] = {EN_ST_LIQUID, EN_ST_JUNGLE, EN_ST_HALF};
    static int32_t buf[512];
    int k;
    for (k = 0; k < 3; k++) {
        uint32_t i;
        en_init();
        shadow_style = ST[k];
        shadow_seed = 900u + (uint32_t)k;
        brk_gen_start(&shadow);
        en_seed(shadow_seed);
        en_set(P_STYLE, ST[k]);
        en_trace = shadow_trace;
        shadow_bars = shadow_breaks = 0u;
        en_play(1);
        for (i = 0u; i < 44100u * 800u / 256u; i++)
            en_render(buf, 256, 1024);
        en_trace = 0;
        en_play(0);
        CHECK(shadow_bars > 500u && shadow_breaks > 0u, "%s: %u bars, %u of breaks, as the engine plays them", EN_STYLE_NAME[ST[k]], shadow_bars, shadow_breaks);
        CHECK(en_hat_peak <= 2, "%d hat voices", en_hat_peak);
    }
}

/* --- to read --- */
static void chart_bar(const char *label, const brk_bar_t *o)
{
    char d[17], h[17];
    int i;
    for (i = 0; i < 16; i++) {
        const char c = o->core[i], t = o->top[i], a = o->hat[i];
        d[i] = c != '.' ? c : is_ghost(t) ? 'g' : t;
        h[i] = a == 'j' || a == 'l' ? 'h' : a;
    }
    d[16] = h[16] = 0;
    printf("%-14s %s  %s\n", label, d, h);
}

/* 16 bars around the first break of the first full section; a plain groove; two breaks */
static void chart_style(int st, uint32_t seed)
{
    static brk_bar_t bars[6000];
    static int lvl[6000], wnt[6000];
    static uint32_t ins[6000];
    const brk_rhythm_t *r = en_style_rhythm(st);
    brk_gen_t g;
    en_arr_t a;
    uint32_t bar, in_sec = 0u, n = 6000u, from = 0u, i;
    int found = 0, seen = 0;
    brk_gen_start(&g);
    en_arr_first(&a, seed, st, DEF_ENERGY);
    for (bar = 0u; bar < n; bar++) {
        en_part_t p;
        uint32_t in = 0u;
        int want;
        while (in_sec >= a.bars) {
            in_sec -= a.bars;
            en_arr_next(&a, seed, st, DEF_ENERGY);
        }
        en_arr_part(&a, in_sec, st, DEF_ENERGY, &p);
        in_sec++;
        want = en_arr_break(bar, p.level, st, DEF_BREAKS, seed, &in);
        brk_gen_bar(&g, r, p.level, DEF_ENERGY, DEF_EVOLVE, seed, bar, want, in, &bars[bar]);
        lvl[bar] = p.level;
        wnt[bar] = want;
        ins[bar] = in;
    }
    printf("\n=== %s, seed %u ===\n", EN_STYLE_NAME[st], seed);
    printf("16 bars in play (bar.phrase bar, level; drums, hats):\n");
    for (bar = 0u; bar + 16u < n && !found; bar++)
        if (wnt[bar] && !ins[bar] && lvl[bar] >= 2 && bar >= 12u && lvl[bar - 12u] >= 2) {
            from = bar - 8u - bar % 8u + (bar % 8u ? 0u : 0u);
            found = 1;
        }
    if (!found) {                                /* this style's play has no break in reach: where its drums are */
        for (bar = 0u; bar + 16u < n; bar++)
            if (lvl[bar] >= 2 && !(bar % 8u)) {
                from = bar;
                break;
            }
    }
    for (i = 0u; i < 16u; i++) {
        char l[32];
        snprintf(l, sizeof l, "%4u.%u L%d%s", from + i, (from + i) % 8u, lvl[from + i], wnt[from + i] ? " B" : "");
        chart_bar(l, &bars[from + i]);
    }
    printf("a plain groove:\n");
    for (bar = 0u; bar + 1u < n; bar++)
        if (lvl[bar] == 3 && !wnt[bar] && !wnt[bar + 1u] && lvl[bar + 1u] == 3 && !(bar & 1u)) {
            chart_bar("  bar 1", &bars[bar]);
            chart_bar("  bar 2", &bars[bar + 1u]);
            break;
        }
    if (bar + 1u >= n)
        for (bar = 0u; bar + 1u < n; bar++)
            if (lvl[bar] >= 2 && !wnt[bar] && !wnt[bar + 1u] && !(bar & 1u)) {
                char l[20];
                snprintf(l, sizeof l, "  L%d bar 1", lvl[bar]);
                chart_bar(l, &bars[bar]);
                chart_bar("  bar 2", &bars[bar + 1u]);
                break;
            }
    for (bar = 0u; bar < n && seen < 2; bar++)
        if (wnt[bar] && !ins[bar] && wnt[bar] >= (seen ? 2 : 1)) {
            int k;
            printf("a break of %d (the bar after it too):\n", wnt[bar]);
            for (k = 0; k <= wnt[bar]; k++) {
                char l[20];
                snprintf(l, sizeof l, "  %s%d", k == wnt[bar] ? "next " : "bar ", k + 1);
                chart_bar(l, &bars[bar + (uint32_t)k]);
            }
            seen++;
        }
    if (seen < 2) {                              /* none in play: what it would do at the full level */
        const int lens[2] = {1, 2};
        printf("(the style's play has %s here: asked for at level 3)\n", seen ? "too few breaks" : "no breaks");
        brk_gen_start(&g);
        for (bar = 0u; bar < 1600u && seen < 2; bar++) {
            const int len = lens[seen];
            const uint32_t at = bar % PHRASE;
            brk_bar_t o;
            const int want = (bar / PHRASE % 5u == 4u && at + (uint32_t)len >= PHRASE) ? len : 0;
            brk_gen_bar(&g, r, 3, DEF_ENERGY, DEF_EVOLVE, seed, bar, want, at + (uint32_t)len - PHRASE, &o);
            if (want) {
                char l[20];
                snprintf(l, sizeof l, "  bar %u/%d", at + (uint32_t)len - PHRASE + 1u, len);
                chart_bar(l, &o);
            }
            if (want && at == PHRASE - 1u)
                seen++;
        }
    }
}

int main(int argc, char **argv)
{
    if (argc > 1 && !strcmp(argv[1], "--chart")) {
        static const uint32_t SEED[EN_NSTYLES] = {1001u, 1002u, 1003u, 1004u, 1005u, 1006u};
        int st;
        for (st = 0; st < EN_NSTYLES; st++)
            chart_style(st, SEED[st]);
        return 0;
    }
    invariants();
    only_at_the_end();
    groove_checks();
    seed_checks();
    break_variety();
    number_checks();
    engine_checks();
    printf(fails ? "rhy: %d FAILED\n" : "rhy: ok\n", fails);
    return fails != 0;
}
