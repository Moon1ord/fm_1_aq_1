/* SPDX-License-Identifier: MPL-2.0 */
/* The melody played by hand: Keys, each key a note of the melody's voices; Keys Arp, the keys held arpeggiated on the grid; Latch. What comes down goes up, whatever happens between:
 * a panic, a piece, a mode, a mute, a rest, a duck, a flood of the queue, thousands of random presses. What a key is on the panel is ui.c's: tests/scenarios/keys.amb. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "amb.h"

static int fails;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); fails++; } } while (0)
#define GAIN 2430u
#define SEC(s) ((uint32_t)((s) * 44100.0f) & ~255u)
#define KEY(k) (1u << (k))

/* ---- MIDI on the melody's channel, note by note */
static uint32_t frames, mhash, midi_n;
static uint32_t ons, offs, double_on, off_unowed, on_steps;
static int down[128], nd, most_down, last_on, last_off, other_nd[16];
#define REC 8192u
static uint8_t rec[REC], rec_vel[REC];           /* what was struck, in order */
static uint32_t rec_step[REC], rec_frame[REC], rec_n;
void en_midi_out(uint32_t st, uint32_t d1, uint32_t d2)
{
    const uint32_t v[4] = {st, d1, d2, frames};
    const int c = (int)(st & 15u), k = (int)(st & 0xF0u);
    int i;
    for (i = 0; i < 4; i++)
        mhash = (mhash ^ v[i]) * 16777619u;
    midi_n++;
    if (st >= 0xF0u)
        return;
    if (c != 1) {
        if (k == 0x90 && d2)
            other_nd[c]++;
        return;
    }
    if (k == 0x90 && d2) {
        ons++;
        if (down[d1])
            double_on++;
        down[d1]++;
        nd++;
        if (nd > most_down)
            most_down = nd;
        last_on = (int)d1;
        if (rec_n < REC) {
            rec[rec_n] = (uint8_t)d1;
            rec_vel[rec_n] = (uint8_t)d2;
            rec_step[rec_n] = en_test_song();
            rec_frame[rec_n] = frames;
            rec_n++;
        }
    } else if (k == 0x80 || k == 0x90) {
        offs++;
        last_off = (int)d1;
        if (down[d1] > 0) {
            down[d1]--;
            nd--;
        } else {
            off_unowed++;
        }
    } else if (k == 0xB0 && d1 == 123u) {
        memset(down, 0, sizeof down);
        nd = 0;
    }
}

/* ---- rendering */
static int32_t buf[2 * 256];
static double e_sum, peak, step_most;
static uint32_t e_n, ahash, led_bad, voices_most;
static int ledger;
static int32_t step_prev;
static void counts_clear(void)
{
    ons = offs = double_on = off_unowed = on_steps = 0u;
    most_down = 0;
    last_on = last_off = -1;
    rec_n = 0u;
    voices_most = 0u;
    midi_n = 0u;
}
static void meter_clear(void)
{
    e_sum = peak = step_most = 0.0;
    e_n = 0u;
}
static void start(uint32_t seed, int mode, int melmode)
{
    en_keys(0u);
    en_init();
    frames = 0u;
    mhash = ahash = 2166136261u;
    memset(down, 0, sizeof down);
    nd = 0;
    counts_clear();
    meter_clear();
    led_bad = 0u;
    en_arrange(mode);
    en_seed(seed);
    en_set(P_MELMODE, melmode);
    en_set(P_MELSND, EN_TB_GLASS + 1);           /* (long and even: what is heard is easy to look at) */
    en_layer(EN_L_MEL, 1);
}
static void run(uint32_t n, uint32_t blk)
{
    while (n) {
        const uint32_t k = n < blk ? n : blk;
        uint32_t i;
        en_render(buf, k, GAIN);
        for (i = 0; i < 2u * k; i++) {
            const double x = (double)buf[i] / 8300000.0, a = fabs(x);
            e_sum += x * x;
            if (a > peak)
                peak = a;
            ahash = (ahash ^ (uint32_t)buf[i]) * 16777619u;
        }
        for (i = 0; i < 2u * k; i += 2u) {
            const double d = fabs((double)buf[i] - (double)step_prev);
            if (d > step_most)
                step_most = d;
            step_prev = buf[i];
        }
        e_n += 2u * k;
        if ((uint32_t)en_test_bell_voices() > voices_most)
            voices_most = (uint32_t)en_test_bell_voices();
        if (ledger && nd != en_test_bells_on()) {
            if (led_bad++ < 4u)
                printf("FAIL the ledger: MIDI says %d of the melody's notes down, the engine %d\n", nd, en_test_bells_on());
            fails++;
        }
        frames += k;
        n -= k;
    }
}
static void block(void) { run(256u, 256u); }
static double rms_now(void) { return sqrt(e_sum / (double)(e_n ? e_n : 1u)); }
static void only_melody(void)
{
    en_set(P_KEYS, 0);
    en_set(P_BASS, 0);
    en_set(P_DRUMS, 0);
    en_set(P_HATS, 0);
    en_set(P_REVERB, 0);
    en_set(P_ECHO, 0);
}
static int pairs_ok(const char *what)
{
    CHECK(!double_on && !off_unowed, "%s: pairs: %u twice on, %u off with none on", what, double_on, off_unowed);
    return !double_on && !off_unowed;
}

