/* SPDX-License-Identifier: MPL-2.0 */
/* The growing harmony and the living drums: both pure functions of a seed, so both can be read and checked. All of it over
 * the demo's song as a fixed script (demo_script.h): what the arranger does with them is arr_test.c's. */
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "amb.h"
#include "brk.h"
#include "demo_script.h"

static int fails;
#define CHECK(c, ...)                                                                                                  \
    do {                                                                                                               \
        if (!(c)) {                                                                                                    \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);                                                                \
            printf(__VA_ARGS__);                                                                                       \
            printf("\n");                                                                                              \
            fails++;                                                                                                   \
        }                                                                                                              \
    } while (0)
static void ev_note(uint32_t st, uint32_t d1);
static uint32_t golden, golden_on;
void en_midi_out(uint32_t st, uint32_t d1, uint32_t d2)
{
    if (golden_on && en_bars < 2752u) {
        golden = (golden ^ en_bars) * 16777619u;
        golden = (golden ^ st) * 16777619u;
        golden = (golden ^ d1) * 16777619u;
        golden = (golden ^ d2) * 16777619u;
    }
    ev_note(st, d1);
}

#define NCYCLES 11u                              /* the script's */
#define NSEEDS 300u

/* the script's character at a cycle's seam */
static int script_level(uint32_t cycle)
{
    int i = 0;
    while (i + 1 < DEMO_SCRIPT_N && DEMO_SCRIPT[i + 1].step <= cycle * 256u)
        i++;
    return DEMO_SCRIPT[i].character;
}

/* the walk replayed: level is this cycle's; the ones before it come from the script */
static void en_cycle_chords(uint32_t cycle, uint32_t seed, int level, uint8_t *out)
{
    en_harm_t st;
    uint32_t c;
    for (c = 0u; c <= cycle; c++)
        en_harm_step(&st, c, seed, c == cycle ? level : script_level(c));
    memcpy(out, st.chord, 4);
}

static void en_cycle_voicing(uint32_t cycle, uint32_t seed, int level, int root, uint8_t keys[4][4], uint8_t pad[4][4])
{
    const uint32_t p = 0xAB5u;
    uint8_t hk[4][4], hp[4][4];
    en_harm_t st, was;
    en_hchord_t ch[4];
    uint32_t c;
    en_home_voicing(p, root, hk, hp);
    memcpy(keys, hk, sizeof hk);
    memcpy(pad, hp, sizeof hp);
    en_harm_step(&st, 0u, seed, cycle == 0u ? level : script_level(0u));
    for (c = 1u; c <= cycle; c++) {
        was = st;
        en_harm_walk(&st, p, root, c, seed, c == cycle ? level : script_level(c), ch);
        en_voice_cycle(&was, &st, ch, 0, root, (const uint8_t (*)[4])hk, (const uint8_t (*)[4])hp, keys, pad);
    }
}

static int popcount(uint32_t m)
{
    int n = 0;
    for (; m; m &= m - 1u)
        n++;
    return n;
}

static void harmony_checks(void)
{
    uint32_t sd, c;
    int id, s, v, distinct = 0;
    uint8_t first[NCYCLES][4];

    /* the graph: a substitution shares at least two tones; D is never in the bass */
    for (id = 0; id < en_harm_count(); id++) {
        CHECK(en_harm_bass(id) != 0, "%s has the tonic in the bass", en_harm_name(id));
        CHECK(en_harm_colour(id) >= 0, "%s has no colour", en_harm_name(id));
        if (en_harm_sub(id) >= 0)
            CHECK(popcount(en_harm_tones(id) & en_harm_tones(en_harm_sub(id))) >= 2, "%s to %s shares too little",
                  en_harm_name(id), en_harm_name(en_harm_sub(id)));
    }

    for (sd = 0u; sd < NSEEDS; sd++) {
        uint8_t seq[NCYCLES][4], ks[NCYCLES][4][4], ps[NCYCLES][4][4];
        for (c = 0u; c < NCYCLES; c++) {
            const int lv = script_level(c);
            uint8_t again[4];
            en_cycle_chords(c, sd, lv, seq[c]);
            en_cycle_chords(c, sd, lv, again);
            CHECK(!memcmp(seq[c], again, 4), "seed %u cycle %u: not repeatable", sd, c);
            en_cycle_voicing(c, sd, lv, 2, ks[c], ps[c]);
            for (s = 0; s < 4; s++) {
                CHECK(en_harm_bass(seq[c][s]) != 0, "seed %u cycle %u: tonic in the bass", sd, c);
                for (v = 0; v < 4; v++) {
                    CHECK(ks[c][s][v] >= 54 && ks[c][s][v] <= 77, "seed %u cycle %u: keys note %d", sd, c, ks[c][s][v]);
                    CHECK(ps[c][s][v] >= 51 && ps[c][s][v] <= 69, "seed %u cycle %u: pad note %d", sd, c, ps[c][s][v]);
                    if (v) {
                        CHECK(ks[c][s][v] != ks[c][s][v - 1], "seed %u cycle %u: a unison in the hand", sd, c);
                        CHECK(ps[c][s][v] != ps[c][s][v - 1], "seed %u cycle %u: a unison in the pad", sd, c);
                    }
                }
            }
        }
        /* every motif note sits in the chord that sounds, and its first two echoes in the same chord */
        for (c = 0u; c < NCYCLES; c++) {
            int m, n;
            for (m = 0; m < 2; m++)
                for (n = 0; n < 4; n++) {
                    const int st = en_motif_step(m, n), slot = st / 64;
                    const int note = en_motif_note(m, n, seq[c]);
                    CHECK((st + 2 * EN_ECHO_STEPS) / 64 == slot, "motif %d note %d: its echo leaves its chord", m, n);
                    CHECK(en_harm_tones(seq[c][slot]) >> ((note + 120) % 12) & 1u, "seed %u cycle %u: motif %d note %d not in %s", sd,
                          c, m, n, en_harm_name(seq[c][slot]));
                }
        }
        /* it starts at home and comes home by the end, voicing and all */
        for (s = 0; s < 4; s++) {
            CHECK(seq[0][s] == en_harm_home(s), "seed %u: not home at the start", sd);
            CHECK(seq[NCYCLES - 1u][s] == en_harm_home(s), "seed %u: not home at the end", sd);
            CHECK(!memcmp(ks[0][s], ks[NCYCLES - 1u][s], 4), "seed %u: the home voicing did not come back", sd);
        }
        /* a little at a time: at most two chords a cycle; a recolouring moves at most two voices more than a tone;
         * the exception is entering the second cycle */
        for (c = 1u; c < NCYCLES; c++) {
            int diff = 0;
            const int enter = script_level(c) == 3 && script_level(c - 1u) != 3;
            for (s = 0; s < 4; s++)
                if (seq[c][s] != seq[c - 1u][s]) {
                    diff++;
                    if (en_harm_plain(seq[c][s]) == en_harm_plain(seq[c - 1u][s]) && !enter) {
                        int far = 0;
                        for (v = 0; v < 4; v++) {
                            const int d = ks[c][s][v] - ks[c - 1u][s][v];
                            far += d > 2 || d < -2;
                        }
                        CHECK(far <= 2, "seed %u cycle %u slot %d: %d voices moved more than a tone", sd, c, s, far);
                    }
                }
            if (enter)
                for (s = 0; s < 4; s++)
                    CHECK(seq[c][s] == en_harm_section(s), "seed %u: the second cycle", sd);
            else
                CHECK(diff <= 2, "seed %u cycle %u: %d chords changed", sd, c, diff);
        }
        if (sd == 0u)
            memcpy(first, seq, sizeof first);
        else
            distinct += memcmp(first, seq, sizeof first) != 0;
    }
    CHECK(distinct > (int)NSEEDS * 9 / 10, "only %d of %u seeds differ from seed 0", distinct, NSEEDS - 1u);
    printf("  harmony: %d of %u other seeds differ from seed 0\n", distinct, NSEEDS - 1u);
}

