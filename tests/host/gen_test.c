/* SPDX-License-Identifier: MPL-2.0 */
/* The chords, one at a time: en_gen_next by itself, a pure function, over every pool and hours of chords. What the engine does
 * with them is harm_test.c's. Prints the statistics the owner is told about. */
#include <stdio.h>
#include <string.h>
#include "amb.h"

static int fails;
#define CHECK(c, ...)                                                                                                  \
    do {                                                                                                               \
        if (!(c)) {                                                                                                    \
            if (fails < 40) {                                                                                          \
                printf("FAIL %s:%d: ", __FILE__, __LINE__);                                                            \
                printf(__VA_ARGS__);                                                                                   \
                printf("\n");                                                                                          \
            }                                                                                                          \
            fails++;                                                                                                   \
        }                                                                                                              \
    } while (0)
void en_midi_out(uint32_t st, uint32_t d1, uint32_t d2) { (void)st, (void)d1, (void)d2; }

#define EVOLVE_MID 50
#define SECS_PER_BAR (4.0 * 60.0 / 170.0)
/* the arranger's characters, as a song goes through them */
static int song_character(uint32_t bar)
{
    const uint32_t b = bar % 216u;               /* a pass of 27 phrases */
    return b < 24u ? 0 : b < 40u ? 1 : b < 104u ? 2 : b < 128u ? 1 : b < 176u ? 3 : b < 200u ? 2 : 1;
}

static int pop(uint32_t m)
{
    int n = 0;
    for (m &= 0xFFFu; m; m &= m - 1u)
        n++;
    return n;
}

/* what may stand in a pool: the rules of the spec */
static void chord_valid(uint32_t p, const en_gchord_t *g)
{
    const en_hchord_t *c = &g->ch;
    const uint32_t out = (uint32_t)c->tones & ~p;
    int v, iv;
    CHECK(c->root < 12 && c->bass < 12 && c->col < 12, "pool %03x: out of range", p);
    CHECK(g->bars == 1 || g->bars == 2 || g->bars == 4, "pool %03x: %d bars", p, g->bars);
    if (c->borrowed) {
        CHECK(c->family != EN_FAM_SPARSE, "pool %03x: a borrowed chord in the sparse family", p);
        CHECK(pop(out) >= 1 && pop(out) <= 2, "pool %03x: %d notes borrowed", p, pop(out));
    } else {
        CHECK(!out, "pool %03x root %d shape %d: tones %03x outside", p, c->root, g->shape, out);
        CHECK(p >> c->col & 1u, "pool %03x root %d: colour %d outside", p, c->root, c->col);
    }
    for (iv = 0; iv < 12; iv++)
        if (c->tones >> ((c->root + iv) % 12) & 1u)
            CHECK(iv != 1 && iv != 6 && iv != 8, "pool %03x root %d shape %d: a tone %d above the root", p, c->root, g->shape, iv);
    CHECK(!(c->tones >> ((c->bass + 1) % 12) & 1u), "pool %03x root %d shape %d: a minor ninth over the bass", p, c->root, g->shape);
    CHECK(c->col != (c->root + 1) % 12, "pool %03x root %d: its colour a minor ninth", p, c->root);
    CHECK(pop(c->tones) >= 2, "pool %03x root %d: %d tones", p, c->root, pop(c->tones));
    CHECK(c->bass != 0, "pool %03x root %d shape %d: the tonic in the bass", p, c->root, g->shape);
    for (v = 0; v < 4; v++)
        CHECK((unsigned)c->keys[v] < 12u && (unsigned)c->pad[v] < 12u, "pool %03x: a voice out of range", p);
}

static void voices_valid(uint32_t p, int key, const en_gchord_t *g)
{
    int v;
    for (v = 0; v < 4; v++) {
        const int k = g->keys_v[v], q = g->pad_v[v];
        CHECK(k >= 52 + key && k <= 75 + key, "pool %03x key %d: keys note %d", p, key, k);
        CHECK(q >= 49 + key && q <= 72 + key, "pool %03x key %d: pad note %d", p, key, q);
        CHECK(g->ch.tones >> (((k - key) % 12 + 12) % 12) & 1u, "pool %03x key %d: keys note %d not in the chord", p, key, k);
        CHECK(g->ch.tones >> (((q - key) % 12 + 12) % 12) & 1u, "pool %03x key %d: pad note %d not in the chord", p, key, q);
        if (v) {
            CHECK(k > g->keys_v[v - 1], "pool %03x key %d: a unison in the hand", p, key);
            CHECK(q > g->pad_v[v - 1], "pool %03x key %d: a unison in the pad", p, key);
        }
    }
}

/* Every pool, every key: 4095 masks must not crash; the 2036 distinct pools with the UI's three notes must be right */
static void pool_checks(void)
{
    uint32_t mask, valid = 0u, fam[3] = {0u, 0u, 0u};
    for (mask = 1u; mask <= 4095u; mask++) {
        const uint32_t p = (mask | 1u) & 0xFFFu;
        en_gen_t st;
        en_gchord_t g;
        int key;
        if (mask != p || pop(p) < 3) {
            uint32_t i;
            en_gen_start(&st);
            for (i = 0u; i < 20u; i++)
                en_gen_next(&st, mask, 2, 1u, EVOLVE_MID, 1, EN_MASK_ALL, &g);
            continue;
        }
        valid++;
        fam[en_pool_family(p)]++;
        for (key = 0; key < 12; key++) {
            uint32_t i;
            en_gen_start(&st);
            for (i = 0u; i < 24u; i++) {
                const int prev_root = st.prev_root, prev_bass = st.prev_bass;
                en_gen_next(&st, p, key, 100u + (uint32_t)key, i % 3u == 0u ? 100 : EVOLVE_MID, (int)(i / 4u) & 3, EN_MASK_ALL, &g);
                chord_valid(p, &g);
                voices_valid(p, key, &g);
                if (i && pop(en_gen_roots(p)) >= 2) {   /* (only one root has anything over it: the pool forces it) */
                    CHECK(g.ch.root != prev_root, "pool %03x key %d: the same root twice", p, key);
                    CHECK(g.ch.bass != prev_bass, "pool %03x key %d: the same bass twice", p, key);
                }
            }
        }
    }
    CHECK(valid == 2036u, "%u valid pools", valid);
    printf("  pools: 4095 masks without a crash; %u distinct pools x 12 keys right: %u major, %u minor, %u sparse\n", valid, fam[0], fam[1], fam[2]);
}