/* ---- the pure parts */
static void seq(const char *what, const uint8_t *order, int n, int pattern, const int *want, int m, int rounds)
{
    en_arp_t a;
    int i;
    en_arp_start(&a);
    for (i = 0; i < m * rounds; i++) {
        const int g = en_held_next(&a, order, n, pattern, 2654435761u * (uint32_t)(i + 1));
        CHECK(g == want[i % m], "%s, step %d: key %d, not %d", what, i, g, want[i % m]);
    }
}
static void t_pure(void)
{
    static const uint8_t HOLD[4] = {7, 0, 12, 4};    /* as played; by pitch 0 4 7 12 */
    static const int UP[4] = {0, 4, 7, 12}, DOWN[4] = {12, 7, 4, 0}, UPDOWN[6] = {0, 4, 7, 12, 7, 4};
    static const int CONV[4] = {0, 12, 4, 7}, PEDT[6] = {12, 0, 12, 4, 12, 7}, PEDR[6] = {0, 4, 0, 7, 0, 12}, PLAYED[4] = {7, 0, 12, 4};
    static const uint8_t ONE[1] = {9};
    static const int ONEW[1] = {9};
    static const uint8_t TWO[2] = {5, 2};
    static const int T_UP[2] = {2, 5}, T_DN[2] = {5, 2};
    en_arp_t a;
    int i, p, k;
    seq("up", HOLD, 4, EN_ARP_UP, UP, 4, 3);
    seq("down", HOLD, 4, EN_ARP_DOWN, DOWN, 4, 3);
    seq("up-down", HOLD, 4, EN_ARP_UPDOWN, UPDOWN, 6, 3);
    seq("converge", HOLD, 4, EN_ARP_CONVERGE, CONV, 4, 3);
    seq("pedal top", HOLD, 4, EN_ARP_PEDTOP, PEDT, 6, 3);
    seq("pedal root", HOLD, 4, EN_ARP_PEDROOT, PEDR, 6, 3);
    seq("as played", HOLD, 4, EN_ARP_PLAYED, PLAYED, 4, 3);
    /* one key: that note, in every pattern. Two: turn and turn about */
    for (p = 0; p < EN_ARP_NPATTERNS; p++)
        seq("one key", ONE, 1, p, ONEW, 1, 6);
    seq("two, up", TWO, 2, EN_ARP_UP, T_UP, 2, 3);
    seq("two, down", TWO, 2, EN_ARP_DOWN, T_DN, 2, 3);
    seq("two, up-down", TWO, 2, EN_ARP_UPDOWN, T_UP, 2, 3);
    seq("two, played", TWO, 2, EN_ARP_PLAYED, T_DN, 2, 3);
    /* Random: of those held, never the same twice, all of them */
    {
        uint32_t seen = 0u;
        int last = -1;
        en_arp_start(&a);
        for (i = 0; i < 400; i++) {
            const int g = en_held_next(&a, HOLD, 4, EN_ARP_RANDOM, 2654435761u * (uint32_t)(i + 1));
            CHECK(g == 0 || g == 4 || g == 7 || g == 12, "random: key %d", g);
            CHECK(g != last, "random: key %d twice", g);
            last = g;
            seen |= 1u << g;
        }
        CHECK(seen == (KEY(0) | KEY(4) | KEY(7) | KEY(12)), "random: only %x", seen);
    }
    /* never out of what is held, never the same twice running with two or more, in any pattern, as keys come and go at any moment */
    for (p = 0; p < EN_ARP_NPATTERNS; p++) {
        uint8_t o[EN_KEYS_N];
        int n = 0, last = -1, t;
        uint32_t h = 2166136261u + (uint32_t)p;
        en_arp_start(&a);
        for (t = 0; t < 20000; t++) {
            h = h * 16777619u + 13u;
            if (!(h >> 28) || !n) {              /* a key goes down, if it is not */
                const int nk = (int)(h >> 8) % EN_KEYS_N;
                int f = 0;
                for (i = 0; i < n; i++)
                    f |= o[i] == nk;
                if (!f)
                    o[n++] = (uint8_t)nk;
            } else if ((h >> 28) == 1u && n > 1) {   /* and comes up */
                const int at = (int)(h >> 8) % n;
                for (i = at; i + 1 < n; i++)
                    o[i] = o[i + 1];
                n--;
            }
            k = en_held_next(&a, o, n, p, h);
            {
                int f = 0;
                for (i = 0; i < n; i++)
                    f |= o[i] == k;
                CHECK(f, "pattern %d: key %d is not held", p, k);
            }
            if (n >= 2 && !(h >> 28 <= 1u))      /* (a change may leave only the last) */
                CHECK(k != last, "pattern %d: key %d twice with %d held", p, k, n);
            last = k;
        }
    }
    /* a key added mid-figure: the figure goes on, up from where it is; one let go: likewise */
    {
        uint8_t o[4] = {0, 7};
        en_arp_start(&a);
        CHECK(en_held_next(&a, o, 2, EN_ARP_UP, 1u) == 0, "up: first");
        o[2] = 3;
        CHECK(en_held_next(&a, o, 3, EN_ARP_UP, 1u) == 3, "up: the key added comes next");
        CHECK(en_held_next(&a, o, 3, EN_ARP_UP, 1u) == 7, "up: then the top");
        o[0] = 3;                                /* 0 and 7 gone from where they were, 3 stays, then 12 */
        o[1] = 12;
        CHECK(en_held_next(&a, o, 2, EN_ARP_UP, 1u) == 12, "up: from 7 with 3 12 held");
        CHECK(en_held_next(&a, o, 2, EN_ARP_UP, 1u) == 3, "up: over the top");
    }
    /* the note: F4 at Octave 0 for the bottom key, a semitone a key; octaves; folded under 128 */
    CHECK(en_keys_note(0, 0) == 65, "key 0: %d", en_keys_note(0, 0));
    CHECK(en_keys_note(26, 0) == 91, "key 26: %d", en_keys_note(26, 0));
    CHECK(en_keys_note(0, -2) == 41, "key 0 down two: %d", en_keys_note(0, -2));
    CHECK(en_keys_note(26, 2) == 115, "key 26 up two: %d", en_keys_note(26, 2));
    for (k = 0; k < EN_KEYS_N; k++)
        CHECK(en_keys_note(k, 0) == 65 + k, "chromatic: key %d, %d", k, en_keys_note(k, 0));
    /* Auto's timbre: of Tone's window, what speaks at once, the softest at the bottom; one for each Tone, whatever else */
    {
        int r, was = -1;
        for (r = 0; r <= 100; r++) {
            const int w_lo = r > 50 ? (r - 50) * 3 / 50 : 0, w_hi = r < 50 ? 2 + r * 3 / 50 : EN_NTIMBRES - 1, t = en_keys_timbre(r);
            CHECK(t >= w_lo && t <= w_hi, "Tone %d: timbre %d out of %d..%d", r, t, w_lo, w_hi);
            CHECK(t >= EN_TB_VIBES, "Tone %d: timbre %d: its slow attack", r, t);
            CHECK(t >= was, "Tone %d: timbre %d under the last, %d", r, t, was);
            was = t;
        }
    }
    {   /* what is stored keeps its meaning */
        char t[16];
        static const char *const W[4] = {"Phrases", "Arp", "Keys", "Keys Arp"};
        int m;
        CHECK(EN_MEL_PHRASES == 0 && EN_MEL_ARP == 1 && EN_MEL_KEYS == 2 && EN_MEL_KEYS_ARP == 3, "the modes' numbers");
        CHECK(EN_ARP_PEDROOT == 6 && EN_ARP_PLAYED == 7 && EN_ARP_NPATTERNS == 8, "the patterns' numbers");
        for (m = 0; m < 4; m++) {
            en_param_text(P_MELMODE, m, t);
            CHECK(!strcmp(t, W[m]), "mode %d: %s", m, t);
        }
        en_param_text(P_ARPPAT, EN_ARP_PLAYED, t);
        CHECK(!strcmp(t, "Played"), "%s", t);
        en_param_text(P_ARPLATCH, 0, t);
        CHECK(!strcmp(t, "Off"), "%s", t);
        en_param_text(P_ARPLATCH, 1, t);
        CHECK(!strcmp(t, "On"), "%s", t);
        CHECK(en_param_info(P_ARPLATCH)->lo == 0 && en_param_info(P_ARPLATCH)->hi == 1 && en_param_info(P_ARPLATCH)->def == 0, "latch's range");
        CHECK(en_param_info(P_MELMODE)->hi == EN_MEL_KEYS_ARP && en_param_info(P_MELMODE)->def == EN_MEL_PHRASES, "the mode's range");
    }
    /* the recipes the engine has are Phrases and no latch: Hands' Keys is the project's (project.c) */
    for (k = 0; k < EN_NPIECES; k++) {
        int16_t par[P_NPARAMS];
        uint16_t pool;
        en_piece(k, par, &pool);
        CHECK(par[P_MELMODE] == EN_MEL_PHRASES && par[P_ARPLATCH] == 0, "piece %d: %d %d", k, par[P_MELMODE], par[P_ARPLATCH]);
    }
    /* Played in Arp, on the chord: there is no order to play them in: Up */
    {
        en_arp_t x, y;
        en_arp_start(&x);
        en_arp_start(&y);
        for (i = 0; i < 40; i++)
            CHECK(en_arp_next(&x, 0x891u, 3, EN_ARP_PLAYED, 1u) == en_arp_next(&y, 0x891u, 3, EN_ARP_UP, 1u), "Played on the chord, step %d", i);
    }
}