/* the engine really plays that walk, and moves only at the seam */
static void engine_checks(void)
{
    static int32_t buf[512];
    uint8_t now[4], want[4];
    uint32_t moves = 0u, frames = 0u, last_chord = 0u, c0 = 0u, s0 = 0u, n0 = 0u;
    uint32_t chords[4] = {0}, attacks[4] = {0}, singles[4] = {0}, notes[4] = {0};
    int s, l;
    en_init();
    en_test_script(DEMO_SCRIPT, DEMO_SCRIPT_N);
    en_play(1);
    en_render(buf, 256, 1024);
    memcpy(now, (const void *)en_cycle_now, 4);
    c0 = en_ep_chords;
    n0 = en_ep_notes;
    while (frames < 44100u * 248u) {
        en_render(buf, 256, 1024);
        frames += 256u;
        /* the piano flows: at most one chord's attack a change (two at level 3), three lone notes a chord */
        if ((en_bars - 1u) / 64u != last_chord) {
            const int lv = script_level(last_chord / 4u);
            const uint32_t a = en_ep_chords - c0, b = en_ep_singles - s0;
            CHECK(a <= (lv == 3 ? 2u : 1u), "level %d chord %u: %u chord attacks", lv, last_chord, a);
            CHECK(b <= 3u, "level %d chord %u: %u lone notes", lv, last_chord, b);
            chords[lv]++;
            attacks[lv] += a;
            singles[lv] += b;
            notes[lv] += en_ep_notes - n0;
            c0 = en_ep_chords;
            s0 = en_ep_singles;
            n0 = en_ep_notes;
            last_chord = (en_bars - 1u) / 64u;
        }
        if (memcmp(now, (const void *)en_cycle_now, 4)) {
            const uint32_t steps = en_bars - 1u, cycle = steps / 256u;
            moves++;
            CHECK(steps % 256u == 0u, "the chords moved at step %u, off the seam", steps);
            en_cycle_chords(cycle, EN_HARM_SEED, script_level(cycle), want);
            memcpy(now, (const void *)en_cycle_now, 4);
            for (s = 0; s < 4; s++)
                CHECK(now[s] == want[s], "cycle %u slot %d: plays %s, the walk says %s", cycle, s, en_harm_name(now[s]),
                      en_harm_name(want[s]));
        }
    }
    CHECK(moves >= 5u, "only %u moves", moves);
    printf("  engine: %u moves, all on the seam\n", moves);
    for (l = 0; l < 4; l++)
        if (chords[l])
            printf("  piano level %d: %.2f chord attacks, %.2f lone notes, %.2f notes struck, a chord\n", l,
                   (double)attacks[l] / (double)chords[l], (double)singles[l] / (double)chords[l],
                   (double)notes[l] / (double)chords[l]);
    printf("  piano: peak %d voices, %u stolen while sounding\n", en_ep_peak, en_ep_steals);
    CHECK(en_ep_steals == 0u, "%u voices stolen while sounding", en_ep_steals);
    CHECK(en_ep_peak <= 12, "peak %d voices", en_ep_peak);
}

/* the demo as the owner approved it: the fixed song, seed and defaults, must play every note and clock it played when it
 * was a fixed song (commit 4e2387c, the last of them; its audio was compared with this, sample for sample, by hand) */
static void golden_checks(void)
{
    static int32_t buf[512];
    en_init();
    en_test_script(DEMO_SCRIPT, DEMO_SCRIPT_N);
    golden = 2166136261u;
    golden_on = 1u;
    en_play(1);
    while (en_bars < 2752u)
        en_render(buf, 256, 1024);
    golden_on = 0u;
    en_play(0);
    en_render(buf, 256, 1024);
    CHECK(golden == 0x4B17C9A8u, "the demo's notes changed: %08x", golden);
}

