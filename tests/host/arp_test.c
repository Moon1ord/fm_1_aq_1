/* SPDX-License-Identifier: MPL-2.0 */
/* The melody's other mode, Arp, and how often Phrases come in Hands. What Arp plays is the chord sounding, in a register of two octaves, at the division Rate says, in the pattern chosen, and nothing in
 * it can hang or come in loud; a mode change at any moment, a panic, a duck, a rest, a new piece. What a recipe has in it (Phrases, Rate up to 60) and Auto at any Rate are what they were: pinned, as renders of the commit before. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "amb.h"

static int fails;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); fails++; } } while (0)
#define GAIN 2430u
#define SEC(s) ((uint32_t)((s) * 44100.0f) & ~255u)

/* ---- MIDI: everything hashed with its time; the melody's channel looked at note by note, against the chord sounding */
static uint32_t frames, mhash, midi_n;
static uint32_t ons[16], mel_vel_lo, mel_vel_hi, mel_last_on_frame, mel_gap_most, mel_gap_least;
static int down[16][128], nd[16], most_down;
static uint32_t chord_tones;                     /* the chord sounding, absolute */
static int mel_lo, mel_hi, mel_range_check, mel_chord_check, mel_note_last, mel_oct;
static uint32_t off_chord, off_range, double_on, off_unowed, note_at_step_off;
static uint32_t key_high;                        /* the keys' notes at or over this: 0 not looked at */
static int key_watch;
static uint32_t step_div;
#define REC_STEPS 4096u
static uint8_t rec_note[REC_STEPS];              /* what the melody's channel struck on each step of the song, while rec_on */
static int rec_on;
static uint32_t other_hash;                      /* and the other channels' MIDI, with its step */
static uint32_t rot12_t(uint32_t t, int by)
{
    by = (by % 12 + 12) % 12;
    return (t << by | t >> (12 - by)) & 0xFFFu;
}
void en_midi_out(uint32_t st, uint32_t d1, uint32_t d2)
{
    const uint32_t v[4] = {st, d1, d2, frames};
    const int c = (int)(st & 15u), k = (int)(st & 0xF0u);
    int i;
    for (i = 0; i < 4; i++)
        mhash = (mhash ^ v[i]) * 16777619u;
    midi_n++;
    if (rec_on && st < 0xF0u && c != 1) {
        other_hash = (other_hash ^ st) * 16777619u;
        other_hash = (other_hash ^ (d1 << 8 | d2)) * 16777619u;
        other_hash = (other_hash ^ en_test_song()) * 16777619u;
    }
    if (rec_on && k == 0x90 && d2 && c == 1 && en_test_song() < REC_STEPS)
        rec_note[en_test_song()] = (uint8_t)d1;
    if (st >= 0xF0u)
        return;
    if (k == 0x90 && d2) {
        ons[c]++;
        if (c == 0 && key_watch && (int)d1 >= key_watch)
            key_high++;
        if (down[c][d1])
            double_on++;
        down[c][d1]++;
        nd[c]++;
        if (c == 1) {
            if (nd[1] > most_down)
                most_down = nd[1];
            if (d2 < mel_vel_lo)
                mel_vel_lo = d2;
            if (d2 > mel_vel_hi)
                mel_vel_hi = d2;
            if (mel_chord_check && !(chord_tones >> (d1 % 12u) & 1u))
                off_chord++;
            if (mel_range_check && ((int)d1 - 12 * mel_oct < mel_lo || (int)d1 - 12 * mel_oct > mel_hi))
                off_range++;
            if (step_div && (en_test_song() % step_div))
                note_at_step_off++;
            if (mel_last_on_frame != 0xFFFFFFFFu) {
                const uint32_t g = frames - mel_last_on_frame;
                if (g > mel_gap_most)
                    mel_gap_most = g;
                if (g < mel_gap_least)
                    mel_gap_least = g;
            }
            mel_last_on_frame = frames;
            mel_note_last = (int)d1;
        }
    } else if (k == 0x80 || k == 0x90) {
        if (down[c][d1] > 0) {
            down[c][d1]--;
            nd[c]--;
        } else if (c == 1) {
            off_unowed++;
        }
    } else if (k == 0xB0 && d1 == 123u) {
        memset(down[c], 0, sizeof down[c]);
        nd[c] = 0;
    }
}
static void on_chord(uint32_t step, const en_gchord_t *g, int character)
{
    (void)step;
    (void)character;
    chord_tones = rot12_t(g->ch.tones, g->ch.tonic);
}

/* ---- rendering */
static int32_t buf[2 * 256];
static double e_sum, peak;
static uint32_t e_n;
static int ledger;
static uint32_t led_bad, voices_most;
static double step_most;
static int32_t step_prev;
static uint32_t ahash;
static void counts_clear(void)
{
    midi_n = 0u;
    memset(ons, 0, sizeof ons);
    mel_vel_lo = 127u;
    mel_vel_hi = 0u;
    mel_last_on_frame = 0xFFFFFFFFu;
    mel_gap_most = 0u;
    mel_gap_least = 0xFFFFFFFFu;
    most_down = 0;
    off_chord = off_range = double_on = off_unowed = note_at_step_off = 0u;
}
static void start(uint32_t seed, int style, int mode)
{
    en_init();
    frames = 0u;
    mhash = ahash = 2166136261u;
    memset(down, 0, sizeof down);
    memset(nd, 0, sizeof nd);
    counts_clear();
    voices_most = 0u;
    mel_range_check = mel_chord_check = 0;
    mel_oct = 0;
    step_div = 0u;
    en_trace_chord = on_chord;
    en_arrange(mode);
    en_set(P_STYLE, style);
    en_seed(seed);
}
static void led_check(void)
{
    if (nd[0] != en_test_keys_down() || nd[1] != en_test_bells_on() || nd[2] != en_test_bass_down() || nd[3] != en_test_pad_sent() || nd[9] > 8) {
        if (led_bad++ < 4u)
            printf("FAIL the ledger: MIDI says keys %d melody %d bass %d pad %d drums %d, the engine %d %d %d %d\n", nd[0], nd[1], nd[2], nd[3], nd[9], en_test_keys_down(),
                   en_test_bells_on(), en_test_bass_down(), en_test_pad_sent());
        fails++;
    }
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
        if (ledger)
            led_check();
        frames += k;
        n -= k;
    }
}
static void meter_clear(void)
{
    e_sum = peak = 0.0;
    e_n = 0u;
    step_most = 0.0;
}
static double rms_now(void) { return sqrt(e_sum / (double)(e_n ? e_n : 1u)); }
static void all_on(void)
{
    int l;
    for (l = 0; l < EN_NLAYERS; l++)
        en_layer(l, 1);
}
static void only_melody(void)
{
    en_layer(EN_L_MEL, 1);
    en_set(P_KEYS, 0);
    en_set(P_BASS, 0);
    en_set(P_DRUMS, 0);
    en_set(P_HATS, 0);
}

/* ---- the pure parts */
static void t_pure(void)
{
    static const uint32_t CHORDS[6] = {0x091u, 0x891u, 0x895u, 0x0A5u, 0x249u, 0x081u};   /* (relative: a triad, a seventh, a ninth, a sus, a diminished, a bare fifth) */
    int r, key, c, p;
    /* Rate is the division, whatever else */
    for (r = 0; r <= 100; r++) {
        const int want = r < 34 ? 4 : r < 67 ? 2 : 1;
        CHECK(en_arp_steps(r) == want, "Rate %d: %d steps", r, en_arp_steps(r));
    }
    for (key = 0; key < 12; key++) {
        int lo, hi;
        en_arp_range(key, &lo, &hi);
        CHECK(hi - lo == 24, "key %d: a range of %d", key, hi - lo);
        CHECK(hi <= EN_MEL_TOP, "key %d: top %d above the melody's", key, hi);
        CHECK(lo >= 72, "key %d: bottom %d under the melody's", key, lo);
    }
    for (key = 0; key < 12; key++)
        for (c = 0; c < 6; c++) {
            const uint32_t tones = rot12_t(CHORDS[c], key);
            int lo, hi, n, cnt = 0;
            uint64_t all = 0u;
            en_arp_range(key, &lo, &hi);
            for (n = lo; n <= hi; n++)
                if (tones >> (n % 12) & 1u) {
                    all |= (uint64_t)1 << (n - lo);
                    cnt++;
                }
            CHECK(cnt >= 3, "key %d chord %d: %d notes in range", key, c, cnt);
            for (p = 0; p < EN_ARP_NPATTERNS; p++) {
                en_arp_t a;
                int i, last = 0, wraps = 0, turns = 0, dir = 0;
                uint64_t seen = 0u;
                en_arp_start(&a);
                for (i = 0; i < 40 * cnt; i++) {
                    const int m = en_arp_next(&a, tones, key, p, 2654435761u * (uint32_t)(i + 1));
                    CHECK(m >= lo && m <= hi, "key %d pattern %d: %d out of %d..%d", key, p, m, lo, hi);
                    CHECK(tones >> (m % 12) & 1u, "key %d pattern %d: %d not in the chord", key, p, m);
                    seen |= (uint64_t)1 << (m - lo);
                    if (i) {
                        const int d = m > last ? 1 : m < last ? -1 : 0;
                        CHECK(d != 0, "key %d chord %d pattern %d: the same note twice: %d", key, c, p, m);
                        if (p == EN_ARP_UP && d < 0) {
                            wraps++;
                            CHECK(last == 63 - __builtin_clzll(all) + lo && m == __builtin_ctzll(all) + lo, "up: it turned over at %d to %d", last, m);
                        }
                        if (p == EN_ARP_DOWN && d > 0) {
                            wraps++;
                            CHECK(m == 63 - __builtin_clzll(all) + lo && last == __builtin_ctzll(all) + lo, "down: it turned over at %d to %d", last, m);
                        }
                        if (p == EN_ARP_UPDOWN && dir && d != dir) {
                            turns++;
                            CHECK(last == 63 - __builtin_clzll(all) + lo || last == __builtin_ctzll(all) + lo, "up-down turned at %d", last);
                        }
                        dir = d;
                    } else if (p == EN_ARP_UP || p == EN_ARP_UPDOWN) {
                        CHECK(m == __builtin_ctzll(all) + lo, "up: it begins at %d", m);
                    } else if (p == EN_ARP_DOWN) {
                        CHECK(m == 63 - __builtin_clzll(all) + lo, "down: it begins at %d", m);
                    }
                    last = m;
                }
                CHECK(seen == all, "key %d chord %d pattern %d: not all its notes came", key, c, p);
                if (p == EN_ARP_UP || p == EN_ARP_DOWN)
                    CHECK(wraps >= 30, "pattern %d: %d turns over", p, wraps);
                if (p == EN_ARP_UPDOWN)
                    CHECK(turns >= 30, "up-down: %d turns", turns);
            }
        }
    /* a chord that changes: on from where it was, the next one of its notes the way it was going */
    {
        en_arp_t a;
        int m;
        en_arp_start(&a);
        en_arp_next(&a, 0x091u, 0, EN_ARP_UP, 1u);
        m = en_arp_next(&a, 0x091u, 0, EN_ARP_UP, 1u);   /* C E */
        CHECK(m == 76, "C then E: %d", m);
        m = en_arp_next(&a, 0x221u, 0, EN_ARP_UP, 1u);   /* F A C: from E */
        CHECK(m == 77, "E into F A C: %d", m);
    }
    /* no notes at all can not be: but it does not go astray */
    {
        en_arp_t a;
        int m, lo, hi;
        en_arp_start(&a);
        m = en_arp_next(&a, 0u, 3, EN_ARP_RANDOM, 77u);
        en_arp_range(3, &lo, &hi);
        CHECK(m >= lo && m <= hi, "no tones: %d", m);
    }
    /* Random, one hash one note */
    {
        en_arp_t a, b;
        int i;
        en_arp_start(&a);
        en_arp_start(&b);
        for (i = 0; i < 100; i++) {
            const uint32_t h = 16777619u * (uint32_t)i + 5u;
            CHECK(en_arp_next(&a, 0x891u, 0, EN_ARP_RANDOM, h) == en_arp_next(&b, 0x891u, 0, EN_ARP_RANDOM, h), "one hash, two notes");
        }
    }
    /* the timbre: what Tone allows, and of that what speaks at a sixteenth */
    for (r = 0; r <= 100; r++) {
        const int w_lo = r > 50 ? (r - 50) * 3 / 50 : 0, w_hi = r < 50 ? 2 + r * 3 / 50 : EN_NTIMBRES - 1;
        uint32_t seen = 0u, i, want = 0u;
        int t;
        for (t = w_lo; t <= w_hi; t++)
            if (t >= EN_TB_VIBES)
                want |= 1u << t;
        for (i = 0u; i < 400u; i++) {
            t = en_arp_timbre(r, i * 2654435761u);
            CHECK(t >= w_lo && t <= w_hi, "Tone %d: timbre %d out of %d..%d", r, t, w_lo, w_hi);
            CHECK(t >= EN_TB_VIBES, "Tone %d: timbre %d: its slow attack", r, t);
            seen |= 1u << t;
        }
        CHECK(seen == want, "Tone %d: only %02x of %02x came", r, seen, want);
    }
}