/* ---- Keys: down, a note and a voice, in the very block; up, its key lets go in the very block, and the voice rings out as the timbre does */
static void t_on_off(void)
{
    int t;
    for (t = 0; t < EN_NTIMBRES; t++) {
        en_mel_voice_t tv;
        uint32_t n;
        start(11u, EN_HANDS, EN_MEL_KEYS);
        en_set(P_MELSND, t + 1);
        block();
        en_mel_voice(t, 50, &tv);
        ledger = 1;
        en_keys(KEY(4));
        CHECK(en_keys_seen() == 0u, "seen before a block");
        block();
        CHECK(en_keys_seen() == KEY(4), "not seen after a block");
        CHECK(ons == 1u && last_on == en_keys_note(4, 0), "timbre %d: a key: %u notes, the last %d", t, ons, last_on);
        CHECK(en_test_bell_voices() == 1 && en_test_bells_on() == 1, "timbre %d: %d voices, %d down", t, en_test_bell_voices(), en_test_bells_on());
        CHECK(en_test_bell_struck == t, "timbre %d: struck %d", t, en_test_bell_struck);
        CHECK(rec_vel[0] >= 40u && rec_vel[0] <= 80u, "a gentle velocity: %u", rec_vel[0]);
        run(SEC(0.3f), 256u);
        CHECK(ons == 1u && offs == 0u, "timbre %d: held: %u on, %u off", t, ons, offs);
        en_keys(0u);
        block();
        CHECK(offs == 1u && last_off == en_keys_note(4, 0), "timbre %d: up: %u offs, the last %d", t, offs, last_off);
        CHECK(en_test_bells_on() == 0, "timbre %d: up: %d down", t, en_test_bells_on());
        /* the timbre's own release: gone in about its time, and still there early on */
        CHECK(en_test_bell_voices() == 1, "timbre %d: up: no tail", t);
        for (n = 0u; n < SEC(5.0f) && en_test_bell_voices(); n += 256u)
            block();
        CHECK(!en_test_bell_voices(), "timbre %d: still sounding after 5 s", t);
        CHECK((float)n / 44100.0f < tv.rel_s * 1.35f, "timbre %d: a tail of %f s, its release %f", t, (double)n / 44100.0, (double)tv.rel_s);
        CHECK((float)n / 44100.0f > tv.rel_s * 0.4f, "timbre %d: a tail of only %f s, its release %f", t, (double)n / 44100.0, (double)tv.rel_s);
        CHECK(pairs_ok("on and off"), "timbre %d", t);
        ledger = 0;
    }
    /* a voice that dies away with the key still down (Marimba's second): the key stays down for MIDI until it comes up, and it costs nothing meanwhile */
    start(13u, EN_HANDS, EN_MEL_KEYS);
    en_set(P_MELSND, EN_TB_MARIMBA + 1);
    block();
    ledger = 1;
    en_keys(KEY(0));
    run(SEC(4.0f), 256u);
    CHECK(en_test_bell_voices() == 0, "a dead voice sounding");
    CHECK(en_test_bells_on() == 1 && offs == 0u, "held and died away: %d down, %u offs", en_test_bells_on(), offs);
    en_keys(0u);
    block();
    CHECK(en_test_bells_on() == 0 && offs == 1u, "and up: %d down, %u offs", en_test_bells_on(), offs);
    pairs_ok("a held key, died away");
    ledger = 0;
    /* the Octave: at the press. A turn while it is down moves the next note and not its own */
    start(15u, EN_HANDS, EN_MEL_KEYS);
    en_set(P_MOCT, 1);
    block();
    en_keys(KEY(2));
    block();
    CHECK(last_on == en_keys_note(2, 1), "Octave +1: %d", last_on);
    en_set(P_MOCT, -1);
    block();
    en_keys(KEY(2) | KEY(3));
    block();
    CHECK(last_on == en_keys_note(3, -1), "Octave -1: %d", last_on);
    en_keys(KEY(3));
    block();
    CHECK(last_off == en_keys_note(2, 1), "its own note comes up: %d", last_off);
    en_keys(0u);
    block();
    CHECK(last_off == en_keys_note(3, -1), "its own note comes up: %d", last_off);
    /* one note from two keys, by way of the Octave: one key down at a time, the pairs clean */
    start(17u, EN_HANDS, EN_MEL_KEYS);
    ledger = 1;
    block();
    en_set(P_MOCT, 1);
    en_keys(KEY(0));
    block();
    en_set(P_MOCT, 0);
    en_keys(KEY(0) | KEY(12));
    block();
    CHECK(en_keys_note(0, 1) == en_keys_note(12, 0), "(the same note)");
    en_keys(KEY(12));
    block();
    en_keys(0u);
    block();
    CHECK(en_test_bells_on() == 0, "one note, two keys: %d down", en_test_bells_on());
    pairs_ok("one note, two keys");
    ledger = 0;
    /* a sound chosen: that one. Auto: Tone's, and it stays put, for minutes */
    start(19u, EN_HANDS, EN_MEL_KEYS);
    en_set(P_MELSND, 0);
    en_set(P_MTONE, 70);
    en_play(1);
    block();
    {
        uint32_t i;
        for (i = 0u; i < 400u; i++) {
            en_keys(i & 1u ? 0u : KEY(i % 27u));
            en_test_ticks(16u);
            block();
            if (!(i & 1u))
                CHECK(en_test_bell_struck == en_keys_timbre(70), "Auto: struck %d, Tone's %d", en_test_bell_struck, en_keys_timbre(70));
        }
    }
    en_keys(0u);
    block();
    /* the mode Keys and no key: nothing, no notes, no voices; and the other layers go on */
    start(21u, EN_HANDS, EN_MEL_KEYS);
    en_play(1);
    run(SEC(20.0f), 256u);
    CHECK(ons == 0u && voices_most == 0u, "Keys and no key: %u notes, %u voices", ons, voices_most);
    CHECK(other_nd[0] > 0, "the keys' layer?");
}

/* ---- the latency: the block after it was posted. No grid, no waiting */
static void t_latency(void)
{
    uint32_t i;
    for (i = 0u; i < 64u; i++) {
        start(25u, EN_HANDS, EN_MEL_KEYS);
        en_play(1);
        run(SEC(0.5f) + i * 256u, 256u);
        counts_clear();
        en_keys(KEY(1));
        block();
        CHECK(ons == 1u, "the block after a press, %u: %u notes", i, ons);
        en_keys(0u);
        block();
        CHECK(offs == 1u, "the block after a release, %u: %u offs", i, offs);
    }
}

