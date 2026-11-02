/* SPDX-License-Identifier: MPL-2.0 */
/* The demo's growing harmony and its living drums: both pure functions of a seed, so both can be read and checked. */
#include <stdio.h>
#include <string.h>
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

#define DSTEPS 6400u
static char core_a[DSTEPS], top_a[DSTEPS];
static void drums(uint32_t seed, int level, char *c, char *t)
{
    uint32_t i;
    brk_init();
    brk_levels(1.0f, 1.0f, 1.0f);
    for (i = 0u; i < DSTEPS; i++) {
        brk_pattern_step(i, level, 0, seed);
        c[i] = brk_hit_core;
        t[i] = brk_hit_top;
    }
}
static int is_ghost(char c) { return c == 'g' || c == 'f' || c == 'e'; }

static void drum_checks(void)
{
    static char c2[DSTEPS], t2[DSTEPS];
    int level;
    uint32_t i;
    for (level = 2; level <= 3; level++) {
        int soft = 0, hat[3] = {0, 0, 0}, opens = 0, core_diff = 0;
        uint32_t ghosts = 0u, maxbar = 0u, inbar = 0u, kicks = 0u, snares = 0u, want_k = 0u, want_s = 0u;
        drums(1234u, level, core_a, top_a);
        drums(1234u, level, c2, t2);
        CHECK(!memcmp(core_a, c2, DSTEPS) && !memcmp(top_a, t2, DSTEPS), "level %d: same seed, other drums", level);
        for (i = 0u; i < DSTEPS; i++) {
            const uint32_t bs = i % 16u;
            const char c = core_a[i], t = top_a[i];
            if (level == 3) {                    /* the skeleton: kick 0 and 10, snare 4 and 12, nowhere else */
                want_k += bs == 0u || bs == 10u;
                want_s += bs == 4u || bs == 12u;
                if (c == 'K' || c == 'k')
                    CHECK(bs == 0u || bs == 10u, "a kick off the skeleton at %u", i);
                if (c == 'S' || c == 's')
                    CHECK(bs == 4u || bs == 12u, "a snare off the skeleton at %u", i);
                if (bs == 4u || bs == 12u)
                    CHECK(c == 'S' || c == 's', "the backbeat missing at %u", i);
                if (bs == 0u)
                    CHECK(c == 'K', "the downbeat's kick missing at %u", i);
            } else {
                want_k += bs == 0u || i % 64u == 54u;
                want_s += bs == 8u;
                if (bs == 8u)
                    CHECK(c == 'S' || c == 's', "the backbeat missing at %u", i);
            }
            kicks += c == 'K' || c == 'k';
            snares += c == 'S' || c == 's';
            soft += c == 's';
            hat[0] += t == 'h';
            hat[1] += t == 'j';
            hat[2] += t == 'l';
            opens += t == 'o';
            if (bs == 0u) {
                maxbar = inbar > maxbar ? inbar : maxbar;
                inbar = 0u;
            }
            if (is_ghost(t)) {
                ghosts++;
                inbar++;
            }
        }
        printf("  drums level %d: %u kicks, %u snares (%d soft), %u ghosts, max %u a bar; hats %d %d %d, %d open\n", level,
               kicks, snares, soft, ghosts, maxbar, hat[0], hat[1], hat[2], opens);
        CHECK(snares == want_s, "level %d: %u snares, the skeleton has %u", level, snares, want_s);
        CHECK(kicks >= want_k * 9u / 10u && kicks <= want_k * 11u / 10u, "level %d: %u kicks against %u", level, kicks, want_k);
        CHECK(maxbar <= 3u, "level %d: %u ghosts in a bar", level, maxbar);
        CHECK(ghosts * 16u <= DSTEPS * 2u, "level %d: %u ghosts in %u steps", level, ghosts, DSTEPS);
        CHECK(ghosts > 0u && soft > 0 && opens > 0, "level %d: nothing varies", level);
        CHECK(hat[0] > 0 && hat[1] > 0 && hat[2] > 0, "level %d: not all the hats", level);
        drums(99u, level, c2, t2);
        for (i = 0u; i < DSTEPS; i++)
            core_diff += core_a[i] != c2[i] || top_a[i] != t2[i];
        CHECK(core_diff > 0, "level %d: two seeds, the same drums", level);
    }
    drums(1234u, 1, core_a, top_a);
    for (i = 0u; i < DSTEPS; i++)
        CHECK(core_a[i] == '.', "level 1 has a kick or a snare at %u", i);
}

int main(void)
{
    harmony_checks();
    engine_checks();
    drum_checks();
    printf(fails ? "harm: %d FAILED\n" : "harm: ok\n", fails);
    return fails != 0;
}