/* The statistics of a long run of a mode */
#define LONG_CHORDS 3000u
typedef struct {
    uint32_t pool;
    const char *name;
    int key;
} mode_t_;
static const mode_t_ MODES[EN_NMODES] = {
    {0xAB5u, "Ionian", 2}, {0x6ADu, "Dorian", 2}, {0x5ABu, "Phrygian", 4}, {0xAD5u, "Lydian", 2},
    {0x6B5u, "Mixolydian", 2}, {0x5ADu, "Aeolian", 9}, {0x295u, "Pent maj", 2}, {0x4A9u, "Pent min", 2},
};

static uint32_t chord_id(const en_gchord_t *g) { return (uint32_t)g->ch.root << 8 | g->shape; }

static void long_run(const mode_t_ *m, uint32_t seed, int evolve, en_gchord_t *out, uint32_t n)
{
    en_gen_t st;
    uint32_t i, bar = 0u;
    en_gen_start(&st);
    for (i = 0u; i < n; i++) {
        en_gen_next(&st, m->pool, m->key, seed, evolve, song_character(bar), EN_MASK_ALL, &out[i]);
        bar += out[i].bars;
    }
}

/* degrees that can stand, in the stock modes: every one of them appears */
static void mode_checks(void)
{
    static const uint32_t WANT[EN_NMODES] = {
        /* the roots above the tonic: half-diminished ones out, the tonic rare and only inverted, so asked for apart */
        1u << 2 | 1u << 4 | 1u << 5 | 1u << 7 | 1u << 9,    /* E F# G A B */
        1u << 2 | 1u << 3 | 1u << 5 | 1u << 7 | 1u << 10,   /* E F G A C */
        1u << 1 | 1u << 3 | 1u << 5 | 1u << 8 | 1u << 10,   /* F G A C D over E */
        1u << 2 | 1u << 4 | 1u << 7 | 1u << 9 | 1u << 11,   /* E F# A B C# */
        1u << 2 | 1u << 5 | 1u << 7 | 1u << 9 | 1u << 10,   /* E G A B C */
        1u << 3 | 1u << 5 | 1u << 7 | 1u << 8 | 1u << 10,   /* C D E F G over A */
        1u << 2 | 1u << 4 | 1u << 7 | 1u << 9,              /* E F# A B */
        1u << 3 | 1u << 5 | 1u << 7 | 1u << 10,             /* F G A C */
    };
    int mi;
    for (mi = 0; mi < EN_NMODES; mi++) {
        const mode_t_ *m = &MODES[mi];
        uint32_t sd, deg_all = 0u, n_all = 0u, common = 0u, moved = 0u, tonic = 0u, area_gap = 0u, with_tonic = 0u;
        uint32_t huddle = 0u, ninths = 0u;
        uint32_t len[5] = {0u, 0u, 0u, 0u, 0u}, deg_n[12];
        memset(deg_n, 0, sizeof deg_n);
        for (sd = 0u; sd < 60u; sd++) {
            static en_gchord_t g[LONG_CHORDS];
            uint32_t i, bar = 0u, deg = 0u, gap = 0u, last_tonic = 100000u;
            long_run(m, 1000u + sd, EVOLVE_MID, g, LONG_CHORDS);
            for (i = 0u; i < LONG_CHORDS; i++) {
                const en_hchord_t *c = &g[i].ch;
                uint32_t v;
                chord_valid(m->pool, &g[i]);
                voices_valid(m->pool, m->key, &g[i]);
                /* a chord starts on a bar, and a chord of 2 or 4 keeps to its half of the phrase: every 8th bar a change */
                CHECK(!(bar % g[i].bars) || g[i].bars == 1u, "%s: %d bars at bar %u", m->name, g[i].bars, bar);
                CHECK(bar / 8u == (bar + g[i].bars - 1u) / 8u, "%s: a chord across the phrase's edge at bar %u", m->name, bar);
                len[g[i].bars]++;
                huddle += g[i].keys_v[3] - g[i].keys_v[0] < 7;
                for (v = 0u; v < 3u; v++) {
                    uint32_t u;
                    for (u = v + 1u; u < 4u; u++)
                        ninths += g[i].keys_v[u] - g[i].keys_v[v] == 13;
                }
                if (!c->borrowed) {
                    deg |= 1u << c->root;
                    deg_n[c->root]++;
                }
                with_tonic += c->tones & 1u;
                if (!c->root) {
                    CHECK(c->bass != 0u, "%s: a tonic chord on the tonic", m->name);
                    CHECK(last_tonic > i || i - last_tonic >= 6u, "%s: tonic chords %u apart", m->name, i - last_tonic);
                    CHECK(i >= 6u, "%s: a chord on the tonic among the first (%u)", m->name, i);
                    last_tonic = i;
                    tonic++;
                }
                if (i) {
                    const en_hchord_t *b = &g[i - 1u].ch;
                    CHECK(c->root != b->root, "%s seed %u chord %u: the same root twice", m->name, sd, i);
                    CHECK(c->bass != b->bass, "%s seed %u chord %u: the same bass twice", m->name, sd, i);
                    common += (uint32_t)pop(c->tones & b->tones);
                    for (v = 0u; v < 4u; v++) {
                        const int d = (int)g[i].keys_v[v] - (int)g[i - 1u].keys_v[v];
                        moved += (uint32_t)(d < 0 ? -d : d);
                    }
                }
                /* the key stays clear: the tonic area comes round. Functions as in FN_OF: the relatives by family */
                {
                    static const uint16_t AREA[3] = {1u | 1u << 4 | 1u << 9, 1u | 1u << 3 | 1u << 8, 1u | 1u << 3 | 1u << 4 | 1u << 8 | 1u << 9};
                    gap = AREA[c->family] >> c->root & 1u ? 0u : gap + 1u;
                    area_gap = gap > area_gap ? gap : area_gap;
                }
                bar += g[i].bars;
            }
            n_all += LONG_CHORDS;
            /* 1000 chords are enough for every degree */
            {
                uint32_t early = 0u;
                for (i = 0u; i < 1000u; i++)
                    early |= 1u << g[i].ch.root;
                CHECK((early & WANT[mi]) == WANT[mi], "%s seed %u: 1000 chords reach only %03x of %03x", m->name, sd, early, WANT[mi]);
            }
            deg_all |= deg;
        }
        CHECK(huddle * 100u <= n_all, "%s: the hand huddled in %.1f%% of chords", m->name, 100.0 * huddle / n_all);
        CHECK(ninths * 100u <= n_all, "%s: two hand notes a minor ninth apart in %.1f%% of chords", m->name, 100.0 * ninths / n_all);
        printf("  %-10s hand: under a fifth wide %.2f%%, a minor ninth inside %.2f%%; roots", m->name, 100.0 * huddle / n_all, 100.0 * ninths / n_all);
        {
            int r;
            for (r = 0; r < 12; r++)
                if (deg_n[r])
                    printf(" %s %.1f%%", EN_NOTE_NAME[(m->key + r) % 12], 100.0 * deg_n[r] / n_all);
        }
        printf("\n             lengths 1: %.1f%% 2: %.1f%% 4: %.1f%% of chords; common tones %.2f a chord, the hand moves %.1f semitones; tonic chords %.1f%%, tonic in the chord %.0f%%, longest from the tonic area %u chords\n",
               100.0 * len[1] / n_all, 100.0 * len[2] / n_all, 100.0 * len[4] / n_all, (double)common / n_all, (double)moved / n_all, 100.0 * tonic / n_all, 100.0 * with_tonic / n_all, area_gap);
        CHECK((deg_all & WANT[mi]) == WANT[mi], "%s: reached %03x", m->name, deg_all);
        CHECK(!(deg_all & ~(WANT[mi] | 1u)), "%s: a root not expected: %03x", m->name, deg_all);
        CHECK(tonic * 100u <= n_all * 8u, "%s: tonic chords %.1f%%", m->name, 100.0 * tonic / n_all);
        CHECK(tonic > 0u, "%s: never a tonic chord", m->name);
        CHECK(common * 10u >= n_all * 24u, "%s: %.2f common tones", m->name, (double)common / n_all);
        CHECK(moved <= n_all * 11u, "%s: the hand moves %.1f", m->name, (double)moved / n_all);
        CHECK(len[2] + len[4] >= len[1] * 5u, "%s: too many short chords", m->name);
        CHECK(len[1] > 0u, "%s: no 1-bar chords", m->name);
        CHECK(area_gap <= 20u, "%s: %u chords away from the tonic area", m->name, area_gap);
    }
}