/* ---- the fifth key: the oldest let go, no click, four voices at most. Which is stolen and what comes of its key */
static void t_steal(void)
{
    int t;
    for (t = 0; t < EN_NTIMBRES; t++) {
        int k;
        uint32_t off;
        /* no click: at many moments of the wave, the largest step as the fifth comes, against the same moment with the same notes coming and the oldest never having played: a wave cut off is a step of its whole height, many of its slope */
        for (off = 0u; off < 24u; off++) {
            double step[2];
            int steal;
            for (steal = 0; steal < 2; steal++) {
                const uint32_t oldest = steal ? KEY(0) : 0u;
                start(31u, EN_HANDS, EN_MEL_KEYS);
                only_melody();
                en_set(P_MELSND, t + 1);
                en_set(P_MOTIFS, 100);
                block();
                en_keys(oldest);
                run(SEC(0.03f), 256u);
                en_keys(oldest | KEY(4));
                run(SEC(0.03f), 256u);
                en_keys(oldest | KEY(4) | KEY(7));
                run(SEC(0.03f), 256u);
                en_keys(oldest | KEY(4) | KEY(7) | KEY(11));
                run(SEC(0.15f) + off * 37u, 256u);
                meter_clear();
                en_keys(oldest | KEY(4) | KEY(7) | KEY(11) | KEY(14));
                run(512u, 64u);
                step[steal] = step_most;
                CHECK(steal ? ons == 5u && offs == 1u : ons == 4u && offs == 0u, "(the fifth: %u, %u)", ons, offs);
            }
            CHECK(step[1] <= step[0] * 1.25 + 2000.0, "timbre %d, moment %u: a steal's step of %f, %f with no steal: a click", t, off, step[1], step[0]);
        }
        start(31u, EN_HANDS, EN_MEL_KEYS);
        only_melody();
        en_set(P_MELSND, t + 1);
        en_set(P_MOTIFS, 100);
        block();
        ledger = 1;
        en_keys(KEY(0));
        run(SEC(0.2f), 256u);
        en_keys(KEY(0) | KEY(4));
        run(SEC(0.2f), 256u);
        en_keys(KEY(0) | KEY(4) | KEY(7));
        run(SEC(0.2f), 256u);
        en_keys(KEY(0) | KEY(4) | KEY(7) | KEY(11));
        run(SEC(0.2f), 256u);
        CHECK(voices_most == 4u, "timbre %d: four keys, %u voices", t, voices_most);
        CHECK(en_test_bells_on() == 4, "four keys, %d down", en_test_bells_on());
        counts_clear();
        en_keys(KEY(0) | KEY(4) | KEY(7) | KEY(11) | KEY(14));
        block();
        CHECK(ons == 1u && last_on == en_keys_note(14, 0), "the fifth: %u notes, the last %d", ons, last_on);
        CHECK(offs == 1u && last_off == en_keys_note(0, 0), "the fifth takes the oldest: %u offs, the last %d", offs, last_off);
        CHECK(en_test_bell_voices() <= 4, "the fifth: %d voices", en_test_bell_voices());
        run(SEC(0.2f), 256u);
        CHECK(voices_most <= 4u, "timbre %d: %u voices", t, voices_most);
        /* the key stolen comes up: it has nothing to say */
        counts_clear();
        en_keys(KEY(4) | KEY(7) | KEY(11) | KEY(14));
        block();
        CHECK(offs == 0u, "a key stolen, up: %u offs", offs);
        /* the sixth and seventh down at once, the next oldest first */
        en_keys(KEY(4) | KEY(7) | KEY(11) | KEY(14) | KEY(16) | KEY(18));
        block();
        CHECK(offs == 2u && ons == 2u, "two more: %u on, %u off", ons, offs);
        CHECK(!down[en_keys_note(4, 0)] && !down[en_keys_note(7, 0)], "the two oldest should have gone");
        CHECK(en_test_bells_on() == 4, "%d down", en_test_bells_on());
        /* a palm on all of them, in one block: the top four, and no more */
        en_keys(0x7FFFFFFu);
        block();
        CHECK(en_test_bells_on() == 4 && en_test_bell_voices() <= 4, "all of them: %d down, %d voices", en_test_bells_on(), en_test_bell_voices());
        for (k = 23; k < 27; k++)
            CHECK(down[en_keys_note(k, 0)], "all of them: key %d?", k);
        run(SEC(0.1f), 256u);
        en_keys(0u);
        block();
        CHECK(en_test_bells_on() == 0 && nd == 0, "none: %d, %d", en_test_bells_on(), nd);
        pairs_ok("the fifth");
        ledger = 0;
        CHECK(!en_test_nans, "NaN");
    }
    /* a tail goes before a key that is down */
    start(33u, EN_HANDS, EN_MEL_KEYS);
    en_set(P_MELSND, EN_TB_GLASS + 1);
    block();
    en_keys(KEY(0));
    block();
    en_keys(KEY(0) | KEY(1));
    block();
    en_keys(KEY(0) | KEY(1) | KEY(2));
    block();
    en_keys(KEY(0) | KEY(1) | KEY(2) | KEY(3));
    block();
    en_keys(KEY(0) | KEY(1) | KEY(2));           /* key 3 up: a tail */
    block();
    counts_clear();
    en_keys(KEY(0) | KEY(1) | KEY(2) | KEY(4));
    block();
    CHECK(ons == 1u && offs == 0u, "a tail goes first: %u, %u", ons, offs);
    CHECK(en_test_bells_on() == 4, "%d down", en_test_bells_on());
    en_keys(0u);
    block();
    /* how loud, at the top of the level: four keys of the loudest sounds and it stays under the limiter's knee by a margin it has anyway */
    for (t = 0; t < EN_NTIMBRES; t++) {
        start(35u, EN_HANDS, EN_MEL_KEYS);
        only_melody();
        en_set(P_MELSND, t + 1);
        en_set(P_MOTIFS, 100);
        block();
        en_keys(KEY(0) | KEY(4) | KEY(7) | KEY(11));
        meter_clear();
        run(SEC(1.0f), 256u);
        printf("  four keys, %-9s level 100: peak %.3f, in at the limiter %.3f\n", EN_TIMBRE_NAME[t], peak, (double)en_test_peak_in);
        CHECK(peak < 1.0, "timbre %d: %f", t, peak);
        en_keys(0u);
        block();
    }
}