/* the bass: long notes, on the chord, never landing on the tonic, moving a little */
static void bass_checks(void)
{
    uint32_t sd, c, cs;
    int s;
    static const int SETS[4] = {EN_BASS_LIGHT, EN_BASS_FULL, EN_BASS_ROLL, EN_BASS_LONG};
    int set;
    for (set = 0; set < 4; set++) {
        const int level = SETS[set];
        uint32_t notes = 0u, chords_n = 0u, steps = 0u, held = 0u, leads = 0u, seen = 0u;
        for (sd = 0u; sd < NSEEDS; sd++)
            for (c = 0u; c < NCYCLES - 1u; c++) {
                uint8_t q[4], nx[4];
                en_cycle_chords(c, sd, script_level(c), q);
                en_cycle_chords(c + 1u, sd, script_level(c + 1u), nx);
                for (s = 0; s < 4; s++) {
                    const int next = s < 3 ? q[s + 1] : nx[0];
                    chords_n++;
                    for (cs = 0u; cs < 64u; cs++) {
                        int len = 0;
                        const int off = en_bass_at(q[s], next, c, cs, level, sd, &len);
                        if (off < 0)
                            continue;
                        notes++;
                        steps += (uint32_t)len;
                        held += (uint32_t)(len > 0 ? len : 0);
                        CHECK((off + 120) % 12 != 0, "seed %u: the tonic in the bass", sd);
                        CHECK(off >= 3 && off <= 22, "seed %u: bass note %d out of its range", sd, off);
                        CHECK(len >= 2 && (uint32_t)len + cs <= 64u, "seed %u: a note of %d at %u", sd, len, cs);
                        if (cs == 0u)
                            CHECK(off == (en_harm_bass(q[s]) < 4 ? en_harm_bass(q[s]) + 12 : en_harm_bass(q[s])), "the chord's first note is its root");
                        if (cs >= 62u) {
                            leads++;
                        } else {
                            CHECK(en_harm_tones(q[s]) >> ((off + 120) % 12) & 1u, "seed %u: %d not on %s", sd, off, en_harm_name(q[s]));
                        }
                        seen |= 1u << ((off + 120) % 12);
                    }
                }
            }
        printf("  bass level %d: %.2f notes a bar, %u leading tones in %u chords\n", level,
               (double)notes * 16.0 / ((double)chords_n * 64.0), leads, chords_n);
        CHECK((double)notes * 16.0 / ((double)chords_n * 64.0) <= (level == EN_BASS_ROLL ? 3.25 : level == EN_BASS_FULL ? 3.0 : 2.25), "level %d: too many notes", level);
        CHECK(steps == chords_n * 64u, "level %d: the notes do not fill the time: %u of %u steps", level, steps, chords_n * 64u);
        CHECK(leads > 0u && held > 0u && seen, "level %d: nothing moves", level);
    }
}

/* the drums: a plain two-step, breaks now and then, back on the grid; deterministic for a seed */
#define DSTEPS 6400u
static char core_a[DSTEPS], top_a[DSTEPS], hat_a[DSTEPS];
static uint8_t brk_in_bar[DSTEPS / 16u];
static void drums(uint32_t seed, int level, char *c, char *t, char *h)
{
    uint32_t i;
    brk_init();
    brk_levels(1.0f, 1.0f, 1.0f);
    for (i = 0u; i < DSTEPS; i++) {
        if (!(i & 15u)) {
            uint32_t in = 0u;
            const int want = en_arr_break(i / 16u, level, EN_ST_LIQUID, en_param_info(P_BREAKS)->def, seed, &in);
            brk_drums_for(en_style_rhythm(EN_ST_LIQUID), level, en_param_info(P_ENERGY)->def, seed, i / 16u, want, in);
            brk_in_bar[i / 16u] = (uint8_t)want;
        }
        brk_pattern_step(i, level, 0, seed);
        c[i] = brk_hit_core;
        t[i] = brk_hit_top;
        h[i] = brk_hat_hit;
    }
}
static int is_ghost(char c) { return c == 'g' || c == 'f' || c == 'e'; }

static void drum_checks(void)
{
    static char c2[DSTEPS], t2[DSTEPS], h2[DSTEPS];
    int level;
    uint32_t i;
    for (level = 2; level <= 3; level++) {
        uint32_t breaks = 0u, bbars = 0u, plain_k = 0u, plain_s = 0u, b;
        int diff = 0, prev = -1;
        drums(1234u, level, core_a, top_a, hat_a);
        drums(1234u, level, c2, t2, h2);
        CHECK(!memcmp(core_a, c2, DSTEPS) && !memcmp(top_a, t2, DSTEPS) && !memcmp(hat_a, h2, DSTEPS),
              "level %d: same seed, other drums", level);
        for (i = 0u; i < DSTEPS; i++) {
            const uint32_t bs = i % 16u;
            const int in = brk_in_bar[i / 16u] ? 0 : -1;
            const char c = core_a[i];
            CHECK(c != 0 && top_a[i] != 0 && hat_a[i] != 0, "a pattern too short at %u", i);
            if (in < 0) {                        /* plain: exactly the skeleton */
                if (bs == 0u)
                    CHECK(c == 'K', "plain bar: no kick at %u", i);
                else if (bs == 10u)
                    CHECK(c == 'k', "plain bar: no kick at %u", i);
                else if (bs == 4u || bs == 12u)
                    CHECK(c == 'S' || c == 's', "plain bar: no snare at %u", i);
                else
                    CHECK(c == '.', "plain bar: %c off the skeleton at %u", c, i);
                plain_k += c == 'K' || c == 'k';
                plain_s += c == 'S' || c == 's';
                if (prev >= 0)
                    CHECK(i % 128u == 0u, "a break ended off the block's edge at %u", i);
            } else {
                if (prev < 0) {
                    CHECK(bs == 0u, "a break began off the bar at %u", i);
                    breaks++;
                }
                if (bs == 0u)
                    bbars++;
            }
            prev = in;
        }
        /* a break's bar is busier but bounded, and they are occasional */
        for (b = 0u; b + 16u <= DSTEPS; b += 16u) {
            uint32_t hits = 0u, ghosts = 0u, j;
            for (j = 0u; j < 16u; j++) {
                hits += core_a[b + j] != '.';
                ghosts += is_ghost(top_a[b + j]);
            }
            CHECK(hits <= 8u, "level %d: %u hits in a bar", level, hits);
            CHECK(ghosts <= 5u, "level %d: %u ghosts in a bar", level, ghosts);
        }
        printf("  drums level %d: %u breaks, %u bars of break in %u bars; plain %u kicks, %u snares (seed 1234)\n", level, breaks, bbars,
               DSTEPS / 16u, plain_k, plain_s);
        CHECK(breaks > 0u, "level %d: no breaks", level);
        CHECK(bbars * 3u <= DSTEPS / 16u, "level %d: %u of %u bars are breaks", level, bbars, DSTEPS / 16u);
        drums(99u, level, c2, t2, h2);
        for (i = 0u; i < DSTEPS; i++)
            diff += core_a[i] != c2[i] || top_a[i] != t2[i];
        CHECK(diff > 0, "level %d: two seeds, the same drums", level);
    }
    drums(1234u, 1, core_a, top_a, hat_a);
    for (i = 0u; i < DSTEPS; i++) {
        CHECK(core_a[i] == '.', "level 1 has a kick or a snare at %u", i);
        CHECK(!is_ghost(top_a[i]), "level 1 has a ghost at %u", i);
    }
}