/* ---- what it plays: every mode and key, every pattern, a sixteenth at the top tempo: every note of the chord sounding, in range, in time, one key at a time */
static void t_every_chord(void)
{
    int m, key, p, chords = 0;
    for (m = 0; m < EN_NMODES; m++)
        for (key = 0; key < 12; key++)
            for (p = 0; p < EN_ARP_NPATTERNS; p++) {
                const uint32_t N = 1600u;
                uint32_t on0;
                start(100u + (uint32_t)(m * 12 + key), p % EN_NSTYLES, EN_HANDS);
                en_set(P_TEMPO, 180);
                en_set(P_MODE, m);
                en_set(P_ROOT, key);
                en_pool(EN_MODE_POOL[m]);
                en_set(P_MELMODE, EN_MEL_ARP);
                en_set(P_ARPPAT, p);
                en_set(P_MRATE, 100);
                en_set(P_EVOLVE, 100);
                en_play(1);
                en_render(buf, 16u, GAIN);
                en_arp_range(key, &mel_lo, &mel_hi);
                mel_chord_check = mel_range_check = 1;
                on0 = ons[1];
                en_test_ticks(N);
                CHECK(ons[1] - on0 >= N - 1u, "mode %d key %d pattern %d: %u notes in %u sixteenths: a pause", m, key, p, ons[1] - on0, N);
                CHECK(!off_chord, "mode %d key %d pattern %d: %u notes not in the chord", m, key, p, off_chord);
                CHECK(!off_range, "mode %d key %d pattern %d: %u notes out of range", m, key, p, off_range);
                CHECK(most_down <= 1, "mode %d key %d pattern %d: %d keys down at once", m, key, p, most_down);
                CHECK(!double_on && !off_unowed, "mode %d key %d pattern %d: pairs: %u twice on, %u off with none on", m, key, p, double_on, off_unowed);
                CHECK(nd[1] == en_test_bells_on(), "mode %d key %d pattern %d: the ledger at the end", m, key, p);
                chords++;
            }
    printf("  every chord: %d runs of 1600 sixteenths at 180\n", chords);
}

/* Rate is the division, on the grid; Octave on top, folded under 127 */
static void t_division(void)
{
    static const int RATE[3] = {10, 50, 90}, STEPS[3] = {4, 2, 1};
    int d, o;
    for (d = 0; d < 3; d++) {
        const uint32_t N = 1600u;
        start(5u, EN_ST_LIQUID, EN_HANDS);
        en_set(P_MELMODE, EN_MEL_ARP);
        en_set(P_MRATE, RATE[d]);
        en_play(1);
        en_render(buf, 16u, GAIN);
        step_div = (uint32_t)STEPS[d];
        counts_clear();
        en_test_ticks(N);
        /* (the note goes out in the step; en_test_song has not moved on yet) */
        CHECK(!note_at_step_off, "division %d: %u notes off the grid", STEPS[d], note_at_step_off);
        CHECK(ons[1] >= N / (uint32_t)STEPS[d] - 1u && ons[1] <= N / (uint32_t)STEPS[d] + 1u, "division %d: %u notes in %u", STEPS[d], ons[1], N);
    }
    for (o = -2; o <= 2; o++) {
        int key;
        for (key = 0; key < 12; key += 11) {
            start(7u, EN_ST_LIQUID, EN_HANDS);
            en_set(P_ROOT, key);
            en_set(P_MOCT, o);
            en_set(P_MELMODE, EN_MEL_ARP);
            en_set(P_MRATE, 100);
            en_play(1);
            en_render(buf, 16u, GAIN);
            en_arp_range(key, &mel_lo, &mel_hi);
            mel_chord_check = mel_range_check = 1;
            mel_oct = o;
            counts_clear();
            en_test_ticks(800u);
            CHECK(ons[1] >= 799u, "Octave %d: %u notes", o, ons[1]);
            CHECK(!off_chord && !off_range, "Octave %d key %d: %u off the chord, %u out of its range", o, key, off_chord, off_range);
        }
    }
    /* level: the knob, and at 0 silence with the MIDI going on */
    {
        double r60, r100, r0;
        start(9u, EN_ST_LIQUID, EN_HANDS);
        only_melody();
        en_set(P_MELMODE, EN_MEL_ARP);
        en_set(P_MRATE, 100);
        en_set(P_MELSND, EN_TB_MARIMBA + 1);
        en_set(P_MOTIFS, 60);
        en_play(1);
        run(SEC(3.0f), 256u);
        meter_clear();
        run(SEC(4.0f), 256u);
        r60 = rms_now();
        en_set(P_MOTIFS, 100);
        run(SEC(3.0f), 256u);
        meter_clear();
        run(SEC(4.0f), 256u);
        r100 = rms_now();
        en_set(P_MOTIFS, 0);
        run(SEC(8.0f), 256u);
        counts_clear();
        meter_clear();
        run(SEC(2.0f), 256u);
        r0 = rms_now();
        CHECK(r100 > r60 * 1.5, "Level: %f at 100, %f at 60", r100, r60);
        CHECK(r0 < 1e-4, "Level 0: %f", r0);
        CHECK(ons[1] > 10u, "Level 0: its MIDI should go on");
    }
}

/* the real thing: no pauses, in time, through a hold, a section, a break; one chord at a time; the ledger clean */
static void t_no_pauses(void)
{
    int d;
    static const int RATE[3] = {10, 50, 90}, TEMPO[3] = {150, 170, 180};
    for (d = 0; d < 3; d++) {
        const int steps = en_arp_steps(RATE[d]);
        const float step_len = 44100.0f * 60.0f / ((float)TEMPO[d] * 4.0f);
        int sec;
        start(11u, EN_ST_ROLLER, EN_HANDS);
        all_on();
        en_set(P_TEMPO, TEMPO[d]);
        en_set(P_MELMODE, EN_MEL_ARP);
        en_set(P_MRATE, RATE[d]);
        en_play(1);
        ledger = 1;
        run(SEC(0.5f), 256u);
        mel_chord_check = 1;
        counts_clear();
        en_hold(1);
        run(SEC(5.0f), 256u);
        en_hold(0);
        for (sec = 0; sec < EN_NSECTIONS; sec++) {
            en_next();
            run(SEC(5.0f), 256u);
        }
        en_break();
        run(SEC(8.0f), 256u);
        CHECK(!off_chord, "division %d: %u notes off the chord", steps, off_chord);
        CHECK((float)mel_gap_most < (float)steps * step_len * 1.2f + 300.0f, "division %d: a gap of %u frames, a division is %f", steps, mel_gap_most, (double)((float)steps * step_len));
        CHECK(!double_on && !off_unowed, "division %d: pairs: %u twice on, %u off with none on", steps, double_on, off_unowed);
        CHECK(most_down <= 1, "division %d: %d keys down", steps, most_down);
    }
    ledger = 0;
    CHECK(!led_bad, "the ledger: %u bad blocks", led_bad);
}

/* the keys leave it its register, as they leave a phrase theirs: while Arp plays, no lone note up there; and no key of it down that is not in the chord, at any block */
static void t_room(void)
{
    uint32_t sd;
    for (sd = 0u; sd < 3u; sd++) {
        uint32_t i;
        int low = 0;
        start(601u + sd, EN_ST_LIQUID, EN_HANDS);
        en_set(P_MELMODE, EN_MEL_ARP);
        en_set(P_MRATE, 90);
        en_set(P_NOTES, 3);
        en_play(1);
        key_watch = EN_MEL_LO(2);
        key_high = 0u;
        for (i = 0u; i < SEC(120.0f); i += 256u) {
            run(256u, 256u);
            low += en_test_bell_off_chord();
        }
        key_watch = 0;
        CHECK(!key_high, "seed %u: %u of the keys' notes in Arp's register", sd, key_high);
        CHECK(!low, "seed %u: a key of Arp down off the chord, %d blocks", sd, low);
    }
}