/* the longest stretch of chords that has come before, at any distance, and how many times a phrase of the first's kind returned */
static uint32_t longest_repeat(const en_gchord_t *g, uint32_t n, uint32_t within)
{
    uint32_t i, j, best = 0u;
    for (i = 0u; i < n; i++)
        for (j = i + 1u; j < n && j - i <= within; j++) {
            uint32_t k = 0u;
            while (j + k < n && i + k < j && chord_id(&g[i + k]) == chord_id(&g[j + k]))
                k++;
            best = k > best ? k : best;
        }
    return best;
}

static void evolve_checks(void)
{
    int mi;
    for (mi = 0; mi < EN_NMODES; mi++) {
        const mode_t_ *m = &MODES[mi];
        static en_gchord_t g[1600];              /* more than an hour of chords */
        uint32_t sd, rep0 = 0u, rep_mid = 0u, rep_100 = 0u, rep_any = 0u, dist_mid = 0u, dist100 = 0u;
        for (sd = 0u; sd < 20u; sd++) {
            uint32_t i, j, per = 0u;
            /* 0: one phrase, for ever: chords and lengths */
            long_run(m, 500u + sd, 0, g, 1600u);
            for (per = 2u; per <= 4u; per++) {
                int same = 1;
                for (i = per; i < 1600u && same; i++)
                    same = chord_id(&g[i]) == chord_id(&g[i - per]) && g[i].bars == g[i - per].bars;
                if (same)
                    break;
            }
            CHECK(per <= 4u, "%s seed %u: Evolve 0 does not repeat a phrase of 2-4 chords", m->name, sd);
            rep0 = 1600u;
            /* 100: no three in a row again within the window */
            long_run(m, 500u + sd, 100, g, 1600u);
            for (i = 3u; i <= 1600u; i++)
                for (j = i > EN_GEN_WINDOW + 3u ? i - EN_GEN_WINDOW : 3u; j < i; j++)
                    CHECK(chord_id(&g[i - 1u]) != chord_id(&g[j - 1u]) || chord_id(&g[i - 2u]) != chord_id(&g[j - 2u]) || chord_id(&g[i - 3u]) != chord_id(&g[j - 3u]),
                          "%s seed %u: at Evolve 100 three chords again after %u", m->name, sd, i - j);
            {
                const uint32_t r = longest_repeat(g, 400u, EN_GEN_WINDOW), ra = longest_repeat(g, 400u, 400u);
                rep_100 = r > rep_100 ? r : rep_100;
                rep_any = ra > rep_any ? ra : rep_any;
            }
            {
                uint32_t k, seen[256], nd = 0u;
                for (i = 0u; i < 1600u; i++) {
                    for (k = 0u; k < nd && seen[k] != chord_id(&g[i]); k++) {}
                    if (k == nd && nd < 256u)
                        seen[nd++] = chord_id(&g[i]);
                }
                dist100 = nd > dist100 ? nd : dist100;
            }
            /* the middle: phrases do return, and it does not stay: a window of a few minutes always holds a repeat, and an
             * hour never the same phrase for long */
            long_run(m, 500u + sd, EVOLVE_MID, g, 1600u);
            {
                const uint32_t r = longest_repeat(g, 400u, 400u);
                uint32_t k, seen[256], nd = 0u;
                rep_mid = r > rep_mid ? r : rep_mid;
                CHECK(r >= 2u, "%s seed %u: the middle never repeats anything", m->name, sd);
                for (i = 0u; i < 1600u; i++) {
                    for (k = 0u; k < nd && seen[k] != chord_id(&g[i]); k++) {}
                    if (k == nd && nd < 256u)
                        seen[nd++] = chord_id(&g[i]);
                }
                dist_mid = nd > dist_mid ? nd : dist_mid;
                CHECK(nd >= 8u, "%s seed %u: only %u chords in an hour", m->name, sd, nd);
            }
            for (i = 0u; i + 40u < 1600u; i += 40u) {
                /* it moves on: no 40 chords all of the same few */
                uint32_t k, seen[16], nd = 0u;
                for (j = 0u; j < 40u; j++) {
                    for (k = 0u; k < nd && seen[k] != g[i + j].ch.root; k++) {}
                    if (k == nd && nd < 16u)
                        seen[nd++] = g[i + j].ch.root;
                }
                CHECK(nd >= 3u, "%s seed %u: 40 chords on only %u roots", m->name, sd, nd);
            }
        }
        printf("  %-10s longest stretch that returns, in chords: Evolve 0 %u (all of it); 50 %u; 100 %u within %d chords, %u at any distance. Distinct chords in an hour: Evolve 50 up to %u, Evolve 100 up to %u\n", m->name, rep0, rep_mid, rep_100, EN_GEN_WINDOW, rep_any, dist_mid, dist100);
        CHECK(rep_100 <= 2u, "%s: at 100 %u chords came again", m->name, rep_100);
    }
}