/* the hats alone, as they play at 0:23 (the reverb and the echo open): dry and short */
static void hat_checks(void)
{
    static int32_t buf[512];
    static float mono[44100 * 40];
    const double st = 44100.0 * 60.0 / 170.0 / 4.0;
    uint32_t i = 0u, s;
    double peak_sum = 0.0, tail_sum = 0.0;
    int n = 0;
    en_play(0);                                  /* from a standstill: the clock's pulse starts at its top */
    en_render(buf, 256, 1024);
    en_init();
    en_test_script(DEMO_SCRIPT, DEMO_SCRIPT_N);
    en_set(P_PAD, 0);
    en_set(P_BELLS, 0);
    en_set(P_BASS, 0);
    en_set(P_SWELL, 0);
    en_play(1);
    while (i + 256u < 44100u * 33u) {
        int j;
        en_render(buf, 256, 1024);
        for (j = 0; j < 256; j++)
            mono[i + (uint32_t)j] = (float)(buf[2 * j] + buf[2 * j + 1]) * (1.0f / 16777216.0f);
        i += 256u;
    }
    /* level 1's hats: steps 2, 6, 10, 14 of a bar, between 0:24 and 0:33: 6 steps... the next hit is 4 steps off */
    for (s = 272u; s < 370u; s++) {
        if (s % 4u == 2u) {
            const uint32_t i0 = (uint32_t)((double)s * st);
            double p = 0.0, t = 0.0;
            uint32_t k;
            for (k = i0; k < i0 + 1100u; k++)    /* the hit: up to 25 ms, with its swing */
                p = fabs((double)mono[k]) > p ? fabs((double)mono[k]) : p;
            for (k = i0 + 2646u; k < i0 + 3528u; k++)   /* its tail: 60 to 80 ms later */
                t += (double)mono[k] * (double)mono[k];
            peak_sum += p;
            tail_sum += sqrt(t / 882.0);
            n++;
        }
    }
    printf("  hats: %d hits, tail at 60-80 ms %.1f dB under the peak, reverb and echo open\n", n,
           20.0 * log10(peak_sum / (tail_sum + 1e-12)));
    CHECK(n > 10, "too few hats");
    CHECK(peak_sum > 0.001 * n, "the hats are silent: %f", peak_sum / n);
    CHECK(peak_sum > 56.0 * tail_sum, "the hats ring: %f against %f", peak_sum, tail_sum);   /* 35 dB */
    CHECK(en_hat_peak <= 2, "%d hat voices", en_hat_peak);
    CHECK(en_hat_peak >= 1, "no hat voices");
}

/* ---- the harmony from the pool ---- */
#define POOL_D_IONIAN 0xAB5u
static int pool_size(uint32_t p) { return popcount(p & 0xFFFu); }

/* one chord in one pool: what may stand in it */
static void chord_valid(uint32_t p, int id, const en_hchord_t *c)
{
    const uint32_t out = (uint32_t)c->tones & ~p;
    int v, iv;
    CHECK(c->root < 12 && c->bass < 12 && c->col < 12, "pool %03x id %d: out of range", p, id);
    if (c->borrowed) {
        CHECK(c->family != EN_FAM_SPARSE, "pool %03x: a borrowed chord in the sparse family", p);
        CHECK(id == 16, "pool %03x id %d: borrowed, not the borrowed place", p, id);
        CHECK(popcount(out) >= 1 && popcount(out) <= 2, "pool %03x: %d notes borrowed", p, popcount(out));
    } else {
        CHECK(!out, "pool %03x id %d: tones %03x outside", p, id, out);
        CHECK(p >> c->col & 1u, "pool %03x id %d: colour %d outside", p, id, c->col);
    }
    for (iv = 0; iv < 12; iv++)
        if (c->tones >> ((c->root + iv) % 12) & 1u)
            CHECK(iv != 1 && iv != 6 && iv != 8, "pool %03x id %d: a tone %d above the root", p, id, iv);
    CHECK(c->col != (c->root + 1) % 12, "pool %03x id %d: its colour a minor ninth", p, id);
    CHECK(popcount(c->tones) >= 2, "pool %03x id %d: %d tones", p, id, popcount(c->tones));
    CHECK(c->bass != 0, "pool %03x id %d: the tonic in the bass", p, id);
    for (v = 0; v < 4; v++)
        CHECK((unsigned)c->keys[v] < 12u && (unsigned)c->pad[v] < 12u, "pool %03x id %d: a voice out of range", p, id);
}