/* back in Phrases, a phrase at its next chance, even where the rests are long */
static void t_back_to_phrases(void)
{
    int sec;
    uint32_t sd;
    for (sec = 0; sec < 3; sec++)
        for (sd = 0u; sd < 8u; sd++) {
            int k;
            start(701u + sd, EN_ST_LIQUID, EN_HANDS);
            en_set(P_MELMODE, EN_MEL_ARP);
            en_play(1);
            run(SEC(0.5f), 256u);
            for (k = 0; k < sec + 1; k++) {
                en_next();
                run(SEC(1.5f), 256u);
            }
            run(SEC(5.0f), 256u);
            en_set(P_MELMODE, EN_MEL_PHRASES);
            run(256u, 256u);
            run(SEC(0.6f), 256u);                /* (Arp's last note's key) */
            counts_clear();
            run(SEC(10.0f), 256u);               /* four bars and a bar's lead, and the note's place in its bar */
            CHECK(ons[1] > 0u, "section %d seed %u: back in Phrases, nothing in 10 s", sec, sd);
        }
}

/* the melody off: unheard, and its MIDI is the same, note for note; back on at once. Keys let go of the chord that is gone */
static void t_mute(void)
{
    uint32_t a, n;
    start(21u, EN_ST_LIQUID, EN_HANDS);
    all_on();
    en_set(P_MELMODE, EN_MEL_ARP);
    en_set(P_MRATE, 90);
    en_play(1);
    run(SEC(15.0f), 256u);
    a = mhash;
    n = midi_n;
    start(21u, EN_ST_LIQUID, EN_HANDS);
    all_on();
    en_set(P_MELMODE, EN_MEL_ARP);
    en_set(P_MRATE, 90);
    en_play(1);
    run(SEC(4.0f), 256u);
    en_layer(EN_L_MEL, 0);
    run(SEC(6.0f), 256u);
    en_layer(EN_L_MEL, 1);
    run(SEC(5.0f), 256u);
    CHECK(mhash == a && midi_n == n, "the melody off: its MIDI moved");
    /* silent in the mix: only the melody, off */
    start(21u, EN_ST_LIQUID, EN_HANDS);
    only_melody();
    en_set(P_MELMODE, EN_MEL_ARP);
    en_set(P_MRATE, 90);
    en_set(P_MELSND, EN_TB_MARIMBA + 1);
    en_play(1);
    run(SEC(3.0f), 256u);
    en_layer(EN_L_MEL, 0);
    run(SEC(9.0f), 256u);
    meter_clear();
    counts_clear();
    run(SEC(2.0f), 256u);
    CHECK(rms_now() < 1e-4, "the melody off, %f", rms_now());
    CHECK(ons[1] > 10u, "the melody off: MIDI stopped");
}

/* ---- Tone, Sound: Auto one timbre at a time, for a long while; one chosen always; and MIDI the same whichever */
static uint32_t timbre_changes[1], timbre_shortest;
static void t_timbre(void)
{
    uint32_t s, last_change = 0u;
    int was = -1;
    uint32_t seen = 0u, a, n;
    start(31u, EN_ST_LIQUID, EN_HANDS);
    en_set(P_MELMODE, EN_MEL_ARP);
    en_set(P_MRATE, 100);
    en_play(1);
    en_render(buf, 16u, GAIN);
    timbre_shortest = 0xFFFFFFFFu;
    for (s = 0u; s < 16u * 2000u; s++) {
        en_test_ticks(1u);
        if (en_test_bell_struck != was) {
            if (was >= 0) {
                const uint32_t bars = (en_test_song() - 1u - last_change) / 16u;
                if (bars < timbre_shortest && timbre_changes[0])   /* (the first starts wherever the loop did) */
                    timbre_shortest = bars;
                timbre_changes[0]++;
                CHECK(!((en_test_song() - 1u) % 16u), "it changed timbre within a bar: step %u", en_test_song() - 1u);
            }
            last_change = en_test_song() - 1u;
            was = en_test_bell_struck;
            seen |= 1u << was;
        }
    }
    printf("  Auto's timbre in Arp: %u changes in 2000 bars, at the shortest %u bars, %02x of them\n", timbre_changes[0], timbre_shortest, seen);
    CHECK(timbre_shortest >= 32u, "a timbre for %u bars", timbre_shortest);
    CHECK(timbre_changes[0] >= 20u, "%u changes of timbre in 2000 bars", timbre_changes[0]);
    /* the sound chosen: that, and the notes the same */
    start(33u, EN_ST_LIQUID, EN_HANDS);
    en_set(P_MELMODE, EN_MEL_ARP);
    en_set(P_MRATE, 50);
    en_play(1);
    run(SEC(10.0f), 256u);
    a = mhash;
    n = midi_n;
    {
        int t;
        for (t = 0; t < EN_NTIMBRES; t++) {
            start(33u, EN_ST_LIQUID, EN_HANDS);
            en_set(P_MELMODE, EN_MEL_ARP);
            en_set(P_MRATE, 50);
            en_set(P_MELSND, t + 1);
            en_play(1);
            run(SEC(10.0f), 256u);
            CHECK(en_test_bell_struck == t, "chose %d, struck %d", t, en_test_bell_struck);
            CHECK(mhash == a && midi_n == n, "timbre %d: MIDI moved", t);
        }
    }
}

/* ---- the voices: at most what the melody ever had, and at a sixteenth, whatever the sound, none taken while it can still be heard. The numbers for the report */
static void t_voices(void)
{
    static const char *const NM[EN_NTIMBRES + 1] = {"Auto", "Flute", "Vocal", "Vibes", "Marimba", "Box", "Glass"};
    int t, tempo;
    for (tempo = 150; tempo <= 180; tempo += 30)
        for (t = 0; t <= EN_NTIMBRES; t++) {
            start(41u, EN_ST_LIQUID, EN_HANDS);
            en_test_bell_stolen_clear();
            en_set(P_TEMPO, tempo);
            en_set(P_MELMODE, EN_MEL_ARP);
            en_set(P_MRATE, 100);
            en_set(P_MELSND, t);
            en_set(P_MTONE, 100);
            en_set(P_MOTIFS, 100);
            en_play(1);
            ledger = 1;
            run(SEC(40.0f), 256u);
            ledger = 0;
            CHECK(voices_most <= (uint32_t)en_test_bell_limit(), "%d voices", voices_most);
            CHECK(voices_most <= 3u, "%s at %d: %u voices at a sixteenth: its tails no longer fit", NM[t], tempo, voices_most);
            CHECK(en_test_bell_stolen() < 0.02f, "%s at %d: a voice taken at %f", NM[t], tempo, (double)en_test_bell_stolen());
            printf("  a sixteenth at %d, %-7s: %u voices at most, the loudest taken %.5f\n", tempo, NM[t], voices_most, (double)en_test_bell_stolen());
        }
    CHECK(!led_bad, "the ledger: %u bad blocks", led_bad);
}

/* ---- how loud: no higher than the phrases' own peak in that timbre, and gently; a soft velocity that varies a little */
static void t_level(void)
{
    static const char *const NM[EN_NTIMBRES] = {"Flute", "Vocal", "Vibes", "Marimba", "Box", "Glass"};
    int t, d;
    static const int RATE[3] = {10, 50, 90};
    for (t = 0; t < EN_NTIMBRES; t++) {
        double ph_peak, ph_rms;
        uint32_t s;
        start(51u, EN_ST_LIQUID, EN_HANDS);
        only_melody();
        en_set(P_REVERB, 0);
        en_set(P_ECHO, 0);
        en_set(P_MELSND, t + 1);
        en_set(P_MRATE, 100);
        en_next();
        en_next();
        en_play(1);
        run(SEC(1.0f), 256u);
        for (s = 0u; s < 2u; s++) {              /* the Full's melody: its firmest */
            en_next();
            run(SEC(3.0f), 256u);
        }
        meter_clear();
        run(SEC(40.0f), 256u);
        ph_peak = peak;
        ph_rms = rms_now();
        CHECK(ph_peak > 0.01, "%s: no phrases to compare with", NM[t]);
        for (d = 0; d < 3; d++) {
            start(51u, EN_ST_LIQUID, EN_HANDS);
            only_melody();
            en_set(P_REVERB, 0);
            en_set(P_ECHO, 0);
            en_set(P_MELSND, t + 1);
            en_set(P_MRATE, RATE[d]);
            en_set(P_MELMODE, EN_MEL_ARP);
            en_play(1);
            run(SEC(1.0f), 256u);
            counts_clear();
            meter_clear();
            run(SEC(20.0f), 256u);
            printf("  %-7s Arp at %d steps: peak %.3f (phrases %.3f), rms %.3f (phrases, with their rests, %.3f), velocity %u..%u\n", NM[t], en_arp_steps(RATE[d]), peak, ph_peak, rms_now(), ph_rms, mel_vel_lo, mel_vel_hi);
            CHECK(peak <= ph_peak * 1.05, "%s, %d steps: a peak of %f, its phrases' %f", NM[t], en_arp_steps(RATE[d]), peak, ph_peak);
            CHECK(mel_vel_hi <= 66u, "%s, %d steps: a velocity of %u", NM[t], en_arp_steps(RATE[d]), mel_vel_hi);
            CHECK(mel_vel_lo >= 30u, "%s, %d steps: a velocity of %u: hardly there", NM[t], en_arp_steps(RATE[d]), mel_vel_lo);
            CHECK(mel_vel_hi > mel_vel_lo, "%s: every note the same", NM[t]);
            CHECK(rms_now() <= ph_rms * 2.5, "%s, %d steps: an rms of %f, its phrases' %f", NM[t], en_arp_steps(RATE[d]), rms_now(), ph_rms);
        }
    }
    /* in the whole mix: it does not lift it */
    {
        double a, b;
        start(53u, EN_ST_LIQUID, EN_HANDS);
        all_on();
        en_set(P_MRATE, 100);
        en_play(1);
        run(SEC(4.0f), 256u);
        meter_clear();
        run(SEC(60.0f), 256u);
        a = rms_now();
        start(53u, EN_ST_LIQUID, EN_HANDS);
        all_on();
        en_set(P_MRATE, 100);
        en_set(P_MELMODE, EN_MEL_ARP);
        en_play(1);
        run(SEC(4.0f), 256u);
        meter_clear();
        run(SEC(60.0f), 256u);
        b = rms_now();
        printf("  the whole mix, 60 s: Phrases at Rate 100 %.1f dB, Arp 1/16 %.1f dB\n", 20.0 * log10(a), 20.0 * log10(b));
        CHECK(fabs(20.0 * log10(b / a)) < 2.0, "the mix moved %f dB", 20.0 * log10(b / a));
    }
}