/* ---- nothing hangs. Each: a key down, the thing, and at the end no note on the channel, no voice */
static void all_gone(const char *what)
{
    run(SEC(2.5f), 256u);
    CHECK(nd == 0, "%s: %d notes down for MIDI", what, nd);
    CHECK(en_test_bells_on() == 0, "%s: %d down for the engine", what, en_test_bells_on());
    CHECK(en_test_bell_voices() == 0, "%s: %d voices", what, en_test_bell_voices());
    CHECK(en_hand_set == 0u, "%s: a set of %x", what, en_hand_set);
    pairs_ok(what);
}
static void piece_now(int i, int melmode)        /* as ui.c does: the hand's first, then the values */
{
    int16_t par[P_NPARAMS];
    uint16_t pool;
    int p;
    en_keys(0u);
    en_keys_clear();
    en_piece(i, par, &pool);
    par[P_MELMODE] = (int16_t)melmode;
    for (p = 0; p < P_NPARAMS; p++)
        en_set(p, par[p]);
    en_pool(pool);
    en_seed(1000u + (uint32_t)i);
    en_regrow();
}
static void t_no_hang(void)
{
    int mode;
    for (mode = EN_MEL_KEYS; mode <= EN_MEL_KEYS_ARP; mode++) {
        const uint32_t two = KEY(3) | KEY(9);
        int to;
        /* a panic */
        start(41u, EN_HANDS, mode);
        en_set(P_MRATE, 100);
        en_play(1);
        run(SEC(0.5f), 256u);
        ledger = 1;
        en_keys(two);
        run(SEC(0.5f), 256u);
        CHECK(ons > 0u, "mode %d: nothing played", mode);
        en_play(0);
        en_panic();
        block();
        ledger = 0;
        CHECK(nd == 0 && en_test_bell_voices() == 0, "a panic, mode %d: %d, %d", mode, nd, en_test_bell_voices());
        counts_clear();
        run(SEC(0.5f), 256u);
        CHECK(ons == 0u, "silent, keys down, mode %d: %u notes", mode, ons);
        /* (the panel lets go at a panic: ui_panic) a restore and it does not come back by itself */
        en_keys(0u);
        block();
        en_restore();
        run(SEC(3.0f), 256u);
        CHECK(ons == 0u && voices_most == 0u, "after a panic, mode %d: %u notes, %u voices", mode, ons, voices_most);
        en_keys(KEY(2));
        run(SEC(0.5f), 256u);
        CHECK(ons > 0u, "after a panic, a key, mode %d: nothing", mode);
        en_keys(0u);
        all_gone("after a panic");
        /* a keys word still down at the panic and through the silence: it stays out until it comes up and goes down again? it plays on the way back: no note hanging either way */
        start(43u, EN_HANDS, mode);
        en_set(P_MRATE, 100);
        en_play(1);
        run(SEC(0.2f), 256u);
        en_keys(two);
        run(SEC(0.2f), 256u);
        en_play(0);
        en_panic();
        run(SEC(0.3f), 256u);
        en_restore();
        run(SEC(3.0f), 256u);
        en_keys(0u);
        all_gone("down through a panic");
        /* a new piece, in the middle of a note, to each mode */
        for (to = EN_MEL_PHRASES; to <= EN_MEL_KEYS_ARP; to++) {
            start(45u + (uint32_t)to, EN_HANDS, mode);
            en_set(P_MRATE, 100);
            en_play(1);
            run(SEC(0.3f), 256u);
            ledger = 1;
            en_keys(two);
            run(SEC(0.3f), 256u);
            piece_now(to + 1, to);
            run(SEC(3.0f), 256u);
            ledger = 0;
            CHECK(en_hand_set == 0u, "a piece, %d to %d: a set of %x", mode, to, en_hand_set);
            if (to != EN_MEL_PHRASES && to != EN_MEL_ARP)
                CHECK(nd == 0 && en_test_bell_voices() == 0, "a piece, %d to %d: %d down, %d voices", mode, to, nd, en_test_bell_voices());
            pairs_ok("a piece");
            /* a mode change, with the keys down, to each */
            start(55u + (uint32_t)to, EN_HANDS, mode);
            en_set(P_MRATE, 100);
            en_play(1);
            run(SEC(0.3f), 256u);
            ledger = 1;
            en_keys(two);
            run(SEC(0.3f), 256u);
            en_set(P_MELMODE, to);
            run(SEC(0.5f), 256u);
            if (to < EN_MEL_KEYS) {
                CHECK(!en_hand_set, "a mode, %d to %d: a set of %x", mode, to, en_hand_set);
                /* (the panel forgets them: ui.c; here the word is still up, and nothing comes of it) */
            }
            en_keys(0u);
            run(SEC(1.0f), 256u);
            en_set(P_MELMODE, EN_MEL_KEYS);
            all_gone("a mode change");
            ledger = 0;
        }
        /* the melody off: unheard, its MIDI the same; a key up as well */
        start(61u, EN_HANDS, mode);
        only_melody();
        en_set(P_MRATE, 100);
        en_play(1);
        run(SEC(0.3f), 256u);
        ledger = 1;
        en_layer(EN_L_MEL, 0);
        run(SEC(1.5f), 256u);
        en_keys(two);
        meter_clear();
        counts_clear();
        run(SEC(1.0f), 256u);
        CHECK(ons > 0u, "muted, mode %d: its MIDI stopped", mode);
        CHECK(rms_now() < 1e-5, "muted, mode %d: %f", mode, rms_now());
        en_keys(0u);
        all_gone("a mute");
        ledger = 0;
        /* a rest in the middle of it; a duck */
        start(63u, EN_HANDS, mode);
        en_set(P_MRATE, 100);
        en_play(1);
        run(SEC(0.3f), 256u);
        ledger = 1;
        en_keys(two);
        run(SEC(0.3f), 256u);
        en_play(0);
        run(SEC(0.3f), 256u);
        en_keys(0u);
        run(SEC(0.3f), 256u);
        all_gone("a rest");
        en_play(1);
        run(SEC(0.3f), 256u);
        en_keys(two);
        run(SEC(0.2f), 256u);
        en_duck(1);
        run(SEC(0.4f), 256u);
        CHECK(en_ducked, "not at zero");
        en_keys(0u);                             /* the key comes up under the write */
        run(SEC(0.2f), 256u);
        en_duck(0);
        run(SEC(0.5f), 256u);
        all_gone("a duck");
        ledger = 0;
        /* a queue flood: more than it holds, a key let go in the middle of it, in the same block: gone at the next block all the same */
        start(65u, EN_HANDS, mode);
        en_set(P_MRATE, 100);
        en_play(1);
        run(SEC(0.3f), 256u);
        ledger = 1;
        en_keys(two);
        run(SEC(0.3f), 256u);
        {
            uint32_t i, seen;
            for (i = 0u; i < 200u; i++)
                en_set(P_BLOOM, (int)(i % 100u));
            CHECK(en_room() == 0u && en_dropped(), "(not a flood)");
            en_keys(0u);
            for (i = 0u; i < 200u; i++)
                en_set(P_BLOOM, (int)(i % 100u));
            for (i = 0u; i < 200u; i++)
                en_next();                       /* (the places kept for a hand: full up, too) */
            CHECK(en_room() == 0u, "(not a flood)");
            block();
            seen = en_hand_set;
            CHECK(!seen, "a flood, mode %d: a set of %x after the next block", mode, seen);
            if (mode == EN_MEL_KEYS)
                CHECK(en_test_bells_on() == 0, "a flood: a note left down: %d", en_test_bells_on());
            CHECK(en_keys_seen() == 0u, "a flood: the release not seen");
        }
        all_gone("a flood");
        ledger = 0;
    }
    /* MIDI out off with a key down, and on again: nothing owed */
    start(71u, EN_HANDS, EN_MEL_KEYS);
    block();
    ledger = 0;
    en_keys(KEY(5));
    block();
    en_set(P_MIDI, 0);
    block();
    CHECK(nd == 0, "MIDI out off: %d", nd);
    en_keys(0u);
    block();
    en_set(P_MIDI, 1);
    block();
    CHECK(nd == 0 && en_test_bells_on() == 0, "MIDI out on: %d, %d", nd, en_test_bells_on());
}

/* ---- playing in a rest: Keys sounds, in the room; Keys Arp waits for the grid. What the rest would have done is not moved */
static void t_rest(void)
{
    start(81u, EN_HANDS, EN_MEL_KEYS);
    only_melody();
    en_set(P_REVERB, 65);
    en_play(1);
    run(SEC(0.5f), 256u);
    en_play(0);
    run(SEC(0.5f), 256u);
    counts_clear();
    meter_clear();
    ledger = 1;
    en_keys(KEY(7));
    run(SEC(0.5f), 256u);
    CHECK(ons == 1u, "in a rest: %u notes", ons);
    CHECK(peak > 0.005, "in a rest: not heard: %f", peak);
    en_keys(0u);
    block();
    CHECK(offs == 1u, "in a rest, up: %u", offs);
    meter_clear();
    run(SEC(0.2f), 256u);
    CHECK(peak > 0.0, "in a rest, its tail and the room");
    all_gone("playing in a rest");
    ledger = 0;
    /* Keys Arp: no grid, no note. On the way back, from the next step */
    start(83u, EN_HANDS, EN_MEL_KEYS_ARP);
    en_set(P_MRATE, 100);
    en_play(1);
    run(SEC(0.5f), 256u);
    en_play(0);
    run(SEC(0.5f), 256u);
    counts_clear();
    ledger = 1;
    en_keys(KEY(0) | KEY(4));
    run(SEC(1.0f), 256u);
    CHECK(ons == 0u, "Keys Arp in a rest: %u notes", ons);
    en_play(1);
    run(SEC(0.3f), 256u);
    CHECK(ons > 2u, "Keys Arp, waking: %u notes", ons);
    en_keys(0u);
    all_gone("Keys Arp in a rest");
    ledger = 0;
}