/* A phrase's return is heard as such: at the middle, most new phrases come again, varied or plainly */
static void phrase_checks(void)
{
    static en_gchord_t g[1600];
    uint32_t sd, lag_same = 0u, all = 0u;
    for (sd = 0u; sd < 40u; sd++) {
        uint32_t i;
        long_run(&MODES[3], 800u + sd, EVOLVE_MID, g, 1600u);
        /* chords that are what they were a phrase ago, for any of the phrase lengths */
        for (i = 4u; i < 1600u; i++) {
            uint32_t per, same = 0u;
            for (per = 2u; per <= 4u; per++)
                same |= g[i].ch.root == g[i - per].ch.root;
            lag_same += same;
            all++;
        }
    }
    printf("  the middle: %.0f%% of chords stand on the root they stood on a phrase ago\n", 100.0 * lag_same / all);
    CHECK(lag_same * 100u >= all * 50u, "the middle: phrases hardly return: %.0f%%", 100.0 * lag_same / all);
}

/* one seed, one result, the character felt; a pool change takes at once in the next chord */
static void seed_checks(void)
{
    const uint32_t lyd = MODES[3].pool, aeo = MODES[5].pool;
    uint32_t sd, differ = 0u;
    static en_gchord_t a[300], b[300];
    for (sd = 0u; sd < 40u; sd++) {
        long_run(&MODES[3], sd, EVOLVE_MID, a, 300u);
        long_run(&MODES[3], sd, EVOLVE_MID, b, 300u);
        CHECK(!memcmp(a, b, sizeof a), "seed %u: not repeatable", sd);
        long_run(&MODES[3], sd + 1u, EVOLVE_MID, b, 300u);
        differ += memcmp(a, b, sizeof a) != 0;
    }
    CHECK(differ == 40u, "only %u of 40 seeds differ", differ);
    /* the very next chord is in the new pool, whatever the memory held; the same with the key: its degrees carry over */
    for (sd = 0u; sd < 200u; sd++) {
        en_gen_t st;
        en_gchord_t g;
        int i;
        en_gen_start(&st);
        for (i = 0; i < 9 + (int)(sd % 7u); i++)
            en_gen_next(&st, lyd, 2, sd, sd % 2u ? 0 : EVOLVE_MID, 1, EN_MASK_ALL, &g);
        en_gen_next(&st, aeo, 2, sd, sd % 2u ? 0 : EVOLVE_MID, 1, EN_MASK_ALL, &g);
        CHECK(!((uint32_t)g.ch.tones & ~aeo) || g.ch.borrowed, "seed %u: the next chord still in the old pool", sd);
        for (i = 0; i < 12; i++) {
            en_gen_next(&st, aeo, 2, sd, sd % 2u ? 0 : EVOLVE_MID, 1, EN_MASK_ALL, &g);
            CHECK(!((uint32_t)g.ch.tones & ~aeo) || g.ch.borrowed, "seed %u: the memory brought back the old pool", sd);
        }
    }
    {
        /* a new key at Evolve 0: the phrase goes on, a key higher */
        en_gen_t s1, s2;
        en_gchord_t g1, g2;
        int i;
        en_gen_start(&s1);
        en_gen_start(&s2);
        for (i = 0; i < 40; i++) {
            en_gen_next(&s1, lyd, 2, 9u, 0, 1, EN_MASK_ALL, &g1);
            en_gen_next(&s2, lyd, i < 20 ? 2 : 7, 9u, 0, 1, EN_MASK_ALL, &g2);
            CHECK(g1.ch.root == g2.ch.root && g1.shape == g2.shape, "a new key: chord %d differs", i);
            voices_valid(lyd, i < 20 ? 2 : 7, &g2);
        }
    }
}