static void voices_valid(uint32_t p, int root, const uint8_t k[4][4], const uint8_t q[4][4], const en_hchord_t *ch, uint32_t cycle)
{
    int s, v;
    for (s = 0; s < 4; s++)
        for (v = 0; v < 4; v++) {
            /* the hand 52..75 and the pad 49..67 above the root's C (the pad, if the pool leaves it too few notes, to 72) */
            CHECK(k[s][v] >= 52 + root && k[s][v] <= 75 + root, "pool %03x root %d cycle %u: keys note %d", p, root, cycle, k[s][v]);
            CHECK(q[s][v] >= 49 + root && q[s][v] <= 72 + root, "pool %03x root %d cycle %u: pad note %d", p, root, cycle, q[s][v]);
            CHECK(ch[s].tones >> (((k[s][v] - root) % 12 + 12) % 12) & 1u, "pool %03x root %d: keys note %d not in the chord", p, root, k[s][v]);
            CHECK(ch[s].tones >> (((q[s][v] - root) % 12 + 12) % 12) & 1u, "pool %03x root %d: pad note %d not in the chord", p, root, q[s][v]);
            if (v) {
                CHECK(k[s][v] > k[s][v - 1], "pool %03x root %d cycle %u slot %d: a unison in the hand", p, root, cycle, s);
                CHECK(q[s][v] > q[s][v - 1], "pool %03x root %d cycle %u slot %d: a unison in the pad", p, root, cycle, s);
            }
        }
}

/* Every pool. "4095 pools": the masks 1..4095; bit 0 is always forced on, so they are 2048 pools, 2036 with at least the
 * three notes the UI insists on. All masks must not crash; those 2036 at all 12 roots must be right. */
static void pool_checks(void)
{
    uint32_t mask, valid = 0u, fam[3] = {0u, 0u, 0u}, borrowed_pools = 0u;
    for (mask = 1u; mask <= 4095u; mask++) {
        const uint32_t p = (mask | 1u) & 0xFFFu;
        const int small = pool_size(p) < 3;
        int id, root;
        en_hchord_t c;
        for (id = 0; id < en_harm_count(); id++) {
            en_harm_resolve(mask, id, &c);
            if (!small && mask == p)             /* (each pool once) */
                chord_valid(p, id, &c);
        }
        if (small || mask != p)
            continue;
        valid++;
        fam[en_pool_family(p)]++;
        en_harm_resolve(p, 16, &c);
        borrowed_pools += c.borrowed;
        for (root = 0; root < 12; root++) {
            en_harm_t st, was;
            en_hchord_t ch[4];
            uint8_t hk[4][4], hp[4][4], k[4][4], q[4][4];
            uint32_t cy;
            int s, v, nb = 0;
            en_home_voicing(p, root, hk, hp);
            memcpy(k, hk, sizeof k);
            memcpy(q, hp, sizeof q);
            en_harm_walk(&st, p, root, 0u, EN_HARM_SEED, script_level(0u), ch);
            voices_valid(p, root, (const uint8_t (*)[4])k, (const uint8_t (*)[4])q, ch, 0u);
            for (cy = 1u; cy < NCYCLES; cy++) {
                was = st;
                en_harm_walk(&st, p, root, cy, EN_HARM_SEED, script_level(cy), ch);
                en_voice_cycle(&was, &st, ch, 0, root, (const uint8_t (*)[4])hk, (const uint8_t (*)[4])hp, k, q);
                voices_valid(p, root, (const uint8_t (*)[4])k, (const uint8_t (*)[4])q, ch, cy);
                for (s = 0; s < 4; s++)
                    nb += ch[s].borrowed;
                if (root == 0)                   /* the bass, the same at every root */
                    for (s = 0; s < 4; s++) {
                        uint32_t cs;
                        for (cs = 0u; cs < 64u; cs++) {
                            int len = 0;
                            const int off = en_bass_ch(st.chord[s], &ch[s], &ch[(s + 1) & 3], p, cy, cs, 3, EN_HARM_SEED, &len);
                            if (off >= 0) {
                                CHECK((off + 120) % 12 != 0, "pool %03x: the tonic in the bass line", p);
                                CHECK(off >= 2 && off <= 24, "pool %03x: bass note %d", p, off);
                            }
                        }
                    }
                for (v = 0; v < 2; v++)
                    for (s = 0; s < 4; s++) {
                        const int mt = (en_motif_note_ch(v, s, ch) + 120) % 12;
                        CHECK(ch[en_motif_step(v, s) / 64].tones >> mt & 1u, "pool %03x: motif out of its chord", p);
                    }
            }
            CHECK(nb <= 1, "pool %03x root %d: %d borrowed chords", p, root, nb);
        }
    }
    CHECK(valid == 2036u, "%u valid pools", valid);
    printf("  pools: 4095 masks without a crash; %u distinct valid pools x 12 roots checked: %u major, %u minor, %u sparse; %u can borrow\n",
           valid, fam[0], fam[1], fam[2], borrowed_pools);

    /* borrowed once in a walk, whatever the levels do (the second cycle entered again and again) */
    {
        uint32_t sd, cy;
        for (sd = 0u; sd < 50u; sd++) {
            en_harm_t st;
            en_hchord_t ch[4];
            int nb = 0, s;
            for (cy = 0u; cy < 200u; cy++) {
                en_harm_walk(&st, POOL_D_IONIAN, 2, cy, sd, cy % 7u < 3u ? 3 : 2, ch);
                for (s = 0; s < 4; s++)
                    nb += ch[s].borrowed;
            }
            CHECK(nb == 1, "seed %u: %d borrowed cycles in a walk", sd, nb);
        }
    }
}