/* ---- a mode change, at any moment: no click, no note hanging, on with the next note */
static void t_mode_change(void)
{
    uint32_t sd;
    /* in the middle of a phrase's note; of a rest; of an arp's note; back */
    for (sd = 0u; sd < 24u; sd++) {
        uint32_t k;
        double steady;
        start(61u + sd, EN_ST_LIQUID, EN_HANDS);
        only_melody();
        en_set(P_REVERB, 0);
        en_set(P_ECHO, 0);
        en_set(P_MRATE, 100);
        en_set(P_MELSND, sd % 6u + 1);
        ledger = 1;
        en_play(1);
        meter_clear();
        en_set(P_MELMODE, EN_MEL_ARP);           /* (its own: Arp's, up to the top of its register, then the phrases and their steals) */
        run(SEC(14.0f), 256u);
        en_set(P_MELMODE, EN_MEL_PHRASES);
        run(SEC(14.0f), 256u);
        steady = step_most;
        /* a note down, or one not */
        for (k = 0u; k < SEC(20.0f); k += 256u) {
            if (en_test_bells_on() == (int)(sd & 1u))
                break;
            run(256u, 256u);
        }
        CHECK(en_test_bells_on() == (int)(sd & 1u), "seed %u: a phrase's note %s never came", sd, sd & 1u ? "down" : "up");
        meter_clear();
        en_set(P_MELMODE, EN_MEL_ARP);
        en_set(P_MRATE, 100);
        run(SEC(1.0f), 256u);
        CHECK(step_most <= steady * 1.1 + 2000.0, "seed %u: to Arp: a step of %f, its own %f", sd, step_most, steady);
        /* and back, in the middle of a note */
        counts_clear();
        run(SEC(3.0f), 256u);
        CHECK(ons[1] > 20u, "seed %u: Arp gave %u notes in 3 s", sd, ons[1]);
        meter_clear();
        en_set(P_MELMODE, EN_MEL_PHRASES);
        run(SEC(1.0f), 16u);
        CHECK(step_most <= steady * 1.1 + 2000.0, "seed %u: to Phrases: a step of %f, its own %f", sd, step_most, steady);
        CHECK(en_test_bell_keys() <= 1, "keys down: %d", en_test_bell_keys());
        /* in Phrases: it goes on as itself, within some bars: nothing is lost */
        counts_clear();
        run(SEC(14.0f), 256u);
        CHECK(ons[1] > 0u, "seed %u: no phrase after Arp", sd);
    }
    /* quick: on and off, faster than anything: a clean ledger and pairs */
    start(71u, EN_ST_ROLLER, EN_HANDS);
    all_on();
    en_play(1);
    ledger = 1;
    {
        uint32_t i;
        for (i = 0u; i < 3000u; i++) {
            en_set(P_MELMODE, (int)(i >> (i % 5u) & 1u));
            run(256u + 64u * (i % 7u), 256u);
        }
    }
    CHECK(!double_on && !off_unowed, "pairs: %u twice on, %u off with none on", double_on, off_unowed);
    /* and it ends clean */
    en_set(P_MIDI, 0);
    ledger = 0;
    run(1024u, 256u);
    CHECK(nd[1] == 0, "MIDI off: %d of the melody's notes hanging", nd[1]);
    /* through a panic, in Arp, mid-note: nothing, for as long as it is silent; a restore and it plays on, and the room is clear */
    {
        uint32_t i;
        start(73u, EN_ST_LIQUID, EN_HANDS);
        en_set(P_MELMODE, EN_MEL_ARP);
        en_set(P_MRATE, 100);
        en_play(1);
        ledger = 1;
        run(SEC(3.0f), 256u);
        en_play(0);
        en_panic();
        ledger = 0;
        run(256u, 256u);
        CHECK(nd[1] == 0 && en_test_bell_voices() == 0, "a panic: %d down, %d voices", nd[1], en_test_bell_voices());
        counts_clear();
        run(SEC(2.0f), 256u);
        CHECK(ons[1] == 0u, "silent, and %u notes", ons[1]);
        for (i = 0u; i < 3u; i++) {
            en_set(P_MELMODE, (int)(i & 1u));    /* (a mode change through the silence, too) */
            run(SEC(0.2f), 256u);
        }
        en_set(P_MELMODE, EN_MEL_ARP);
        en_restore();
        run(SEC(4.0f), 256u);
        ledger = 1;
        CHECK(ons[1] > 10u, "back: %u notes", ons[1]);
        run(SEC(2.0f), 256u);
        ledger = 0;
    }
    /* resting, in Arp: it stops, and the key lets go; on again, from the next note */
    {
        start(75u, EN_ST_LIQUID, EN_HANDS);
        en_set(P_MELMODE, EN_MEL_ARP);
        en_set(P_MRATE, 50);
        en_play(1);
        ledger = 1;
        run(SEC(3.0f), 256u);
        en_play(0);
        run(SEC(0.5f), 256u);
        CHECK(nd[1] == 0, "resting: %d of the melody's notes down", nd[1]);
        counts_clear();
        run(SEC(3.0f), 256u);
        CHECK(ons[1] == 0u, "resting: %u notes", ons[1]);
        en_play(1);
        run(SEC(2.0f), 256u);
        CHECK(ons[1] > 8u, "growing again: %u notes", ons[1]);
        ledger = 0;
    }
    CHECK(!led_bad, "the ledger: %u bad blocks", led_bad);
}

/* ---- a save's duck: under it Arp goes on, note for note: only the line on the output knows. A new piece: on, from the next note, in the new seed; the keys let go */
static void t_duck_and_piece(void)
{
    uint32_t a, n, b4;
    uint32_t i;
    start(81u, EN_ST_LIQUID, EN_HANDS);
    all_on();
    en_set(P_MELMODE, EN_MEL_ARP);
    en_set(P_MRATE, 90);
    en_play(1);
    run(SEC(15.0f), 256u);
    a = mhash;
    n = midi_n;
    b4 = ahash;
    start(81u, EN_ST_LIQUID, EN_HANDS);
    all_on();
    en_set(P_MELMODE, EN_MEL_ARP);
    en_set(P_MRATE, 90);
    en_play(1);
    ledger = 1;
    run(SEC(5.0f), 256u);
    en_duck(1);
    run(SEC(0.4f), 256u);
    CHECK(en_ducked, "not at zero");
    meter_clear();
    run(SEC(0.1f), 256u);
    CHECK(peak == 0.0, "ducked and not silent: %f", peak);
    en_duck(0);
    run(SEC(15.0f) - SEC(5.0f) - SEC(0.4f) - SEC(0.1f), 256u);
    ledger = 0;
    CHECK(mhash == a && midi_n == n, "a duck: its MIDI moved");
    CHECK(ahash != b4, "(a duck: nothing heard of it)");
    /* a piece: the values all at once, in the middle of a note, as project_apply and owed_tick do; Phrases <-> Arp */
    for (i = 0u; i < 8u; i++) {
        int p;
        int16_t par[P_NPARAMS];
        uint16_t pool;
        start(90u + i, EN_ST_LIQUID, EN_HANDS);
        all_on();
        en_set(P_MELMODE, EN_MEL_ARP);
        en_set(P_MRATE, 90);
        en_play(1);
        ledger = 1;
        run(SEC(2.0f) + i * 512u, 256u);
        en_piece((int)i, par, &pool);
        CHECK(par[P_MELMODE] == EN_MEL_PHRASES, "piece %u: a recipe in Arp", i);
        if (i & 1u)
            par[P_MELMODE] = EN_MEL_ARP;
        par[P_ARPPAT] = (int16_t)(i % EN_ARP_NPATTERNS);
        for (p = 0; p < P_NPARAMS; p++)
            en_set(p, par[p]);
        en_pool(pool);
        en_seed(1000u + i);
        en_regrow();
        counts_clear();
        run(SEC(6.0f), 256u);
        if (i & 1u) {
            CHECK(ons[1] > 10u, "piece %u, Arp: %u notes", i, ons[1]);
            CHECK(!double_on && !off_unowed, "piece %u: pairs", i);
            mel_chord_check = 1;
            counts_clear();
            run(SEC(20.0f), 256u);           /* (from the next chord on, the new key's) */
            CHECK(ons[1] > 10u, "piece %u, Arp: %u notes", i, ons[1]);
            CHECK(!off_chord, "piece %u: %u notes off the chord", i, off_chord);
        }
        ledger = 0;
    }
    CHECK(!led_bad, "the ledger: %u bad blocks", led_bad);
}