/* borrowed: rarely, gapped, gone again */
static void borrow_checks(void)
{
    static en_gchord_t g[3000];
    uint32_t sd, total = 0u, min_gap = 100000u;
    for (sd = 0u; sd < 30u; sd++) {
        uint32_t i, last = 0u;
        int any = 0;
        long_run(&MODES[0], 40u + sd, EVOLVE_MID, g, 3000u);
        for (i = 0u; i < 3000u; i++)
            if (g[i].ch.borrowed) {
                total++;
                if (any)
                    min_gap = i - last < min_gap ? i - last : min_gap;
                any = 1;
                last = i;
            }
        long_run(&MODES[6], 40u + sd, EVOLVE_MID, g, 1000u);
        for (i = 0u; i < 1000u; i++)
            CHECK(!g[i].ch.borrowed, "a borrowed chord in a pentatonic");
    }
    printf("  borrowed: %.1f%% of chords, at least %u apart\n", 100.0 * total / (30.0 * 3000.0), min_gap);
    CHECK(total > 0u, "no borrowed chords");
    CHECK(total * 100u <= 30u * 3000u * 3u, "too many borrowed: %u", total);
    CHECK(min_gap >= 40u, "borrowed only %u chords apart", min_gap);
}

/* the character: calm slow, the peak brighter, higher and quicker; going down leans home */
static void character_checks(void)
{
    uint32_t c;
    double bars[4], ninth[4], top[4], area[4];
    for (c = 0u; c < 4u; c++) {
        uint32_t sd, n = 0u, b = 0u, nine = 0u, ar = 0u;
        long tops = 0;
        for (sd = 0u; sd < 40u; sd++) {
            en_gen_t st;
            en_gchord_t g;
            uint32_t i;
            en_gen_start(&st);
            for (i = 0u; i < 400u; i++) {
                en_gen_next(&st, MODES[0].pool, 2, sd * 13u + 5u, EVOLVE_MID, (int)c, EN_MASK_ALL, &g);
                n++;
                b += g.bars;
                nine += g.shape == 1u || g.shape == 3u;
                tops += g.keys_v[3];
                ar += (g.ch.root == 4u || g.ch.root == 9u || !g.ch.root);
            }
        }
        bars[c] = (double)b / n;
        ninth[c] = (double)nine / n;
        top[c] = (double)tops / n;
        area[c] = (double)ar / n;
    }
    printf("  character 0..3: bars a chord %.2f %.2f %.2f %.2f; ninths %.0f%% %.0f%% %.0f%% %.0f%%; top note %.1f %.1f %.1f %.1f; tonic area %.0f%% %.0f%% %.0f%% %.0f%%\n", bars[0], bars[1], bars[2], bars[3], 100 * ninth[0], 100 * ninth[1],
           100 * ninth[2], 100 * ninth[3], top[0], top[1], top[2], top[3], 100 * area[0], 100 * area[1], 100 * area[2], 100 * area[3]);
    CHECK(bars[0] > bars[2] && bars[2] > bars[3], "lengths by character: %.2f %.2f %.2f", bars[0], bars[2], bars[3]);
    CHECK(bars[3] >= 2.0, "too quick at the peak: %.2f bars a chord", bars[3]);
    CHECK(ninth[3] > ninth[0] * 1.2, "the peak not brighter: %.2f %.2f", ninth[3], ninth[0]);
    CHECK(top[3] > top[2] + 1.0, "the peak not higher: %.1f %.1f", top[3], top[2]);
    CHECK(area[0] > area[3], "the calm not nearer home: %.2f %.2f", area[0], area[3]);
}

/* at Evolve 100 nothing comes back to bring it: chords on the tonic stay apart */
static void tonic_gap_checks(void)
{
    static en_gchord_t g[1600];
    int mi;
    for (mi = 0; mi < EN_NMODES; mi++) {
        uint32_t sd;
        for (sd = 0u; sd < 10u; sd++) {
            uint32_t i, last = 0u;
            int any = 0;
            long_run(&MODES[mi], 70u + sd, 100, g, 1600u);
            for (i = 0u; i < 1600u; i++)
                if (!g[i].ch.root) {
                    CHECK(!any || i - last >= 6u, "%s: at 100, tonic chords %u apart", MODES[mi].name, i - last);
                    any = 1;
                    last = i;
                }
            long_run(&MODES[mi], 70u + sd, 0, g, 200u);
            for (i = 0u; i < 200u; i++)
                CHECK(g[i].ch.root != 0u, "%s: at 0, a tonic chord for ever", MODES[mi].name);
        }
    }
}

