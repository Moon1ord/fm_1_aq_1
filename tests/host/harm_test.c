/* SPDX-License-Identifier: MPL-2.0 */
/* The demo's growing harmony and its living drums: both pure functions of a seed, so both can be read and checked. */
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "amb.h"
#include "brk.h"

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
void en_midi_out(uint32_t st, uint32_t d1, uint32_t d2)
{
    (void)st;
    (void)d1;
    (void)d2;
}

#define NCYCLES 11u                              /* the song's: cycle 10 holds its last chord */
#define NSEEDS 300u

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
            const int lv = en_song_level(c);
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
            const int enter = en_song_level(c) == 3 && en_song_level(c - 1u) != 3;
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
            const int lv = en_song_level(last_chord / 4u);
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
            en_cycle_chords(cycle, EN_HARM_SEED, en_song_level(cycle), want);
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

/* the bass: long notes, on the chord, never landing on the tonic, moving a little */
static void bass_checks(void)
{
    uint32_t sd, c, cs;
    int level, s;
    for (level = 2; level <= 3; level++) {
        uint32_t notes = 0u, chords_n = 0u, steps = 0u, held = 0u, leads = 0u, seen = 0u;
        for (sd = 0u; sd < NSEEDS; sd++)
            for (c = 0u; c < NCYCLES - 1u; c++) {
                uint8_t q[4], nx[4];
                en_cycle_chords(c, sd, en_song_level(c), q);
                en_cycle_chords(c + 1u, sd, en_song_level(c + 1u), nx);
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
        CHECK((double)notes * 16.0 / ((double)chords_n * 64.0) <= (level == 3 ? 3.0 : 2.25), "level %d: too many notes", level);
        CHECK(steps == chords_n * 64u, "level %d: the notes do not fill the time: %u of %u steps", level, steps, chords_n * 64u);
        CHECK(leads > 0u && held > 0u && seen, "level %d: nothing moves", level);
    }
}

/* the drums: a plain two-step, breaks now and then, back on the grid; deterministic for a seed */
#define DSTEPS 6400u
static char core_a[DSTEPS], top_a[DSTEPS], hat_a[DSTEPS];
static void drums(uint32_t seed, int level, char *c, char *t, char *h)
{
    uint32_t i;
    brk_init();
    brk_levels(1.0f, 1.0f, 1.0f);
    for (i = 0u; i < DSTEPS; i++) {
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
            const int in = brk_break_at(i, level, 1234u, 0);
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

int main(void)
{
    harmony_checks();
    engine_checks();
    bass_checks();
    drum_checks();
    hat_checks();
    printf(fails ? "harm: %d FAILED\n" : "harm: ok\n", fails);
    return fails != 0;
}