/* ---- the queue: a piece is so many commands; what does not fit is said to be so, and the app has the rest (ui.c, tests/scenarios/queue.amb, arp.amb) */
static void t_queue(void)
{
    uint32_t i;
    printf("  a piece: %d commands, the queue %u\n", P_NPARAMS + 6, EN_ROOM_MAX);
    CHECK(P_NPARAMS + 6 <= (int)EN_ROOM_MAX, "a piece no longer fits");
    CHECK(P_NPARAMS + 6 == 52, "a piece's commands: %d", P_NPARAMS + 6);
    CHECK(EN_ROOM_MAX == 56u, "the queue as it was");
    en_init();
    run(256u, 256u);
    CHECK(en_room() == EN_ROOM_MAX, "room %u", en_room());
    for (i = 0u; i < 52u; i++)
        en_set(P_MELMODE, (int)(i & 1u));
    CHECK(!en_dropped(), "fifty-two dropped");
    CHECK(en_room() == EN_ROOM_MAX - 52u, "room %u", en_room());
    for (i = 0u; i < EN_ROOM_MAX; i++)
        en_set(P_ARPPAT, (int)(i & 7u) % EN_ARP_NPATTERNS);
    CHECK(en_dropped() && !en_dropped(), "a flood: it says so, once");
    run(256u, 256u);
    CHECK(en_room() == EN_ROOM_MAX, "room %u after a block", en_room());
    /* its range */
    en_set(P_MELMODE, 7);
    en_set(P_ARPPAT, 9);
    en_set(P_ARPSKIP, 101);
    run(256u, 256u);
    CHECK(en_test_par(P_MELMODE) == EN_MEL_KEYS_ARP && en_test_par(P_ARPPAT) == EN_ARP_PLAYED && en_test_par(P_ARPSKIP) == 100, "out of range: %d %d %d", en_test_par(P_MELMODE), en_test_par(P_ARPPAT), en_test_par(P_ARPSKIP));
    en_set(P_MELMODE, -1);
    en_set(P_ARPPAT, -1);
    en_set(P_ARPSKIP, -1);
    run(256u, 256u);
    CHECK(en_test_par(P_MELMODE) == EN_MEL_PHRASES && en_test_par(P_ARPPAT) == EN_ARP_UP && en_test_par(P_ARPSKIP) == 0, "out of range: %d %d %d", en_test_par(P_MELMODE), en_test_par(P_ARPPAT), en_test_par(P_ARPSKIP));
    CHECK(en_param_info(P_ARPSKIP)->lo == 0 && en_param_info(P_ARPSKIP)->hi == 100 && en_param_info(P_ARPSKIP)->def == 0, "Skip's range");
    CHECK(en_param_info(P_ARPPAT)->hi == 7, "the patterns: %d", en_param_info(P_ARPPAT)->hi + 1);
    CHECK(EN_ARP_UP == 0 && EN_ARP_DOWN == 1 && EN_ARP_UPDOWN == 2 && EN_ARP_RANDOM == 3 && EN_ARP_CONVERGE == 4 && EN_ARP_PEDTOP == 5 && EN_ARP_PEDROOT == 6 && EN_ARP_PLAYED == 7, "a stored pattern keeps its meaning");
    /* the words */
    {
        static const char *const W[EN_ARP_NPATTERNS] = {"Up", "Down", "Up-Down", "Random", "Conv", "PedT", "PedR", "Played"};
        char t[16];
        int p;
        en_param_text(P_MELMODE, EN_MEL_PHRASES, t);
        CHECK(!strcmp(t, "Phrases"), "%s", t);
        en_param_text(P_MELMODE, EN_MEL_ARP, t);
        CHECK(!strcmp(t, "Arp"), "%s", t);
        for (p = 0; p < EN_ARP_NPATTERNS; p++) {
            en_param_text(P_ARPPAT, p, t);
            CHECK(!strcmp(t, W[p]), "%s", t);
        }
    }
    /* the recipes: Phrases, and Up: nothing else */
    {
        int i2;
        for (i2 = 0; i2 < EN_NPIECES; i2++) {
            int16_t par[P_NPARAMS];
            uint16_t pool;
            en_piece(i2, par, &pool);
            CHECK(par[P_MELMODE] == EN_MEL_PHRASES && par[P_ARPPAT] == EN_ARP_UP && par[P_ARPSKIP] == 0, "piece %d: %d %d %d", i2, par[P_MELMODE], par[P_ARPPAT], par[P_ARPSKIP]);
        }
    }
}

/* ---- the patterns after the first four: each note where it should be, on a known chord, and on every chord and key */
static int chord_notes(uint32_t tones, int key, int *o)
{
    int lo, hi, n, c = 0;
    en_arp_range(key, &lo, &hi);
    for (n = lo; n <= hi; n++)
        if (tones >> (n % 12) & 1u)
            o[c++] = n;
    return c;
}
static void t_modes(void)
{
    static const int C_CONV[7] = {72, 96, 76, 91, 79, 88, 84};
    static const int C_PEDT[12] = {96, 72, 96, 76, 96, 79, 96, 84, 96, 88, 96, 91};
    static const int C_PEDR[12] = {72, 76, 72, 79, 72, 84, 72, 88, 72, 91, 72, 96};
    static const uint32_t CHORDS[6] = {0x091u, 0x891u, 0x895u, 0x0A5u, 0x249u, 0x081u};
    int i, key, c;
    en_arp_t a;
    /* C major, key C: C4 up to C6 as this register has it: 72 76 79 84 88 91 96 */
    en_arp_start(&a);
    for (i = 0; i < 7 * 3; i++) {
        const int m = en_arp_next(&a, 0x091u, 0, EN_ARP_CONVERGE, 1u);
        CHECK(m == C_CONV[i % 7], "converge, step %d: %d, not %d", i, m, C_CONV[i % 7]);
    }
    en_arp_start(&a);
    for (i = 0; i < 12 * 3; i++) {
        const int m = en_arp_next(&a, 0x091u, 0, EN_ARP_PEDTOP, 1u);
        CHECK(m == C_PEDT[i % 12], "pedal top, step %d: %d, not %d", i, m, C_PEDT[i % 12]);
    }
    en_arp_start(&a);
    for (i = 0; i < 12 * 3; i++) {
        const int m = en_arp_next(&a, 0x091u, 0, EN_ARP_PEDROOT, 1u);
        CHECK(m == C_PEDR[i % 12], "pedal root, step %d: %d, not %d", i, m, C_PEDR[i % 12]);
    }
    /* the same on every chord and key, worked out from its notes */
    for (key = 0; key < 12; key++)
        for (c = 0; c < 6; c++) {
            const uint32_t tones = rot12_t(CHORDS[c], key);
            int o[32], n;
            n = chord_notes(tones, key, o);
            en_arp_start(&a);
            for (i = 0; i < n * 3; i++) {
                const int k = i % n, want = k & 1 ? o[n - 1 - k / 2] : o[k / 2], m = en_arp_next(&a, tones, key, EN_ARP_CONVERGE, 1u);
                CHECK(m == want, "converge, key %d chord %d step %d: %d, not %d", key, c, i, m, want);
            }
            en_arp_start(&a);
            for (i = 0; i < (n - 1) * 6; i++) {
                const int k = i % (2 * (n - 1)), want = k & 1 ? o[k / 2] : o[n - 1], m = en_arp_next(&a, tones, key, EN_ARP_PEDTOP, 1u);
                CHECK(m == want, "pedal top, key %d chord %d step %d: %d, not %d", key, c, i, m, want);
            }
            en_arp_start(&a);
            for (i = 0; i < (n - 1) * 6; i++) {
                const int k = i % (2 * (n - 1)), want = k & 1 ? o[k / 2 + 1] : o[0], m = en_arp_next(&a, tones, key, EN_ARP_PEDROOT, 1u);
                CHECK(m == want, "pedal root, key %d chord %d step %d: %d, not %d", key, c, i, m, want);
            }
        }
    /* a chord that changes under it: never the same note twice, always one of the chord's, the pedal still every other note */
    {
        int p;
        for (p = EN_ARP_CONVERGE; p <= EN_ARP_PEDROOT; p++) {
            int last = 0, was_pedal = -1;
            uint32_t h = 2166136261u;
            en_arp_start(&a);
            for (i = 0; i < 4000; i++) {
                int run_of, j;
                uint32_t tones;
                int o[32], n, lo, hi;
                h = h * 16777619u + 7u;
                key = (int)(h >> 8) % 12;
                tones = rot12_t(CHORDS[(h >> 16) % 6u], (int)(h >> 20) % 12);
                run_of = 1 + (int)(h >> 26) % 9;
                n = chord_notes(tones, key, o);
                en_arp_range(key, &lo, &hi);
                was_pedal = -1;
                for (j = 0; j < run_of; j++) {
                    const int m = en_arp_next(&a, tones, key, p, h);
                    CHECK(m >= lo && m <= hi, "pattern %d: %d out of %d..%d", p, m, lo, hi);
                    CHECK(tones >> (m % 12) & 1u, "pattern %d: %d off the chord", p, m);
                    CHECK(m != last, "pattern %d: %d twice, over a change of chord", p, m);
                    if (p != EN_ARP_CONVERGE) {
                        const int is = m == (p == EN_ARP_PEDTOP ? o[n - 1] : o[0]);
                        if (was_pedal == 0)
                            CHECK(is, "pattern %d: two notes running with no pedal", p);
                        if (was_pedal == 1)
                            CHECK(!is, "pattern %d: the pedal twice", p);
                        was_pedal = is;
                    }
                    last = m;
                }
            }
        }
    }
    /* a turn of the pattern: a new one from its own beginning; from what is left of another, nothing out of bounds; the first four go on from the note, as they always have */
    en_arp_start(&a);
    for (i = 0; i < 5; i++)
        en_arp_next(&a, 0x091u, 0, EN_ARP_CONVERGE, 1u);
    CHECK(en_arp_next(&a, 0x091u, 0, EN_ARP_PEDTOP, 1u) == 96, "to pedal top: from its beginning");
    CHECK(en_arp_next(&a, 0x091u, 0, EN_ARP_PEDTOP, 1u) == 72, "pedal top's second");
    en_arp_start(&a);
    en_arp_next(&a, 0x091u, 0, EN_ARP_UP, 1u);
    en_arp_next(&a, 0x091u, 0, EN_ARP_UP, 1u);   /* 76 */
    CHECK(en_arp_next(&a, 0x091u, 0, EN_ARP_CONVERGE, 1u) == 72, "up, then converge: its lowest");
    CHECK(en_arp_next(&a, 0x091u, 0, EN_ARP_CONVERGE, 1u) == 96, "converge's second");
    CHECK(en_arp_next(&a, 0x091u, 0, EN_ARP_UP, 1u) == 72, "converge, then up: up from where it is, over the top");
    CHECK(en_arp_next(&a, 0x091u, 0, EN_ARP_CONVERGE, 1u) == 96, "up, then converge: its lowest is what just sounded, so the next");
}