/* the families, by name: what a colour comes to in the stock modes */
static void family_checks(void)
{
    static const struct {
        int mode, key, fam, root, shape;
        const char *name;
    } T[] = {
        {MODE_IONIAN, 2, EN_FAM_MAJOR, 5, SH_7, "GM7"},
        {MODE_IONIAN, 2, EN_FAM_MAJOR, 9, SH_9, "Bm9"},
        {MODE_IONIAN, 2, EN_FAM_MAJOR, 7, SH_ADD9, "Aadd9"},
        {MODE_IONIAN, 2, EN_FAM_MAJOR, 7, SH_13SUS, "A13sus"},
        {MODE_IONIAN, 2, EN_FAM_MAJOR, 2, SH_9, "Em9"},
        {MODE_IONIAN, 2, EN_FAM_MAJOR, 0, SH_7_3, "DM7"},
        {MODE_LYDIAN, 2, EN_FAM_MAJOR, 2, SH_69, "E6/9"},
        {MODE_MIXOLYDIAN, 2, EN_FAM_MAJOR, 10, SH_7, "CM7"},
        {MODE_DORIAN, 2, EN_FAM_MINOR, 3, SH_7, "FM7"},
        {MODE_AEOLIAN, 9, EN_FAM_MINOR, 5, SH_9, "Dm9"},
        {MODE_PHRYGIAN, 4, EN_FAM_MINOR, 5, SH_7, "Am7"},
        {MODE_PENT_MAJ, 2, EN_FAM_SPARSE, 7, SH_S2, "Asus2"},
        {MODE_PENT_MIN, 2, EN_FAM_SPARSE, 10, SH_S4_7, "Csus4"},
        {MODE_IONIAN, 2, EN_FAM_MAJOR, 0, SH_BORROW, "CM9"},
        {MODE_LYDIAN, 2, EN_FAM_MAJOR, 0, SH_BORROW, "GM9"},
        {MODE_MIXOLYDIAN, 2, EN_FAM_MAJOR, 0, SH_BORROW, "FM9"},
        {MODE_DORIAN, 2, EN_FAM_MINOR, 0, SH_BORROW, "BbM9"},
        {MODE_AEOLIAN, 9, EN_FAM_MINOR, 0, SH_BORROW, "Dadd9"},
        {MODE_PHRYGIAN, 4, EN_FAM_MINOR, 0, SH_BORROW, "Aadd9"},
    };
    unsigned t;
    char nm[24];
    for (t = 0u; t < sizeof T / sizeof T[0]; t++) {
        const uint32_t p = EN_MODE_POOL[T[t].mode];
        en_hchord_t c;
        CHECK(en_pool_family(p) == T[t].fam, "mode %d: family %d", T[t].mode, en_pool_family(p));
        CHECK(en_chord_make(p, T[t].root, T[t].shape, &c), "mode %d: no %s", T[t].mode, T[t].name);
        c.tonic = (uint8_t)T[t].key;
        CHECK(!strcmp(en_hchord_name(&c, nm), T[t].name), "mode %d root %d shape %d: %s, not %s", T[t].mode, T[t].root, T[t].shape, nm, T[t].name);
    }
    {
        en_hchord_t c;
        CHECK(!en_chord_make(EN_MODE_POOL[MODE_PENT_MAJ], 0, SH_BORROW, &c), "a borrowed chord in a pentatonic");
    }
    /* a custom pool goes by its third; no third, or under six notes: sparse */
    CHECK(en_pool_family(0xAB5u & ~(1u << 11)) == EN_FAM_MAJOR, "Ionian less its seventh");
    CHECK(en_pool_family(0x5ADu & ~(1u << 10)) == EN_FAM_MINOR, "Aeolian less its seventh");
    CHECK(en_pool_family(1u | 1u << 2 | 1u << 5 | 1u << 7 | 1u << 9 | 1u << 11) == EN_FAM_SPARSE, "no third");
    CHECK(en_pool_family(1u | 1u << 4 | 1u << 7) == EN_FAM_SPARSE, "three notes");
}

/* ---- the degrees: which notes may carry a chord (en_gen_next's mask) */
#define GOLDEN_ALL_ON 0x8993b33du                /* the generator before it had a mask, 0772c0a: all on must be that, to the note */
static void all_on_checks(void)
{
    static const int E[3] = {0, 50, 100}, K[2] = {0, 5};
    uint32_t h = 2166136261u;
    int m, k, s, e;
#define H(x) h = (h ^ (uint32_t)(x)) * 16777619u
    for (m = 0; m < EN_NMODES; m++)
        for (k = 0; k < 2; k++)
            for (s = 0; s < 3; s++)
                for (e = 0; e < 3; e++) {
                    en_gen_t st;
                    en_gchord_t g;
                    int i, v;
                    en_gen_start(&st);
                    for (i = 0; i < 300; i++) {
                        en_gen_next(&st, EN_MODE_POOL[m], K[k], (uint32_t)(11 + s * 97), E[e], (i / 5) & 3, EN_MASK_ALL, &g);
                        H(g.ch.root);
                        H(g.ch.bass);
                        H(g.shape);
                        H(g.bars);
                        H(g.ch.tones);
                        for (v = 0; v < 4; v++) {
                            H(g.keys_v[v]);
                            H(g.pad_v[v]);
                        }
                    }
                }
#undef H
    CHECK(h == GOLDEN_ALL_ON, "all degrees on: %08x, not what it was before there was a mask (%08x)", h, GOLDEN_ALL_ON);
}

/* a pool and a mask: every chord on an allowed root over an allowed bass, the tonic out of the bass, no chord twice; too few
 * roots left, and it is what all on is, note for note */