/* the families, by name: what was chosen, in the stock modes */
static void family_checks(void)
{
    static const struct {
        int mode, root, fam;
        const char *home[4], *sec[4], *borrow;
    } T[] = {
        {MODE_IONIAN, 2, EN_FAM_MAJOR, {"GM7", "F#m7", "Bm9", "Aadd9"}, {"Em9", "A13sus", "F#m7", "Bm9"}, "CM9"},
        {MODE_LYDIAN, 2, EN_FAM_MAJOR, {"Eadd9", "F#m7", "Bm9", "Aadd9"}, {"E6/9", "Asus4", "F#m7", "Bm9"}, "GM9"},
        {MODE_MIXOLYDIAN, 2, EN_FAM_MAJOR, {"GM7", "CM7", "Bm7", "Amadd9"}, {"Em9", "A13sus", "CM7", "Bm7"}, "FM9"},
        {MODE_DORIAN, 2, EN_FAM_MINOR, {"Gadd9", "FM7", "CM9", "Amadd9"}, {"G6/9", "Csus4", "FM7", "Am9"}, "BbM9"},
        {MODE_AEOLIAN, 9, EN_FAM_MINOR, {"Dm7", "CM7", "FM9", "Em7"}, {"Dm9", "G13sus", "CM7", "FM9"}, "Dadd9"},
        {MODE_PHRYGIAN, 4, EN_FAM_MINOR, {"Am7", "Gadd9", "CM9", "Dmadd9"}, {"Am9", "D13sus", "Gadd9", "CM9"}, "Aadd9"},
        {MODE_PENT_MAJ, 2, EN_FAM_SPARSE, {"Esus4", "F#7sus4", "Bsus4", "Asus2"}, {"Esus2", "Asus4", "F#7sus4", "Bsus4"}, 0},
        {MODE_PENT_MIN, 2, EN_FAM_SPARSE, {"Gsus4", "Fsus2", "Csus2", "A7sus4"}, {"Gsus2", "Csus4", "Fsus2", "A7sus4"}, 0},
    };
    unsigned t;
    int s;
    char nm[16];
    for (t = 0u; t < sizeof T / sizeof T[0]; t++) {
        const uint32_t p = EN_MODE_POOL[T[t].mode];
        en_hchord_t c;
        CHECK(en_pool_family(p) == T[t].fam, "mode %d: family %d", T[t].mode, en_pool_family(p));
        for (s = 0; s < 4; s++) {
            en_harm_resolve(p, en_harm_home(s), &c);
            c.tonic = (uint8_t)T[t].root;
            CHECK(!strcmp(en_hchord_name(&c, nm), T[t].home[s]), "mode %d home %d: %s, not %s", T[t].mode, s, nm, T[t].home[s]);
            en_harm_resolve(p, en_harm_section(s), &c);
            c.tonic = (uint8_t)T[t].root;
            CHECK(!strcmp(en_hchord_name(&c, nm), T[t].sec[s]), "mode %d section %d: %s, not %s", T[t].mode, s, nm, T[t].sec[s]);
        }
        en_harm_resolve(p, 16, &c);
        c.tonic = (uint8_t)T[t].root;
        CHECK(c.borrowed == (T[t].borrow != 0), "mode %d: borrowed %d", T[t].mode, c.borrowed);
        if (T[t].borrow)
            CHECK(!strcmp(en_hchord_name(&c, nm), T[t].borrow), "mode %d borrowed: %s, not %s", T[t].mode, nm, T[t].borrow);
    }
    /* a custom pool goes by its third; no third, or under six notes: sparse */
    CHECK(en_pool_family(0xAB5u & ~(1u << 11)) == EN_FAM_MAJOR, "Ionian less its seventh");
    CHECK(en_pool_family(0x5ADu & ~(1u << 10)) == EN_FAM_MINOR, "Aeolian less its seventh");
    CHECK(en_pool_family(1u | 1u << 2 | 1u << 5 | 1u << 7 | 1u << 9 | 1u << 11) == EN_FAM_SPARSE, "no third");
    CHECK(en_pool_family(1u | 1u << 4 | 1u << 7) == EN_FAM_SPARSE, "three notes");
    /* the same seed, the same chords; D Ionian as it was, voicing for voicing, is held by harmony_checks over en_cycle_voicing */
    {
        en_harm_t a, b;
        en_hchord_t ca[4], cb[4];
        uint32_t cy;
        for (cy = 0u; cy < NCYCLES; cy++) {
            en_harm_walk(&a, EN_MODE_POOL[MODE_DORIAN], 2, cy, 5u, script_level(cy), ca);
            en_harm_walk(&b, EN_MODE_POOL[MODE_DORIAN], 2, cy, 5u, script_level(cy), cb);
            CHECK(!memcmp(&a, &b, sizeof a) && !memcmp(ca, cb, sizeof ca), "cycle %u: the same seed differs", cy);
        }
    }
}

/* what the engine really plays, from its MIDI: step, kind, note */
#define NEV 8000
typedef struct {
    uint32_t step;
    uint8_t kind, note;                          /* kind: 0 chord's piano note, 1 lone piano note, 2 motif, 3 bass */
} ev_t;
static ev_t ev[NEV];
static int nev;
static uint32_t ev_singles;
static void ev_note(uint32_t st, uint32_t d1)
{
    int kind = -1;
    if (st == 0x90u) {
        kind = en_ep_singles != ev_singles ? 1 : 0;
        ev_singles = en_ep_singles;
    } else if (st == 0x91u) {
        kind = 2;
    } else if (st == 0x92u) {
        kind = 3;
    }
    if (kind >= 0 && nev < NEV) {
        ev[nev].step = en_bars;
        ev[nev].kind = (uint8_t)kind;
        ev[nev].note = (uint8_t)d1;
        nev++;
    }
}

static void render_until(uint32_t step)
{
    static int32_t buf[512];
    int j;
    while (en_bars < step) {
        en_render(buf, 256, 1024);
        for (j = 0; j < 512; j++)
            CHECK(buf[j] > -(1 << 24) && buf[j] < (1 << 24), "out of range: %d", buf[j]);
        CHECK(en_pad_level == en_pad_level && en_bass_level == en_bass_level, "NaN");
    }
}
static void engine_start(uint32_t p, int root)
{
    en_play(0);
    render_until(0u);
    {
        static int32_t b[512];
        en_render(b, 256, 1024);
    }
    en_init();
    en_test_script(DEMO_SCRIPT, DEMO_SCRIPT_N);
    en_set(P_ROOT, root);
    en_pool(p);
    en_play(1);
    {
        static int32_t b[512];
        en_render(b, 256, 1024);                 /* (en_bars starts again, after the first step) */
    }
    nev = 0;
    ev_singles = en_ep_singles;
}