/* ---- Keys Arp: what a known hold plays, in each pattern, at each division, on the grid */
static void hold_start(uint32_t seed, int rate, int pattern, int skip, uint32_t keys)
{
    start(seed, EN_HANDS, EN_MEL_KEYS_ARP);
    en_set(P_TEMPO, 170);
    en_set(P_MRATE, rate);
    en_set(P_ARPPAT, pattern);
    en_set(P_ARPSKIP, skip);
    en_play(1);
    run(SEC(0.5f), 256u);
    en_keys(keys);
    counts_clear();
}
static void t_order(void)
{
    static const int RATE[3] = {10, 50, 90}, DIV[3] = {4, 2, 1};
    /* (Played: as they were pressed; three blocks apart) */
    static const int UP[3] = {0, 4, 7}, DOWN[3] = {7, 4, 0}, UPDOWN[4] = {0, 4, 7, 4}, CONV[3] = {0, 7, 4}, PEDT[4] = {7, 0, 7, 4}, PEDR[4] = {0, 4, 0, 7}, PLAYED[3] = {4, 7, 0};
    static const struct { int p, n; const int *w; } W[] = {{EN_ARP_UP, 3, UP}, {EN_ARP_DOWN, 3, DOWN}, {EN_ARP_UPDOWN, 4, UPDOWN}, {EN_ARP_CONVERGE, 3, CONV}, {EN_ARP_PEDTOP, 4, PEDT}, {EN_ARP_PEDROOT, 4, PEDR}, {EN_ARP_PLAYED, 3, PLAYED}};
    int d;
    uint32_t q;
    for (d = 0; d < 3; d++)
        for (q = 0u; q < sizeof W / sizeof W[0]; q++) {
            uint32_t i, from = 0u;
            hold_start(100u + q, RATE[d], W[q].p, 0, KEY(4));
            ledger = 1;
            en_keys(KEY(4));
            en_keys(KEY(4) | KEY(7));
            en_keys(KEY(4) | KEY(7) | KEY(0));   /* (one word: a block's keys go in by pitch. Played's order is made below) */
            if (W[q].p == EN_ARP_PLAYED) {
                en_keys(0u);
                block();
                counts_clear();
                en_keys(KEY(4));
                block();
                en_keys(KEY(4) | KEY(7));
                block();
                en_keys(KEY(4) | KEY(7) | KEY(0));
                block();
            } else {
                block();
            }
            run(SEC(6.0f), 256u);
            /* the first may be off the grid; from the first note on the grid, the figure */
            while (from < rec_n && (rec_step[from] - 0u) % (uint32_t)DIV[d])
                from++;
            CHECK(rec_n > 8u, "division %d pattern %d: %u notes", DIV[d], W[q].p, rec_n);
            if (d == 2 || W[q].p != EN_ARP_PLAYED) {
                /* what follows in the figure, whichever place it came in at */
                int at = -1, k;
                for (k = 0; k < W[q].n && at < 0; k++) {
                    int ok = 1;
                    for (i = 0u; i < 12u && from + i < rec_n; i++)
                        ok &= rec[from + i] == en_keys_note(W[q].w[(k + (int)i) % W[q].n], 0);
                    if (ok)
                        at = k;
                }
                CHECK(at >= 0, "division %d pattern %d: not the figure: %d %d %d %d %d %d", DIV[d], W[q].p, rec[from], rec[from + 1], rec[from + 2], rec[from + 3], rec[from + 4], rec[from + 5]);
                for (i = from; i < rec_n; i++)
                    CHECK(rec[i] == en_keys_note(W[q].w[(at + (int)(i - from)) % W[q].n], 0), "division %d pattern %d: note %u is %d", DIV[d], W[q].p, i, rec[i]);
            }
            for (i = from; i < rec_n; i++)
                CHECK(!((rec_step[i]) % (uint32_t)DIV[d]), "division %d pattern %d: a note off the grid at step %u", DIV[d], W[q].p, rec_step[i]);
            for (i = from + 1u; i < rec_n; i++)
                CHECK(rec_step[i] - rec_step[i - 1u] == (uint32_t)DIV[d], "division %d pattern %d: from step %u to %u", DIV[d], W[q].p, rec_step[i - 1u], rec_step[i]);
            CHECK(most_down <= 1, "division %d pattern %d: %d down at once", DIV[d], W[q].p, most_down);
            CHECK(voices_most <= 3u || DIV[d] > 1, "division %d pattern %d: %u voices", DIV[d], W[q].p, voices_most);
            en_keys(0u);
            all_gone("the figure");
            ledger = 0;
        }
    /* the first of a new hold, after silence: the next sixteenth, whatever the division; then the grid. Skip 100: nothing */
    for (d = 0; d < 3; d++) {
        int off;
        for (off = 0; off < 8; off++) {
            uint32_t s0;
            hold_start(130u, RATE[d], EN_ARP_UP, 0, 0u);
            run((uint32_t)off * 1024u, 256u);
            counts_clear();
            s0 = en_test_song();
            en_keys(KEY(2));
            run(SEC(0.25f), 256u);
            CHECK(rec_n >= 1u && rec_step[0] <= s0, "division %d, off %d: the first at step %u, the next step was %u", DIV[d], off, rec_n ? rec_step[0] : 0u, s0);
            en_keys(0u);
            all_gone("the first of a hold");
        }
        hold_start(132u, RATE[d], EN_ARP_UP, 100, KEY(2));
        run(SEC(2.0f), 256u);
        CHECK(ons == 0u, "Skip 100, division %d: %u notes", DIV[d], ons);
        en_keys(0u);
        all_gone("Skip 100");
    }
}

/* ---- Skip: the same rule as Arp's, on the same grid; the figure goes on through its rests */
static void t_skip(void)
{
    static const int RATE[3] = {10, 50, 90}, DIV[3] = {4, 2, 1};
    int d;
    for (d = 0; d < 3; d++) {
        static uint8_t zero[REC];
        uint32_t i, st0 = 0u, seed = 140u + (uint32_t)d;
        uint32_t rests = 0u, grid = 0u, wrong = 0u, s;
        memset(zero, 0, sizeof zero);
        hold_start(seed, RATE[d], EN_ARP_UP, 0, KEY(0) | KEY(5) | KEY(9));
        run(SEC(20.0f), 256u);
        for (i = 0u; i < rec_n; i++)
            if (rec_step[i] < REC)
                zero[rec_step[i]] = rec[i];
        st0 = rec_step[0];
        hold_start(seed, RATE[d], EN_ARP_UP, 50, KEY(0) | KEY(5) | KEY(9));
        run(SEC(20.0f), 256u);
        {
            static uint8_t got[REC];
            memset(got, 0, sizeof got);
            for (i = 0u; i < rec_n; i++)
                if (rec_step[i] < REC)
                    got[rec_step[i]] = rec[i];
            for (s = (st0 + 16u) / (uint32_t)DIV[d] * (uint32_t)DIV[d]; s < rec_step[rec_n - 1u]; s += (uint32_t)DIV[d]) {
                const int r = en_arp_rest(en_test_seed(), s, DIV[d], 50);
                grid++;
                rests += !got[s];
                wrong += r ? got[s] != 0u : got[s] != zero[s];
            }
        }
        printf("  Keys Arp, Skip 50 at %d: %u of %u steps rest, %u not by the rule\n", DIV[d], rests, grid, wrong);
        CHECK(!wrong, "division %d: %u steps not as Skip 0 less its rests", DIV[d], wrong);
        CHECK(rests * 100u > grid * 40u && rests * 100u < grid * 60u, "division %d: %u of %u rest", DIV[d], rests, grid);
        en_keys(0u);
        all_gone("Skip");
    }
}

/* ---- keys added and let go mid-figure: the grid and the bar do not move, the figure goes on, nothing hangs */
static void t_changes(void)
{
    uint32_t i, step_was;
    hold_start(150u, 90, EN_ARP_UP, 0, KEY(0) | KEY(7));
    ledger = 1;
    run(SEC(1.0f), 256u);
    step_was = rec_n ? rec_step[rec_n - 1u] : 0u;
    counts_clear();
    en_keys(KEY(0) | KEY(7) | KEY(3));           /* a key added */
    run(SEC(1.0f), 256u);
    CHECK(rec_n > 4u, "added: %u notes", rec_n);
    CHECK(rec_step[0] == step_was + 1u || rec_step[0] == step_was + 2u, "added: from step %u to %u: the grid moved", step_was, rec_step[0]);
    {
        int saw3 = 0;
        for (i = 0u; i < rec_n; i++) {
            saw3 |= rec[i] == en_keys_note(3, 0);
            CHECK(rec[i] == en_keys_note(0, 0) || rec[i] == en_keys_note(3, 0) || rec[i] == en_keys_note(7, 0), "added: note %d", rec[i]);
            if (i)
                CHECK(rec_step[i] == rec_step[i - 1u] + 1u, "added: a gap from %u to %u", rec_step[i - 1u], rec_step[i]);
        }
        CHECK(saw3, "added: it never came");
    }
    step_was = rec_step[rec_n - 1u];
    counts_clear();
    en_keys(KEY(3));                             /* two let go */
    run(SEC(0.5f), 256u);
    for (i = 0u; i < rec_n; i++)
        CHECK(rec[i] == en_keys_note(3, 0), "let go: note %d", rec[i]);
    counts_clear();
    en_keys(0u);                                 /* the last: it stops, and its key is up in the block */
    block();
    CHECK(en_test_bells_on() == 0 && nd == 0, "all let go: %d down, %d for MIDI", en_test_bells_on(), nd);
    counts_clear();
    run(SEC(1.0f), 256u);
    CHECK(ons == 0u, "all let go: %u notes more", ons);
    all_gone("keys coming and going");
    ledger = 0;
    /* a pattern's or a division's change, the keys down */
    hold_start(152u, 90, EN_ARP_UP, 0, KEY(0) | KEY(4) | KEY(7));
    ledger = 1;
    for (i = 0u; i < 400u; i++) {
        en_set(P_ARPPAT, (int)(i % EN_ARP_NPATTERNS));
        en_set(P_MRATE, (int)(i * 37u % 101u));
        en_set(P_ARPSKIP, (int)(i * 13u % 101u));
        en_set(P_MOCT, (int)(i % 5u) - 2);
        run(256u * (1u + i % 5u), 256u);
    }
    en_keys(0u);
    all_gone("the knobs, keys down");
    ledger = 0;
}