/* ---- Skip, the rule: what share of a bar's steps, which, and the same ones again */
static uint32_t bar_rests(uint32_t seed, int div, int skip, uint32_t bar)
{
    uint32_t m = 0u, i;
    for (i = 0u; i < 16u; i += (uint32_t)div)
        if (en_arp_rest(seed, bar * 16u + i, div, skip))
            m |= 1u << i;
    return m;
}
static int bits16(uint32_t m)
{
    int n = 0;
    while (m) {
        n += (int)(m & 1u);
        m >>= 1;
    }
    return n;
}
static void t_skip_rule(void)
{
    static const int DIV[3] = {4, 2, 1};
    uint32_t sd;
    int d;
    for (d = 0; d < 3; d++) {
        const int n = 16 / DIV[d];
        uint32_t beat = 0u, off = 0u, nb = 0u, no = 0u;
        for (sd = 0u; sd < 400u; sd++) {
            const uint32_t seed = sd * 2654435761u + 17u;
            int p;
            uint32_t was = 0u;
            for (p = 0; p <= 100; p++) {
                const uint32_t m = bar_rests(seed, DIV[d], p, 0u);
                uint32_t b;
                CHECK(bits16(m) == p * n / 100, "division %d Skip %d: %d rests of %d", DIV[d], p, bits16(m), n);
                CHECK(!(was & ~m), "division %d: the rests of Skip %d not all in Skip %d: %04x, %04x", DIV[d], p - 1, p, was, m);
                if (p < 100)
                    CHECK(!(m & 1u), "division %d Skip %d: the downbeat rests", DIV[d], p);
                for (b = 1u; b < 5u; b++)
                    CHECK(bar_rests(seed, DIV[d], p, b * 37u) == m, "division %d Skip %d: another bar, another rhythm", DIV[d], p);
                was = m;
            }
            CHECK(bar_rests(seed, DIV[d], 0, 0u) == 0u, "Skip 0 rests");
            CHECK(bits16(bar_rests(seed, DIV[d], 100, 0u)) == n, "Skip 100 strikes");
            if (d == 2) {                        /* the beats sound more than the rest, a preference and no more */
                const uint32_t m = bar_rests(seed, 1, 50, 0u);
                uint32_t i;
                for (i = 1u; i < 16u; i++) {
                    if (!(i & 3u)) {
                        nb++;
                        beat += m >> i & 1u;
                    } else if (i & 1u) {
                        no++;
                        off += m >> i & 1u;
                    }
                }
            }
        }
        if (d == 2) {
            const double b = (double)beat / (double)nb, o = (double)off / (double)no;
            printf("  Skip 50 at a sixteenth, %u seeds: a beat rests %.0f%%, an odd sixteenth %.0f%%\n", 400u, b * 100.0, o * 100.0);
            CHECK(b < o, "a beat rests as often as an odd sixteenth: %f %f", b, o);
            CHECK(b > 0.05, "a beat never rests: the rhythm is only the grid: %f", b);
        }
    }
    /* the seed's: another piece another rhythm. Not one figure for all, and not a plain grid */
    for (d = 0; d < 3; d++) {
        static const int WANT[3] = {3, 10, 60};
        uint32_t seen[400];
        int ns = 0;
        for (sd = 0u; sd < 400u; sd++) {
            const uint32_t m = bar_rests(sd * 2654435761u + 5u, DIV[d], 50, 0u);
            int k, f = 0;
            for (k = 0; k < ns; k++)
                f |= seen[k] == m;
            if (!f)
                seen[ns++] = m;
        }
        printf("  Skip 50 at %d: %d figures in 400 seeds\n", DIV[d], ns);
        CHECK(ns >= WANT[d], "Skip 50, division %d: only %d figures in 400 seeds", DIV[d], ns);
    }
}

/* ---- Skip, played: what a step strikes, a note a step, on the grid */
static uint8_t rec_been[REC_STEPS];
static void rec_clear(void)
{
    memset(rec_note, 0, sizeof rec_note);
    other_hash = 2166136261u;
    rec_on = 1;
}
static void rec_run(uint32_t seed, int rate, int pattern, int skip, int mode, int nexts)
{
    uint32_t i;
    start(seed, EN_ST_ROLLER, mode);
    all_on();
    en_set(P_MELMODE, EN_MEL_ARP);
    en_set(P_ARPPAT, pattern);
    en_set(P_ARPSKIP, skip);
    en_set(P_MRATE, rate);
    en_set(P_TEMPO, 180);
    en_play(1);
    en_render(buf, 16u, GAIN);
    rec_clear();
    memset(rec_been, 0, sizeof rec_been);
    for (i = 0u; i < 3200u; i++) {
        en_test_ticks(1u);
        if (en_test_song() - 1u < REC_STEPS)     /* (the step it played: a jump is on the bar, before it) */
            rec_been[en_test_song() - 1u] = 1u;
        if (nexts && !(i % 800u)) {              /* (a section's jump goes past some steps) */
            en_next();
            en_render(buf, 16u, GAIN);
        }
    }
    rec_on = 0;
}
static void t_skip_played(void)
{
    static const int RATE[3] = {10, 50, 90}, DIV[3] = {4, 2, 1}, PAT[4] = {EN_ARP_UP, EN_ARP_RANDOM, EN_ARP_PEDROOT, EN_ARP_CONVERGE}, SHARE[3] = {25, 50, 75};
    static uint8_t zero[REC_STEPS];
    int d, q, s;
    for (d = 0; d < 3; d++)
        for (q = 0; q < 4; q++) {
            const uint32_t seed = 900u + (uint32_t)(d * 4 + q);
            uint32_t oh, was_rests = 0u;
            rec_run(seed, RATE[d], PAT[q], 0, EN_HANDS, 0);
            memcpy(zero, rec_note, sizeof zero);
            oh = other_hash;
            {                                    /* Skip 0: every step of the grid a note */
                uint32_t st, miss = 0u, off = 0u;
                for (st = 64u; st < 3200u; st++) {
                    if (st % (uint32_t)DIV[d])
                        off += zero[st] != 0u;
                    else
                        miss += zero[st] == 0u;
                }
                CHECK(!miss && !off, "division %d pattern %d Skip 0: %u steps without a note, %u notes off the grid", DIV[d], PAT[q], miss, off);
            }
            for (s = 0; s < 3; s++) {
                uint32_t st, grid = 0u, rests = 0u, wrong = 0u, moved = 0u, seen_seed;
                uint32_t first = 0u, mask = 0u, bad_bars = 0u, bar;
                rec_run(seed, RATE[d], PAT[q], SHARE[s], EN_HANDS, 0);
                seen_seed = en_test_seed();
                for (st = 64u; st < 3200u; st += (uint32_t)DIV[d]) {
                    const int r = en_arp_rest(seen_seed, st, DIV[d], SHARE[s]);
                    grid++;
                    rests += rec_note[st] == 0u;
                    wrong += r ? rec_note[st] != 0u : rec_note[st] != zero[st];   /* a rest in the running figure: the same note, or none */
                    moved += !r && !zero[st];
                }
                CHECK(!wrong, "division %d pattern %d Skip %d: %u steps not as Skip 0 less its rests", DIV[d], PAT[q], SHARE[s], wrong);
                CHECK(!moved, "(nothing at Skip 0 where there is a note now)");
                CHECK(other_hash == oh, "division %d pattern %d Skip %d: the other layers' MIDI moved", DIV[d], PAT[q], SHARE[s]);
                {                                /* its share, within a percentage point over 200 bars */
                    const double share = (double)rests / (double)grid * 100.0;
                    if (!q)
                        printf("  Skip %d at %d: %.1f%% of %u steps struck nothing\n", SHARE[s], DIV[d], share, grid);
                    CHECK(share > (double)SHARE[s] - 1.0 && share < (double)SHARE[s] + 1.0, "division %d Skip %d: a share of %f", DIV[d], SHARE[s], share);
                }
                /* the same rests on repeat, heard, and each setting's within the next's */
                for (bar = 4u; bar < 200u; bar++) {
                    uint32_t i;
                    mask = 0u;
                    for (i = 0u; i < 16u; i += (uint32_t)DIV[d])
                        if (!rec_note[bar * 16u + i])
                            mask |= 1u << i;
                    if (bar == 4u)
                        first = mask;
                    bad_bars += mask != first;
                }
                CHECK(!bad_bars, "division %d Skip %d: %u bars of another rhythm", DIV[d], SHARE[s], bad_bars);
                if (s)
                    CHECK(!(was_rests & ~first), "division %d: the rests of Skip %d, %04x, not all in Skip %d's, %04x", DIV[d], SHARE[s - 1], was_rests, SHARE[s], first);
                was_rests = first;
            }
            /* through sections and in Auto the same rule, by the bar */
            for (s = 0; s < 2; s++) {
                uint32_t st, wrong = 0u, been = 0u;
                rec_run(seed, RATE[d], PAT[q], 50, s ? EN_AUTO : EN_HANDS, 1);
                for (st = 64u; st < REC_STEPS; st += (uint32_t)DIV[d])
                    if (rec_been[st]) {
                        been++;
                        wrong += en_arp_rest(en_test_seed(), st, DIV[d], 50) != !rec_note[st];
                    }
                CHECK(!wrong, "division %d %s through sections: %u steps not by the rule", DIV[d], s ? "Auto" : "Hands", wrong);
                CHECK(been > 300u / (uint32_t)DIV[d], "division %d %s through sections: only %u steps", DIV[d], s ? "Auto" : "Hands", been);
            }
        }
}

/* every chord and key, at a sixteenth at 180, Skip on the new patterns too: of the chord, in range, one key at a time */
static void t_skip_every_chord(void)
{
    static const int SKIP[4] = {25, 50, 75, 99};
    int m, key, p, runs = 0;
    for (m = 0; m < EN_NMODES; m++)
        for (key = 0; key < 12; key++)
            for (p = 0; p < EN_ARP_NPATTERNS; p++) {
                start(300u + (uint32_t)(m * 12 + key), p % EN_NSTYLES, EN_HANDS);
                en_set(P_TEMPO, 180);
                en_set(P_MODE, m);
                en_set(P_ROOT, key);
                en_pool(EN_MODE_POOL[m]);
                en_set(P_MELMODE, EN_MEL_ARP);
                en_set(P_ARPPAT, p);
                en_set(P_ARPSKIP, SKIP[(m + key + p) & 3]);
                en_set(P_MRATE, 100);
                en_set(P_EVOLVE, 100);
                en_play(1);
                en_render(buf, 16u, GAIN);
                en_arp_range(key, &mel_lo, &mel_hi);
                mel_chord_check = mel_range_check = 1;
                en_test_ticks(800u);
                CHECK(ons[1] > 0u, "mode %d key %d pattern %d: nothing", m, key, p);
                CHECK(!off_chord && !off_range, "mode %d key %d pattern %d: %u off the chord, %u out of range", m, key, p, off_chord, off_range);
                CHECK(most_down <= 1, "mode %d key %d pattern %d: %d keys down", m, key, p, most_down);
                CHECK(!double_on && !off_unowed, "mode %d key %d pattern %d: pairs: %u twice on, %u off with none on", m, key, p, double_on, off_unowed);
                CHECK(nd[1] == en_test_bells_on(), "mode %d key %d pattern %d: the ledger", m, key, p);
                runs++;
            }
    printf("  every chord with Skip: %d runs of 800 sixteenths at 180\n", runs);
}