static int abs_in(uint32_t p, int root, int note) { return p >> (((note - root) % 12 + 12) % 12) & 1u; }

/* a change at step at, seen in the notes: chords and bass wait for the seam, lone notes and motifs do not. to: */
static int motif_early;                          /* motif notes struck between the change and its seam */
static void change_seen(const char *what, uint32_t p0, int r0, uint32_t p1, int r1, uint32_t at, int by_root)
{
    const uint32_t seam = (at / 256u + 1u) * 256u, end = seam + 256u;
    int i, lone = 0, motif = 0, before = 0, after = 0;
    motif_early = 0;
    en_hchord_t old[4], fresh_ch[4];
    en_harm_t st, st2;
    uint32_t cy;
    engine_start(p0, r0);
    render_until(at);
    nev = 0;
    if (by_root)
        en_set(P_ROOT, r1);
    else
        en_pool(p1);
    render_until(end);
    for (cy = 0u; cy <= at / 256u; cy++)
        en_harm_walk(&st, p0, r0, cy, EN_HARM_SEED, script_level(cy), old);
    st2 = st;
    for (; cy <= seam / 256u; cy++)
        en_harm_walk(&st2, p1, r1, cy, EN_HARM_SEED, script_level(cy), fresh_ch);
    /* (a note's step is en_bars as it is struck: song step s runs while en_bars is s) */
    for (i = 0; i < nev; i++) {
        const ev_t *e = &ev[i];
        const int s = (int)(e->step / 64u & 3u);
        const int late = e->step >= seam;
        if (e->step < at)
            continue;
        if (e->kind == 1 || e->kind == 2) {
            /* a borrowed chord's own notes are allowed, while the pool it was made from stands */
            if (!(late && fresh_ch[s].borrowed))
                CHECK(abs_in(p1, r1, e->note), "%s: step %u: a %s note %d outside the new pool", what, e->step, e->kind == 1 ? "lone" : "motif", e->note);
            lone += e->kind == 1;
            motif += e->kind == 2;
            motif_early += e->kind == 2 && !late;
        } else if (!late) {
            const uint32_t m = old[s].tones;
            if (e->kind == 0)
                CHECK(m >> (((e->note - r0) % 12 + 12) % 12) & 1u, "%s: step %u: a chord note %d not of the old chord", what, e->step, e->note);
            else
                CHECK(abs_in(p0, r0, e->note) || old[s].borrowed, "%s: step %u: bass %d left the old key before the seam", what, e->step, e->note);
            before++;
        } else {
            if (e->kind == 0 && e->step < seam + 64u)
                CHECK(fresh_ch[s].tones >> (((e->note - r1) % 12 + 12) % 12) & 1u, "%s: step %u: chord note %d not of the new chord", what, e->step, e->note);
            if (!fresh_ch[s].borrowed)
                CHECK(abs_in(p1, r1, e->note), "%s: step %u: %d after the seam, outside the new pool", what, e->step, e->note);
            after++;
        }
    }
    CHECK(lone > 0, "%s: no lone notes to judge by", what);
    CHECK((before > 0 || at + 16u > seam) && after > 0, "%s: %d notes before, %d after", what, before, after);
    /* the screen: the chord playing now by the same name the pure function gives it (the cycle's last chord) */
    {
        int pcs[5], v;
        char a[16], b[16];
        pcs[0] = en_chord_root;
        for (v = 0; v < 4; v++)
            pcs[v + 1] = en_chord[v] % 12;
        en_chord_name(pcs, 5, a);
        en_hchord_name(&fresh_ch[3], b);
        CHECK(!strcmp(a, b), "%s: the screen says %s, the walk %s", what, a, b);
    }
    printf("  %s at step %u: seam %u: %d lone, %d motif notes obeyed at once; %d chord and bass notes waited, %d followed\n", what, at, seam, lone, motif, before, after);
}