/* ---- Latch */
static void t_latch(void)
{
    uint32_t i;
    hold_start(160u, 90, EN_ARP_UP, 0, KEY(0));
    en_set(P_ARPLATCH, 1);
    ledger = 1;
    block();
    en_keys(KEY(0) | KEY(4));
    run(SEC(0.3f), 256u);
    en_keys(0u);                                 /* let go, and on it plays */
    block();
    CHECK(en_hand_set == (KEY(0) | KEY(4)), "latched: a set of %x", en_hand_set);
    counts_clear();
    run(SEC(1.0f), 256u);
    CHECK(rec_n > 6u, "latched, let go: %u notes", rec_n);
    for (i = 0u; i < rec_n; i++)
        CHECK(rec[i] == en_keys_note(0, 0) || rec[i] == en_keys_note(4, 0), "latched: note %d", rec[i]);
    /* a press after all were let go: a new set */
    en_keys(KEY(9));
    block();
    CHECK(en_hand_set == KEY(9), "replaced: a set of %x", en_hand_set);
    run(256u * 40u, 256u);
    counts_clear();
    run(SEC(1.0f), 256u);
    for (i = 0u; i < rec_n; i++)
        CHECK(rec[i] == en_keys_note(9, 0), "replaced: note %d", rec[i]);
    /* a press with one still down: added */
    en_keys(KEY(9) | KEY(11));
    block();
    CHECK(en_hand_set == (KEY(9) | KEY(11)), "added: a set of %x", en_hand_set);
    /* one let go, and the other still down: it stays in the set; another down then: added */
    en_keys(KEY(11));
    block();
    CHECK(en_hand_set == (KEY(9) | KEY(11)), "let go, one down: a set of %x", en_hand_set);
    en_keys(KEY(11) | KEY(14));
    block();
    CHECK(en_hand_set == (KEY(9) | KEY(11) | KEY(14)), "added again: a set of %x", en_hand_set);
    /* latch off: only what is held plays; all gone if none is */
    en_set(P_ARPLATCH, 0);
    block();
    CHECK(en_hand_set == (KEY(11) | KEY(14)), "latch off, two held: a set of %x", en_hand_set);
    en_set(P_ARPLATCH, 1);
    en_keys(KEY(11));
    block();
    CHECK(en_hand_set == (KEY(11) | KEY(14)), "latch on: a set of %x", en_hand_set);
    en_keys(0u);
    block();
    en_set(P_ARPLATCH, 0);
    block();
    CHECK(en_hand_set == 0u, "latch off, none held: a set of %x", en_hand_set);
    CHECK(en_test_bells_on() == 0, "latch off, none held: %d down", en_test_bells_on());
    all_gone("latch off");
    /* a mute: it stops being heard, the set stays and its MIDI goes on */
    start(162u, EN_HANDS, EN_MEL_KEYS_ARP);
    only_melody();
    en_set(P_MRATE, 90);
    en_set(P_ARPLATCH, 1);
    en_play(1);
    run(SEC(0.5f), 256u);
    en_keys(KEY(0) | KEY(4));
    block();
    en_keys(0u);
    block();
    en_layer(EN_L_MEL, 0);
    run(SEC(1.5f), 256u);
    meter_clear();
    counts_clear();
    run(SEC(1.0f), 256u);
    CHECK(rms_now() < 1e-5, "latched and muted: %f", rms_now());
    CHECK(ons > 6u, "latched and muted: %u", ons);
    CHECK(en_hand_set == (KEY(0) | KEY(4)), "latched and muted: a set of %x", en_hand_set);
    en_layer(EN_L_MEL, 1);
    run(SEC(1.5f), 256u);
    meter_clear();
    run(SEC(0.5f), 256u);
    CHECK(rms_now() > 1e-4, "latched, back on: %f", rms_now());
    /* a rest: it stops striking; the set is kept; on waking it plays on */
    en_play(0);
    run(SEC(0.3f), 256u);
    counts_clear();
    run(SEC(1.0f), 256u);
    CHECK(ons == 0u && en_hand_set == (KEY(0) | KEY(4)), "latched, resting: %u notes, a set of %x", ons, en_hand_set);
    en_play(1);
    run(SEC(0.5f), 256u);
    CHECK(ons > 2u, "latched, waking: %u notes", ons);
    /* a panic: gone, and not back */
    ledger = 1;
    en_play(0);
    en_panic();
    run(SEC(0.5f), 256u);
    en_restore();
    run(SEC(3.0f), 256u);
    CHECK(en_hand_set == 0u, "latched, a panic: a set of %x", en_hand_set);
    counts_clear();
    run(SEC(1.0f), 256u);
    CHECK(ons == 0u, "latched, after a panic: %u notes", ons);
    all_gone("latched, a panic");
    /* a new piece: gone; by the clear word, whatever the mode the piece has */
    hold_start(164u, 90, EN_ARP_UP, 0, KEY(0) | KEY(4));
    en_set(P_ARPLATCH, 1);
    block();
    en_keys(0u);
    run(SEC(0.2f), 256u);
    CHECK(en_hand_set == (KEY(0) | KEY(4)), "latched: a set of %x", en_hand_set);
    en_keys_clear();
    block();
    CHECK(en_hand_set == 0u, "cleared: a set of %x", en_hand_set);
    all_gone("latched, a piece");
    /* another mode: the set goes */
    hold_start(166u, 90, EN_ARP_UP, 0, KEY(0));
    en_set(P_ARPLATCH, 1);
    block();
    en_keys(0u);
    run(SEC(0.2f), 256u);
    en_set(P_MELMODE, EN_MEL_KEYS);
    block();
    CHECK(en_hand_set == 0u, "out of Keys Arp: a set of %x", en_hand_set);
    all_gone("latched, a mode");
    en_set(P_MELMODE, EN_MEL_KEYS_ARP);
    block();
    counts_clear();
    run(SEC(1.0f), 256u);
    CHECK(ons == 0u, "back in Keys Arp: %u notes of the old set", ons);
    /* latch in Keys has no say: a key comes up, its note lets go */
    start(168u, EN_HANDS, EN_MEL_KEYS);
    en_set(P_ARPLATCH, 1);
    block();
    en_keys(KEY(1));
    block();
    en_keys(0u);
    block();
    CHECK(en_test_bells_on() == 0, "Keys with the latch: %d down", en_test_bells_on());
    ledger = 0;
}