static void mask_one(uint32_t p, uint32_t m, int key, uint32_t seed, int evolve, uint32_t chords, uint32_t *seen_out)
{
    en_gen_t st, all;
    uint32_t i, seen = 0u;
    const int usable = en_mask_usable(p, m);
    const uint32_t eff = en_mask_effective(p, m);
    CHECK(usable ? eff == (m & 0xFFFu) : eff == EN_MASK_ALL, "pool %03x mask %03x: it uses %03x", p, m, eff);
    CHECK(usable == (pop(en_gen_roots_in(p, m)) >= 2), "pool %03x mask %03x: usable says %d", p, m, usable);
    en_gen_start(&st);
    en_gen_start(&all);
    for (i = 0u; i < chords; i++) {
        en_gchord_t g, a;
        const int prev_root = st.prev_root;
        const int ch = (int)(i / 4u) & 3;
        en_gen_next(&st, p, key, seed, evolve, ch, m, &g);
        chord_valid(p, &g);
        CHECK(eff >> g.ch.root & 1u, "pool %03x mask %03x key %d: a chord on %d", p, m, key, g.ch.root);
        CHECK(eff >> g.ch.bass & 1u, "pool %03x mask %03x key %d: root %d over a bass on %d", p, m, key, g.ch.root, g.ch.bass);
        if (i && pop(en_gen_roots_in(p, eff)) >= 2)
            CHECK(g.ch.root != prev_root, "pool %03x mask %03x key %d: the same root twice", p, m, key);
        seen |= 1u << g.ch.root;
        if (!usable) {
            en_gen_next(&all, p, key, seed, evolve, ch, EN_MASK_ALL, &a);
            CHECK(a.ch.root == g.ch.root && a.shape == g.shape && a.bars == g.bars, "pool %03x mask %03x: too few, and yet not all on", p, m);
        }
    }
    if (usable)
        CHECK(pop(seen & ~1u) >= 2 || chords < 16u, "pool %03x mask %03x: only roots %03x came", p, m, seen);
    if (seen_out)
        *seen_out = seen;
}

static void mask_checks(void)
{
    uint32_t refused = 0u, tried = 0u;
    int mo;
    /* every subset of every mode's notes, in every key */
    for (mo = 0; mo < EN_NMODES; mo++) {
        const uint32_t p = EN_MODE_POOL[mo];
        uint32_t m = p;
        for (;;) {                               /* (the subsets of p) */
            int key;
            for (key = 0; key < 12; key += 5) {
                mask_one(p, m, key, 40u + (uint32_t)key, (key % 3) * 50, 40u, 0);
                tried++;
            }
            refused += !en_mask_usable(p, m);
            if (!m)
                break;
            m = (m - 1u) & p;
        }
    }
    /* custom pools, all sorts of masks over them, with bits outside the pool as well: those do not count */
    {
        uint32_t x = 99u, i;
        for (i = 0u; i < 1500u; i++) {
            uint32_t p, m;
            x = x * 1664525u + 1013904223u;
            p = (x >> 8 | 1u) & 0xFFFu;
            x = x * 1664525u + 1013904223u;
            m = (x >> 8) & 0xFFFu;
            if (pop(p) < 3)
                continue;
            mask_one(p, m, (int)(x >> 28) % 12, x >> 4, (int)(i % 3u) * 50, 30u, 0);
            CHECK(en_mask_usable(p, m) == en_mask_usable(p, m | ~p), "pool %03x mask %03x: a bit outside the pool counted", p, m);
        }
    }
    printf("  degrees: %u mode, key and mask cases right, %u masks of the modes leave too few\n", tried, refused);

    /* the tonic: turned off, no glimpses of it; on, they come, over an allowed bass only */
    {
        const uint32_t p = EN_MODE_POOL[MODE_IONIAN];
        uint32_t seen;
        mask_one(p, EN_MASK_ALL, 2, 7u, 100, 600u, &seen);
        CHECK(seen & 1u, "Ionian, all on: no glimpse of the tonic in 600 chords");
        mask_one(p, EN_MASK_ALL & ~1u, 2, 7u, 100, 600u, &seen);
        CHECK(!(seen & 1u), "the tonic off, and a chord on it");
        /* the tonic's chords stand over its third or its fifth: with those off it is the tonic that has to wait */
        mask_one(p, EN_MASK_ALL & ~(1u << 4 | 1u << 7), 2, 7u, 100, 600u, &seen);
        CHECK(!(seen & 1u), "the tonic's third and fifth off, and a chord on the tonic");
        /* the tonic does not count towards the two: the tonic and one more is too few */
        CHECK(!en_mask_usable(p, 1u | 1u << 7), "the tonic and one more counted as two");
        CHECK(en_mask_usable(p, 1u << 5 | 1u << 7), "two without the tonic did not");
    }
    /* a note off stays in the scale: the chords may still have it, only not stand on it */
    {
        const uint32_t p = EN_MODE_POOL[MODE_LYDIAN], m = EN_MASK_ALL & ~(1u << 9);
        en_gen_t st;
        en_gchord_t g;
        uint32_t i, had = 0u;
        en_gen_start(&st);
        for (i = 0u; i < 200u; i++) {
            en_gen_next(&st, p, 2, 5u, 100, 1, m, &g);
            had += g.ch.tones >> 9 & 1u;
        }
        CHECK(had > 10u, "the sixth off: only %u chords of 200 have it as a tone", had);
    }
    /* what it remembers: at Evolve 0 the phrase plays for ever; a note off leaves the memory at the next chord, the rest of
     * the phrase stays, and it does not come back by itself */
    {
        int sd;
        for (sd = 0; sd < 40; sd++) {
            const uint32_t p = EN_MODE_POOL[MODE_LYDIAN];
            en_gen_t st;
            en_gchord_t g;
            uint32_t i, m;
            int n = 0, off, kept_before = 0, kept_after = 0;
            en_gen_start(&st);
            for (i = 0u; i < 16u; i++)
                en_gen_next(&st, p, 2, 500u + (uint32_t)sd, 0, 1, EN_MASK_ALL, &g);
            n = st.ph_i;
            off = st.phrase[0].root;                 /* a chord the phrase has */
            for (i = 1u; i < 4u; i++)
                kept_before += st.phrase[i].root != off;
            m = EN_MASK_ALL & ~(1u << off);
            en_gen_next(&st, p, 2, 500u + (uint32_t)sd, 0, 1, m, &g);
            CHECK(m >> g.ch.root & 1u, "seed %d: a chord on %d at once after it went off", sd, off);
            for (i = 0u; i < 4u; i++) {
                CHECK(st.phrase[i].root == EN_GEN_GONE || st.phrase[i].root != off, "seed %d: %d still in the memory", sd, off);
                kept_after += st.phrase[i].root != EN_GEN_GONE;
            }
            CHECK(kept_after >= 1, "seed %d: the whole memory went for one note (%d before, %d phrase places)", sd, kept_before, n);
            for (i = 0u; i < 60u; i++) {
                en_gen_next(&st, p, 2, 500u + (uint32_t)sd, 0, 1, m, &g);
                CHECK(g.ch.root != off, "seed %d: %d came back", sd, off);
            }
            /* on again: free to come, nothing forced */
            en_gen_next(&st, p, 2, 500u + (uint32_t)sd, 0, 1, EN_MASK_ALL, &g);
            CHECK(st.mask == EN_MASK_ALL, "seed %d: the mask did not come back on", sd);
        }
    }
    /* a new key: the same degrees (relative, so it survives), a removed note's mark stays for its return */
    {
        const uint32_t ion = EN_MODE_POOL[MODE_IONIAN], m = EN_MASK_ALL & ~(1u << 5);
        en_gen_t st;
        en_gchord_t g;
        int key;
        en_gen_start(&st);
        for (key = 0; key < 120; key++) {
            en_gen_next(&st, ion, key % 12, 3u, 50, 1, m, &g);
            CHECK(g.ch.root != 5 && g.ch.tonic == key % 12, "key %d: a chord on the off degree, or in the wrong key", key % 12);
        }
        /* the fourth out of the scale: then back; the mark still says off */
        for (key = 0; key < 40; key++)
            en_gen_next(&st, ion & ~(1u << 5), 2, 3u, 50, 1, m, &g);
        for (key = 0; key < 60; key++) {
            en_gen_next(&st, ion, 2, 3u, 50, 1, m, &g);
            CHECK(g.ch.root != 5, "the fourth came back to the scale, and with it a chord on it");
        }
    }
    /* two roots: they take turns, each chord in its own right */
    {
        const uint32_t p = EN_MODE_POOL[MODE_DORIAN], m = 1u << 5 | 1u << 10;
        uint32_t seen;
        mask_one(p, m, 7, 9u, 50, 200u, &seen);
        CHECK(seen == m, "two roots on: %03x came", seen);
    }
}