static void live_checks(void)
{
    const uint32_t ion = EN_MODE_POOL[MODE_IONIAN], lyd = EN_MODE_POOL[MODE_LYDIAN];
    /* the mode */
    change_seen("mode", lyd, 2, EN_MODE_POOL[MODE_AEOLIAN], 2, 700u, 0);
    change_seen("mode, in the second cycle", lyd, 2, EN_MODE_POOL[MODE_DORIAN], 2, 1900u, 0);
    /* the root */
    change_seen("root", ion, 2, ion, 9, 900u, 1);
    /* a note out: F# of D Ionian */
    change_seen("a note out", ion, 2, ion & ~(1u << 4), 2, 1100u, 0);
    /* close to the seam: the prepared cycle must be remade */
    change_seen("late, four steps before the seam", lyd, 2, EN_MODE_POOL[MODE_PENT_MIN], 2, 1276u, 0);
    change_seen("later still, on the last step", lyd, 2, EN_MODE_POOL[MODE_PENT_MIN], 2, 1279u, 0);

    /* the motifs, from D Ionian into a pool without most of their notes, with both phrases still to come in this cycle */
    change_seen("motifs", ion, 2, EN_MODE_POOL[MODE_PENT_MIN], 2, 770u, 0);
    CHECK(motif_early >= 6, "only %d motif notes before the seam", motif_early);
    /* the very last moment, between the seam's step before and the seam: it still takes this seam */
    {
        en_harm_t st;
        en_hchord_t ch[4];
        uint32_t cy;
        char a[16], b[16];
        int pcs[5], v;
        engine_start(lyd, 2);
        render_until(1280u);
        en_pool(EN_MODE_POOL[MODE_DORIAN]);
        render_until(1281u);
        for (cy = 0u; cy <= 5u; cy++)
            en_harm_walk(&st, EN_MODE_POOL[MODE_DORIAN], 2, cy, EN_HARM_SEED, script_level(cy), ch);
        pcs[0] = en_chord_root;
        for (v = 0; v < 4; v++)
            pcs[v + 1] = en_chord[v] % 12;
        en_chord_name(pcs, 5, a);
        en_hchord_name(&ch[0], b);
        CHECK(!strcmp(a, b), "changed just before the seam: plays %s, should be %s", a, b);
    }
    /* a note in: heard on the next eighth, once */
    {
        const uint32_t p1 = ion | 1u << 3, at = 1000u;    /* F natural into D Ionian */
        int i, heard = -1;
        engine_start(ion, 2);
        render_until(at);
        nev = 0;
        en_pool(p1);
        render_until(at + 64u);
        for (i = 0; i < nev && heard < 0; i++)
            if (ev[i].kind == 1 && (ev[i].note + 120 - 2) % 12 == 3)
                heard = (int)ev[i].step;
        CHECK(heard >= 0 && (uint32_t)heard <= at + 3u, "the new note speaks at step %d after %u", heard, at);
        printf("  a note in at step %u: heard on step %d\n", at, heard);
    }
    /* the walk keeps its place: changes mid-song, the same ids as the walk with none */
    {
        static const uint32_t CH[3] = {0x5ADu, 0x4A9u, 0xAB5u};
        uint32_t step = 300u, i = 0u;
        uint8_t want[4];
        engine_start(lyd, 2);
        while (step < 2600u) {
            render_until(step);
            en_pool(CH[i++ % 3u]);
            en_cycle_chords(step / 256u, EN_HARM_SEED, script_level(step / 256u), want);
            CHECK(!memcmp(want, (const void *)en_cycle_now, 4), "step %u: the walk lost its place", step);
            step += 300u;
        }
    }
    /* same seed, same notes: the human errors of time and touch never choose one */
    {
        static uint32_t a[NEV];
        const uint32_t p0 = EN_MODE_POOL[MODE_DORIAN];
        int i, na;
        engine_start(p0, 2);
        render_until(600u);
        en_pool(lyd);
        render_until(1200u);
        na = nev;
        for (i = 0; i < na; i++)
            a[i] = ev[i].step << 10 | (uint32_t)ev[i].kind << 8 | ev[i].note;
        engine_start(p0, 2);
        render_until(600u);
        en_pool(lyd);
        render_until(1200u);
        CHECK(nev == na, "the same seed: %d notes, then %d", na, nev);
        for (i = 0; i < na && i < nev; i++)
            CHECK(a[i] == (ev[i].step << 10 | (uint32_t)ev[i].kind << 8 | ev[i].note), "the same seed: note %d differs", i);
    }
}

/* the piano flows in every mode, and through changes: the limits engine_checks holds in D */
static void flow_run(const char *name, uint32_t p, int root, int live)
{
    uint32_t frames = 0u, last_chord = 0u, c0, s0, n0;
    uint32_t chords = 0u, att = 0u, sing = 0u, next_change = 44100u * 50u;
    int k = 0;
    engine_start(p, root);
    c0 = en_ep_chords;
    s0 = en_ep_singles;
    n0 = en_ep_notes;
    (void)n0;
    while (frames < 44100u * 248u) {
        static int32_t buf[512];
        int j;
        en_render(buf, 256, 1024);
        frames += 256u;
        for (j = 0; j < 512; j++)
            CHECK(buf[j] > -(1 << 24) && buf[j] < (1 << 24), "%s: out of range", name);
        if ((en_bars - 1u) / 64u != last_chord) {
            const int lv = script_level(last_chord / 4u);
            const uint32_t a = en_ep_chords - c0, b = en_ep_singles - s0;
            CHECK(a <= (lv == 3 ? 2u : 1u), "%s: level %d chord %u: %u chord attacks", name, lv, last_chord, a);
            CHECK(b <= 3u, "%s: level %d chord %u: %u lone notes", name, lv, last_chord, b);
            chords++;
            att += a;
            sing += b;
            c0 = en_ep_chords;
            s0 = en_ep_singles;
            last_chord = (en_bars - 1u) / 64u;
        }
        if (live && frames >= next_change) {
            next_change += 44100u * 25u;
            switch (k++) {
            case 0: en_pool(EN_MODE_POOL[MODE_AEOLIAN]); break;
            case 1: en_set(P_ROOT, 9); break;
            case 2: en_pool(EN_MODE_POOL[MODE_AEOLIAN] | 1u << 9); break;
            case 3: en_pool(EN_MODE_POOL[MODE_AEOLIAN] & ~(1u << 8)); break;
            case 4: en_pool(EN_MODE_POOL[MODE_PENT_MAJ]); break;
            case 5: en_set(P_ROOT, 2); break;
            default: en_pool(p); break;
            }
        }
    }
    printf("  flow %-10s %.2f chord attacks, %.2f lone notes a chord; peak %d voices, %u stolen\n", name,
           (double)att / (double)chords, (double)sing / (double)chords, en_ep_peak, en_ep_steals);
    CHECK(en_ep_steals == 0u, "%s: %u voices stolen while sounding", name, en_ep_steals);
    CHECK(en_ep_peak <= 12, "%s: peak %d voices", name, en_ep_peak);
}

static void flow_checks(void)
{
    static const int ROOT[EN_NMODES] = {2, 2, 4, 2, 2, 9, 2, 2};
    int m;
    for (m = 0; m < EN_NMODES; m++)
        flow_run(EN_MODE_NAME[m], EN_MODE_POOL[m], ROOT[m], 0);
    flow_run("live", EN_MODE_POOL[MODE_LYDIAN], 2, 1);
}

int main(void)
{
    golden_checks();
    harmony_checks();
    pool_checks();
    family_checks();
    engine_checks();
    live_checks();
    flow_checks();
    bass_checks();
    drum_checks();
    hat_checks();
    printf(fails ? "harm: %d FAILED\n" : "harm: ok\n", fails);
    return fails != 0;
}