/* ---- what is not Keys does not hear the word, to the bit; and a Keys that nobody plays has no MIDI on the channel */
static void t_others(void)
{
    int m;
    for (m = EN_MEL_PHRASES; m <= EN_MEL_ARP; m++) {
        uint32_t a, h;
        uint32_t i, n;
        start(171u, EN_HANDS, m);
        en_layer(EN_L_MEL, 1);
        en_set(P_MRATE, 90);
        en_play(1);
        run(SEC(20.0f), 256u);
        a = ahash;
        h = mhash;
        n = midi_n;
        start(171u, EN_HANDS, m);
        en_layer(EN_L_MEL, 1);
        en_set(P_MRATE, 90);
        en_play(1);
        midi_n = 0u;
        for (i = 0u; i < SEC(20.0f); i += 256u) {
            if (!(i % 5120u))
                en_keys(i >> 8 & 0x7FFFFFFu);
            if (!(i % 25600u))
                en_keys_clear();
            block();
        }
        en_keys(0u);
        CHECK(ahash == a && mhash == h && midi_n == n, "mode %d: the word was heard", m);
    }
}

/* ---- the ledger: random presses, thousands, with everything else a hand can do between them */
static void t_random(void)
{
    uint32_t sd, ev = 0u;
    for (sd = 0u; sd < 8u; sd++) {
        uint32_t h = 2166136261u + sd * 977u, i, keys = 0u;
        start(200u + sd, sd & 1u ? EN_AUTO : EN_HANDS, EN_MEL_KEYS + (int)(sd >> 1 & 1u));
        en_set(P_MRATE, 90);
        en_play(1);
        run(SEC(0.2f), 256u);
        ledger = 1;
        counts_clear();
        for (i = 0u; i < 12000u; i++) {
            uint32_t r;
            h = h * 16777619u + 7u;
            r = h >> 8;
            switch (h >> 28) {
            case 0: case 1: case 2: case 3: case 4: case 5: case 6: case 7:
                keys ^= KEY(r % 27u);            /* a key */
                if (__builtin_popcount(keys) > 6)
                    keys &= r;
                en_keys(keys);
                ev++;
                break;
            case 8:
                if (!(r % 8u))
                    en_set(P_MELMODE, (int)(r >> 3 & 3u));
                ev++;
                break;
            case 9:
                en_set(P_ARPLATCH, (int)(r & 1u));
                ev++;
                break;
            case 10:
                en_set(r & 1u ? P_ARPPAT : P_ARPSKIP, (int)((r >> 1) % (r & 1u ? EN_ARP_NPATTERNS : 101u)));
                ev++;
                break;
            case 11:
                if (!(r % 16u))
                    en_set(P_MOCT, (int)(r >> 4 & 3u) - 2);
                else if (!(r % 16u - 1u))
                    en_set(P_MELSND, (int)((r >> 4) % 7u));
                else if (!(r % 16u - 2u))
                    en_set(P_MRATE, (int)((r >> 4) % 101u));
                ev++;
                break;
            case 12:
                if (!(r % 32u)) {
                    en_layer(EN_L_MEL, (int)(r >> 5 & 1u));
                    ev++;
                }
                break;
            case 13:
                if (!(r % 16u)) {
                    en_play((int)(r >> 4 & 1u));
                    ev++;
                }
                break;
            case 14:
                if (!(r % 256u)) {               /* a panic and the way back: the panel lets go */
                    en_play(0);
                    en_panic();
                    en_keys(0u);
                    en_keys_clear();
                    keys = 0u;
                    run(256u * (r >> 8 & 15u), 256u);
                    en_restore();
                    ev++;
                } else if (!(r % 128u - 1u)) {   /* a duck */
                    en_duck((int)(r >> 8 & 1u));
                    ev++;
                }
                break;
            default:
                if (!(r % 64u)) {                /* a piece */
                    piece_now((int)((r >> 6) % 8u), (int)(r >> 12 & 3u));
                    keys = 0u;
                    ev++;
                } else if (!(r % 64u - 1u)) {    /* a flood */
                    uint32_t j;
                    for (j = 0u; j < 100u; j++)
                        en_set(P_BLOOM, (int)(j % 101u));
                    en_dropped();
                    ev++;
                }
                break;
            }
            run(32u + 32u * (r >> 20 & 15u), 256u);
            CHECK(en_test_bell_voices() <= 4, "%d voices", en_test_bell_voices());
        }
        pairs_ok("random");
        CHECK(!en_test_nans, "random: NaN");
        /* and it all comes down */
        en_keys(0u);
        en_keys_clear();
        en_set(P_ARPLATCH, 0);
        en_set(P_MELMODE, EN_MEL_KEYS);
        en_duck(0);
        en_layer(EN_L_MEL, 1);
        run(SEC(0.5f), 256u);
        all_gone("random");
        ledger = 0;
        CHECK(!led_bad, "random: the ledger: %u bad blocks", led_bad);
    }
    printf("  random presses: %u events over 8 seeds\n", ev);
    CHECK(ev > 50000u, "only %u events", ev);
}

/* ---- the queue: a piece is 42 commands; the hand's keys do not go through it at all */
static void t_queue(void)
{
    uint32_t i;
    printf("  a piece: %d commands, the queue %u\n", P_NPARAMS + 6, EN_ROOM_MAX);
    CHECK(P_NPARAMS + 6 == 42, "a piece's commands: %d", P_NPARAMS + 6);
    CHECK(P_NPARAMS + 6 <= (int)EN_ROOM_MAX, "a piece no longer fits");
    CHECK(EN_ROOM_MAX == 56u, "the queue as it was");
    en_init();
    block();
    CHECK(en_room() == EN_ROOM_MAX, "room %u", en_room());
    for (i = 0u; i < (uint32_t)P_NPARAMS + 6u; i++)
        en_set(P_ARPLATCH, (int)(i & 1u));
    CHECK(!en_dropped(), "a whole piece: dropped");
    CHECK(en_room() == EN_ROOM_MAX - (uint32_t)P_NPARAMS - 6u, "room %u", en_room());
    /* keys take none of it */
    for (i = 0u; i < 1000u; i++)
        en_keys(i & 1u ? KEY(i % 27u) : 0u);
    for (i = 0u; i < 32u; i++)
        en_keys_clear();
    CHECK(en_room() == EN_ROOM_MAX - (uint32_t)P_NPARAMS - 6u, "keys took room: %u", en_room());
    CHECK(!en_dropped(), "keys: a drop");
    en_keys(0u);
    block();
    /* a word's range: its keys, whatever is asked; the bits above them are no keys */
    en_set(P_MELMODE, EN_MEL_KEYS);
    block();
    en_keys(0xFFFFFFFFu);
    block();
    CHECK(en_test_bells_on() == 4, "every key: %d", en_test_bells_on());
    en_keys(0u);
    block();
    CHECK(en_test_bells_on() == 0, "every key, up: %d", en_test_bells_on());
    /* its range */
    en_set(P_MELMODE, 9);
    en_set(P_ARPLATCH, 2);
    en_set(P_ARPPAT, 20);
    block();
    CHECK(en_test_par(P_MELMODE) == EN_MEL_KEYS_ARP && en_test_par(P_ARPLATCH) == 1 && en_test_par(P_ARPPAT) == EN_ARP_PLAYED, "out of range: %d %d %d", en_test_par(P_MELMODE), en_test_par(P_ARPLATCH), en_test_par(P_ARPPAT));
    en_set(P_ARPLATCH, -1);
    block();
    CHECK(en_test_par(P_ARPLATCH) == 0, "out of range: %d", en_test_par(P_ARPLATCH));
}

int main(void)
{
    t_pure();
    t_on_off();
    t_latency();
    t_steal();
    t_no_hang();
    t_rest();
    t_order();
    t_skip();
    t_changes();
    t_latch();
    t_others();
    t_queue();
    t_random();
    printf(fails ? "keys_test: %d FAILED\n" : "keys_test: ok\n", fails);
    return fails != 0;
}