/* the engine: the mask changes at the very last moment before a chord, and that chord obeys; none before it moves */
static uint32_t step_at[64];
static int root_at[64], n_at;
static void note_chord(uint32_t step, const en_gchord_t *g, int character)
{
    (void)character;
    if (n_at < 64) {
        step_at[n_at] = step;
        root_at[n_at++] = g->ch.root;
    }
}
static void render_to(uint32_t bars)
{
    static int32_t b[64];
    while (en_bars < bars)
        en_render(b, 8, 1024);
}
static void engine_checks(void)
{
    static const uint32_t BACK[5] = {8u, 4u, 2u, 1u, 0u};  /* 0: the last step has run, the chord's has not */
    static const int K[3] = {5, 9, 14};
    int b, k;
    for (k = 0; k < 3; k++)
        for (b = 0; b < 5; b++) {
            int was_n, off, c;
            static uint32_t first_step[64];
            static int first_root[64];
            en_trace_chord = 0;
            en_init();
            en_seed(700u + (uint32_t)k);
            en_trace_chord = note_chord;
            n_at = 0;
            en_play(1);
            render_to(2600u);
            was_n = n_at;
            CHECK(was_n > K[k] + 1, "only %d chords", was_n);
            for (c = 0; c < was_n; c++) {
                first_step[c] = step_at[c];
                first_root[c] = root_at[c];
            }
            en_trace_chord = 0;
            en_init();
            en_seed(700u + (uint32_t)k);
            en_trace_chord = note_chord;
            n_at = 0;
            en_play(1);
            off = first_root[K[k]];
            render_to(first_step[K[k]] - BACK[b]);
            CHECK(n_at == K[k], "the chords before: %d, not %d", n_at, K[k]);
            en_mask(EN_MASK_ALL & ~(1u << off));
            render_to(first_step[K[k]] + 2u);
            for (c = 0; c < K[k]; c++)
                CHECK(root_at[c] == first_root[c] && step_at[c] == first_step[c], "chord %d moved, before the change", c);
            CHECK(n_at == K[k] + 1 && step_at[K[k]] == first_step[K[k]], "the change's chord did not come on time");
            CHECK(root_at[K[k]] != off, "the next chord is on %d, which went off %u steps before it", off, BACK[b]);
            en_trace_chord = 0;
        }
}

int main(void)
{
    all_on_checks();
    mask_checks();
    engine_checks();
    family_checks();
    pool_checks();
    tonic_gap_checks();
    mode_checks();
    evolve_checks();
    phrase_checks();
    seed_checks();
    borrow_checks();
    character_checks();
    printf(fails ? "gen: %d FAILED\n" : "gen: ok\n", fails);
    return fails != 0;
}