/* the silences: Skip 100 strikes nothing, no voice, no MIDI, the rest going on; at once on the way there and back; and it goes on composing the note: Skip's off is the note it would have been */
static void t_skip_silent(void)
{
    static const int RATE[3] = {10, 50, 90};
    int d;
    for (d = 0; d < 3; d++) {
        start(1100u + (uint32_t)d, EN_ST_LIQUID, EN_HANDS);
        all_on();
        en_set(P_MELMODE, EN_MEL_ARP);
        en_set(P_ARPSKIP, 100);
        en_set(P_MRATE, RATE[d]);
        en_set(P_MELSND, EN_TB_GLASS + 1);
        en_play(1);
        ledger = 1;
        run(SEC(12.0f), 256u);
        CHECK(ons[1] == 0u, "Skip 100, Rate %d: %u notes", RATE[d], ons[1]);
        CHECK(voices_most == 0u, "Skip 100, Rate %d: %u voices", RATE[d], voices_most);
        CHECK(ons[0] + ons[2] + ons[3] + ons[9] > 10u, "Skip 100: and the others?");
        /* on the way back, the next step's */
        en_set(P_ARPSKIP, 0);
        run(SEC(2.0f), 256u);
        CHECK(ons[1] >= 3u, "Skip 100 to 0, Rate %d: %u notes in 2 s", RATE[d], ons[1]);
        en_set(P_ARPSKIP, 100);
        run(SEC(0.5f), 256u);
        counts_clear();
        run(SEC(2.0f), 256u);
        CHECK(ons[1] == 0u, "to Skip 100, Rate %d: %u notes more", RATE[d], ons[1]);
        run(SEC(2.0f), 256u);
        CHECK(en_test_bells_on() == 0 && nd[1] == 0, "to Skip 100, Rate %d: a key down", RATE[d]);
        CHECK(en_test_bell_voices() == 0, "to Skip 100, Rate %d: %d voices sounding after 4.5 s", RATE[d], en_test_bell_voices());
        ledger = 0;
    }
    /* only the melody, at Skip 100: nothing to hear */
    start(1110u, EN_ST_LIQUID, EN_HANDS);
    only_melody();
    en_set(P_MELMODE, EN_MEL_ARP);
    en_set(P_ARPSKIP, 100);
    en_set(P_MRATE, 90);
    en_play(1);
    run(SEC(3.0f), 256u);
    meter_clear();
    run(SEC(2.0f), 256u);
    CHECK(rms_now() < 1e-4, "only the melody at Skip 100: %f", rms_now());
    /* the melody off: the same MIDI, rests and all */
    {
        uint32_t a, n;
        start(1120u, EN_ST_LIQUID, EN_HANDS);
        all_on();
        en_set(P_MELMODE, EN_MEL_ARP);
        en_set(P_ARPSKIP, 50);
        en_set(P_MRATE, 90);
        en_play(1);
        run(SEC(12.0f), 256u);
        a = mhash;
        n = midi_n;
        start(1120u, EN_ST_LIQUID, EN_HANDS);
        all_on();
        en_set(P_MELMODE, EN_MEL_ARP);
        en_set(P_ARPSKIP, 50);
        en_set(P_MRATE, 90);
        en_play(1);
        run(SEC(4.0f), 256u);
        en_layer(EN_L_MEL, 0);
        run(SEC(8.0f), 256u);
        CHECK(mhash == a && midi_n == n, "Skip 50, the melody off: its MIDI moved");
    }
    CHECK(!led_bad, "the ledger: %u bad blocks", led_bad);
}

/* ---- MIDI pairs, the ledger: at each setting, through changes of the pattern and of Skip of any size at any moment, a panic and a new piece */
static void t_skip_pairs(void)
{
    uint32_t i, sd;
    int skip, q;
    for (skip = 0; skip <= 100; skip += 5) {
        start(1200u + (uint32_t)skip, EN_ST_LIQUID, EN_HANDS);
        all_on();
        en_set(P_MELMODE, EN_MEL_ARP);
        en_set(P_MRATE, skip & 1 ? 50 : 90);
        en_set(P_ARPPAT, skip / 5 % EN_ARP_NPATTERNS);
        en_set(P_ARPSKIP, skip);
        en_play(1);
        ledger = 1;
        run(SEC(5.0f), 256u);
        ledger = 0;
        CHECK(!double_on && !off_unowed, "Skip %d: pairs: %u twice on, %u off with none on", skip, double_on, off_unowed);
        CHECK(most_down <= 1, "Skip %d: %d keys down", skip, most_down);
    }
    /* changed at any moment */
    start(1300u, EN_ST_ROLLER, EN_HANDS);
    all_on();
    en_set(P_MELMODE, EN_MEL_ARP);
    en_set(P_MRATE, 100);
    en_play(1);
    ledger = 1;
    for (i = 0u; i < 3000u; i++) {
        const uint32_t h = i * 2654435761u;
        en_set(P_ARPSKIP, (int)(h >> 8) % 101);
        en_set(P_ARPPAT, (int)(h >> 16) % EN_ARP_NPATTERNS);
        if (!(i % 37u))
            en_set(P_MELMODE, (int)(h >> 24 & 1u));
        if (!(i % 211u))
            en_set(P_MRATE, (int)(h >> 20) % 101);
        run(256u + 64u * (i % 7u), 256u);
    }
    CHECK(!double_on && !off_unowed, "changes: pairs: %u twice on, %u off with none on", double_on, off_unowed);
    CHECK(most_down <= 1, "changes: %d keys down", most_down);
    en_set(P_MELMODE, EN_MEL_ARP);
    en_set(P_ARPSKIP, 100);
    run(SEC(3.0f), 256u);
    CHECK(nd[1] == 0, "changes, to Skip 100: %d keys down", nd[1]);
    ledger = 0;
    /* a panic, in a figure with rests: nothing; a restore, the same rests */
    for (q = 0; q < EN_ARP_NPATTERNS; q++) {
        start(1400u + (uint32_t)q, EN_ST_LIQUID, EN_HANDS);
        en_set(P_MELMODE, EN_MEL_ARP);
        en_set(P_ARPPAT, q);
        en_set(P_ARPSKIP, 50);
        en_set(P_MRATE, 100);
        en_play(1);
        ledger = 1;
        run(SEC(3.0f) + (uint32_t)q * 256u, 256u);
        en_play(0);
        en_panic();
        ledger = 0;
        run(256u, 256u);
        CHECK(nd[1] == 0 && en_test_bell_voices() == 0, "a panic, pattern %d: %d down, %d voices", q, nd[1], en_test_bell_voices());
        counts_clear();
        en_set(P_ARPSKIP, 25);                   /* (a change through the silence, too) */
        run(SEC(1.0f), 256u);
        CHECK(ons[1] == 0u, "silent, and %u notes", ons[1]);
        en_restore();
        run(SEC(3.0f), 256u);
        ledger = 1;
        CHECK(ons[1] > 5u, "back, pattern %d: %u notes", q, ons[1]);
        CHECK(!double_on && !off_unowed, "a panic, pattern %d: pairs", q);
        run(SEC(1.0f), 256u);
        ledger = 0;
    }
    /* a new piece, as project_apply and owed_tick do it: from Skip to its Skip and pattern */
    for (sd = 0u; sd < 8u; sd++) {
        int p;
        int16_t par[P_NPARAMS];
        uint16_t pool;
        start(1500u + sd, EN_ST_LIQUID, EN_HANDS);
        all_on();
        en_set(P_MELMODE, EN_MEL_ARP);
        en_set(P_ARPPAT, EN_ARP_CONVERGE + (int)(sd % 3u));
        en_set(P_ARPSKIP, sd & 1u ? 60 : 30);
        en_set(P_MRATE, 90);
        en_play(1);
        ledger = 1;
        run(SEC(2.0f) + sd * 512u, 256u);
        en_piece((int)sd, par, &pool);
        CHECK(par[P_ARPSKIP] == 0, "piece %u: a recipe with Skip %d", sd, par[P_ARPSKIP]);
        par[P_MELMODE] = EN_MEL_ARP;
        par[P_MRATE] = 90;
        par[P_ARPPAT] = (int16_t)(EN_ARP_PEDTOP + (int)(sd & 1u));
        par[P_ARPSKIP] = (int16_t)(sd & 2u ? 40 : 0);
        for (p = 0; p < P_NPARAMS; p++)
            en_set(p, par[p]);
        en_pool(pool);
        en_seed(1000u + sd);
        en_regrow();
        counts_clear();
        run(SEC(6.0f), 256u);
        CHECK(ons[1] > 10u, "piece %u: %u notes", sd, ons[1]);
        CHECK(!double_on && !off_unowed, "piece %u: pairs: %u, %u", sd, double_on, off_unowed);
        CHECK(most_down <= 1, "piece %u: %d keys down", sd, most_down);
        ledger = 0;
    }
    CHECK(!led_bad, "the ledger: %u bad blocks", led_bad);
}

/* ---- the voices: at a sixteenth, every new pattern and Skip, none taken while it can be heard */
static void t_skip_voices(void)
{
    int p, t;
    for (p = EN_ARP_CONVERGE; p < EN_ARP_NPATTERNS; p++)
        for (t = EN_NTIMBRES; t >= EN_NTIMBRES - 1; t--) {
            start(1600u + (uint32_t)p, EN_ST_LIQUID, EN_HANDS);
            en_test_bell_stolen_clear();
            en_set(P_TEMPO, 180);
            en_set(P_MELMODE, EN_MEL_ARP);
            en_set(P_MRATE, 100);
            en_set(P_ARPPAT, p);
            en_set(P_MELSND, t);
            en_set(P_MTONE, 100);
            en_play(1);
            run(SEC(15.0f), 256u);
            CHECK(voices_most <= 3u, "pattern %d timbre %d: %u voices", p, t, voices_most);
            CHECK(en_test_bell_stolen() < 0.02f, "pattern %d timbre %d: a voice taken at %f", p, t, (double)en_test_bell_stolen());
        }
}

/* ---- how often, in Phrases. Hands, section by section; phrases in three minutes at 170 (2040 sixteenths), and the longest silence between the end of one and the first note of the next */
static uint32_t ph_n, ph_prev_end, ph_silence_most, ph_silence_sum, ph_silence_n;
static void on_mel(uint32_t step, const en_phrase_t *p)
{
    const uint32_t first = step + p->note[0].step;
    if (ph_n && first > ph_prev_end && first - ph_prev_end > ph_silence_most)
        ph_silence_most = first - ph_prev_end;
    if (ph_n && first > ph_prev_end) {
        ph_silence_sum += first - ph_prev_end;
        ph_silence_n++;
    }
    ph_prev_end = step + p->end;
    ph_n++;
}
#define NSEEDS 40u
static void phrases_of(int mode, int section, int rate, double *per3min, uint32_t *silence)
{
    uint32_t sd, sum = 0u, most = 0u;
    for (sd = 0u; sd < NSEEDS; sd++) {
        int k;
        start(500u + sd * 37u, (int)(sd % EN_NSTYLES), mode);
        en_set(P_MRATE, rate);
        en_play(1);
        en_render(buf, 16u, GAIN);
        if (mode == EN_AUTO)
            en_hold(1);                          /* (stays where it is told) */
        for (k = 0; k < 90 && en_section != section; k++) {   /* (Loading seldom goes by its full section) */
            en_next();
            en_render(buf, 16u, GAIN);
            en_test_ticks(16u);
        }
        CHECK(en_section == section, "got to section %d, not %d", en_section, section);
        en_test_ticks(32u);                      /* (settled in) */
        ph_n = 0u;
        ph_prev_end = 0u;
        ph_silence_most = 0u;
        if (rate != 100)
            ph_silence_sum = ph_silence_n = 0u;
        en_trace_mel = on_mel;
        en_test_ticks(16u * 8u);
        ph_n = 0u;                               /* (from here it is that section's own pace) */
        en_test_ticks(2040u);
        en_trace_mel = 0;
        sum += ph_n;
        if (ph_silence_most > most)
            most = ph_silence_most;
    }
    *per3min = (double)sum / (double)NSEEDS;
    *silence = most;
}
static void t_rate(void)
{
    static const int RATE[6] = {0, 50, 60, 75, 90, 100};
    double t[EN_NSECTIONS][6];
    uint32_t sil[EN_NSECTIONS][6], s100 = 0u;
    int sec, r;
    printf("  phrases per 3 minutes in Hands (%u seeds, every style; in brackets the longest silence, in bars)\n", NSEEDS);
    printf("  %-10s", "Rate");
    for (r = 0; r < 6; r++)
        printf(" %11d", RATE[r]);
    printf("\n");
    for (sec = 0; sec < EN_NSECTIONS; sec++) {
        printf("  %-10s", EN_SECTION_NAME[sec]);
        for (r = 0; r < 6; r++) {
            phrases_of(EN_HANDS, sec, RATE[r], &t[sec][r], &sil[sec][r]);
            printf(" %5.1f (%3u)", t[sec][r], sil[sec][r] / 16u);
        }
        printf("\n");
    }
    for (sec = 0; sec < EN_NSECTIONS; sec++) {
        /* above 60 it goes up, in every section; at 100 they come about back to back, in every section alike */
        CHECK(t[sec][3] > t[sec][2], "%s: Rate 75 no more than 60: %f %f", EN_SECTION_NAME[sec], t[sec][3], t[sec][2]);
        CHECK(t[sec][5] > t[sec][3], "%s: Rate 100 no more than 75: %f %f", EN_SECTION_NAME[sec], t[sec][5], t[sec][3]);
        CHECK(t[sec][5] >= 25.0, "%s: Rate 100, only %f phrases in three minutes", EN_SECTION_NAME[sec], t[sec][5]);
        CHECK(sil[sec][5] <= 4u * 16u, "%s: Rate 100, a silence of %u sixteenths", EN_SECTION_NAME[sec], sil[sec][5]);
        if (sil[sec][5] > s100)
            s100 = sil[sec][5];
    }
    for (r = 3; r < 6; r++) {                    /* the calm sections count for less, the more there is of it */
        CHECK(t[EN_SEC_LIGHT][r] / t[EN_SEC_OUT][r] < t[EN_SEC_LIGHT][2] / fmax(t[EN_SEC_OUT][2], 0.5), "Rate %d: Light against Out", RATE[r]);
    }
    printf("  Rate 100: the longest silence anywhere %.1f bars, on average %.1f\n", (double)s100 / 16.0, ph_silence_n ? (double)ph_silence_sum / (double)ph_silence_n / 16.0 : 0.0);
    /* in Auto, what it was: the same as the hashes, below; here its count for the report */
    printf("  in Auto (held in each section, today's curve):\n");
    for (sec = 0; sec < EN_NSECTIONS; sec++) {
        double a, b, c, d;
        uint32_t u;
        phrases_of(EN_AUTO, sec, 0, &a, &u);
        phrases_of(EN_AUTO, sec, 50, &b, &u);
        phrases_of(EN_AUTO, sec, 75, &c, &u);
        phrases_of(EN_AUTO, sec, 100, &d, &u);
        printf("  %-10s %5.1f %5.1f %5.1f %5.1f\n", EN_SECTION_NAME[sec], a, b, c, d);
    }
}

/* ---- what must not have moved: renders of 0a686da, the commit before this one. Hands with all the layers from the start and no press after, Rate up to 60, every style; Auto, up to 100 */
static const struct {
    int mode, style, rate;
    uint32_t audio, midi, n;
} PIN[] = {
    {EN_HANDS, 0, 50, 0x2863f1b1u, 0x27b01489u, 8353u},
    {EN_HANDS, 1, 50, 0x11e7520au, 0x6998248fu, 9156u},
    {EN_HANDS, 2, 50, 0x0a608559u, 0x2a58f971u, 8543u},
    {EN_HANDS, 3, 50, 0x186b6df3u, 0x7b4093d1u, 7793u},
    {EN_HANDS, 4, 50, 0xe9a50563u, 0x96591bf8u, 7457u},
    {EN_HANDS, 5, 50, 0x5c207c69u, 0x7413cb05u, 7932u},
    {EN_HANDS, 0, 0, 0x8160aa45u, 0xcd2b1485u, 8290u},
    {EN_HANDS, 1, 25, 0x16be03ccu, 0x15ea4dadu, 9130u},
    {EN_HANDS, 3, 55, 0xfb516513u, 0xfc806856u, 7800u},
    {EN_HANDS, 4, 60, 0xc57e72efu, 0x83566d85u, 7459u},
    {EN_AUTO, 0, 75, 0xf8ca4ca0u, 0x364f9dc6u, 7192u},
    {EN_AUTO, 1, 100, 0xdd514ca8u, 0x0f1113c9u, 8087u},
    {EN_AUTO, 5, 100, 0xcea67aefu, 0x41934f88u, 6875u},
};
static void t_pins(void)
{
    uint32_t i;
    for (i = 0u; i < sizeof PIN / sizeof PIN[0]; i++) {
        uint32_t t;
        start(1234u + (uint32_t)PIN[i].style, PIN[i].style, PIN[i].mode);
        all_on();
        en_set(P_MRATE, PIN[i].rate);
        en_play(1);
        mhash = ahash = 2166136261u;
        frames = midi_n = 0u;
        for (t = 0u; t < 44100u * 90u / 256u; t++)
            run(256u, 256u);
        CHECK(ahash == PIN[i].audio, "mode %d style %d Rate %d: the sound moved: %08x", PIN[i].mode, PIN[i].style, PIN[i].rate, ahash);
        CHECK(mhash == PIN[i].midi && midi_n == PIN[i].n, "mode %d style %d Rate %d: MIDI moved: %08x, %u messages", PIN[i].mode, PIN[i].style, PIN[i].rate, mhash, midi_n);
    }
}

/* ---- what must not have moved: Arp in its first four patterns and Skip 0: renders of b34adef, the commit before Skip. The last of each Rate's five goes through the four */
static const struct {
    int rate, pat;
    uint32_t audio, midi, n;
} ARP_PIN[] = {
    {10, 0, 0x982d1da2u, 0xf26f09e3u, 2972u},
    {10, 1, 0xd98e6ac2u, 0xef5927d9u, 3187u},
    {10, 2, 0x0e3b321bu, 0x5cf6b216u, 3003u},
    {10, 3, 0xc953b36du, 0xc1bcb05au, 2783u},
    {10, 4, 0x2137f413u, 0xd018f03fu, 2612u},
    {50, 0, 0xdb6f89a1u, 0xecc350e7u, 3142u},
    {50, 1, 0x6889f4deu, 0x6eb6e816u, 3357u},
    {50, 2, 0x9b94feebu, 0x254035fau, 3173u},
    {50, 3, 0x709552deu, 0x34425677u, 2953u},
    {50, 4, 0x30f7a56eu, 0x2c625027u, 2782u},
    {90, 0, 0x423b578bu, 0xb028b589u, 3482u},
    {90, 1, 0x55e590deu, 0x95845f37u, 3697u},
    {90, 2, 0xc3f31d64u, 0x8b585de2u, 3513u},
    {90, 3, 0x12f2d925u, 0x6a75d9f4u, 3293u},
    {90, 4, 0x3e6960ccu, 0x4da961deu, 3122u},
};
static void t_arp_pins(void)
{
    uint32_t i;
    for (i = 0u; i < sizeof ARP_PIN / sizeof ARP_PIN[0]; i++) {
        const int p = ARP_PIN[i].pat;
        uint32_t t;
        start(4321u + (uint32_t)p, p % EN_NSTYLES, EN_HANDS);
        all_on();
        en_set(P_MELMODE, EN_MEL_ARP);
        en_set(P_ARPPAT, p & 3);
        en_set(P_MRATE, ARP_PIN[i].rate);
        en_play(1);
        mhash = ahash = 2166136261u;
        frames = midi_n = 0u;
        for (t = 0u; t < 44100u * 30u / 256u; t++) {
            if (p == 4 && !(t % 300u))
                en_set(P_ARPPAT, (int)(t / 300u & 3u));
            run(256u, 256u);
        }
        CHECK(ahash == ARP_PIN[i].audio, "Arp, Rate %d pattern %d: the sound moved: %08x", ARP_PIN[i].rate, p, ahash);
        CHECK(mhash == ARP_PIN[i].midi && midi_n == ARP_PIN[i].n, "Arp, Rate %d pattern %d: MIDI moved: %08x, %u messages", ARP_PIN[i].rate, p, mhash, midi_n);
    }
}

int main(void)
{
    t_pure();
    t_modes();
    t_skip_rule();
    t_every_chord();
    t_skip_every_chord();
    t_skip_played();
    t_skip_silent();
    t_skip_pairs();
    t_skip_voices();
    t_arp_pins();
    t_division();
    t_no_pauses();
    t_mute();
    t_room();
    t_back_to_phrases();
    t_timbre();
    t_voices();
    t_level();
    t_mode_change();
    t_duck_and_piece();
    t_queue();
    t_rate();
    t_pins();
    (void)mel_note_last;
    printf(fails ? "arp_test: %d FAILED\n" : "arp_test: ok\n", fails);
    return fails != 0;
}
