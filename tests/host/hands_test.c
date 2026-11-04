/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal */
/* The player's arrangement: layers on and off, and the modes, Auto and Hands. A layer off is a gain: nothing that composes or plays hears of it, so
 * what MIDI says is the same to the sample, and a dry mix comes back the same to the sample; only drums and bass wait for a bar, and nothing
 * clicks. In Hands no section comes by itself and Energy is the drums. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "amb.h"

static int fails;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); fails++; } } while (0)
#define GAIN 2430u
#define SEC(s) ((uint32_t)((s) * 44100.0f) & ~255u)   /* whole blocks of 256: a run is the same run only in the same blocks */

/* ---- MIDI: a hash of everything with its time, counts by channel and note, and a ledger against the engine */
static uint64_t frames;
static uint32_t mhash, midi_n, ons[16], drum_on[128];
static int down[16][128], nd[16];
static uint32_t led_bad;
void en_midi_out(uint32_t st, uint32_t d1, uint32_t d2)
{
    const uint32_t v[4] = {st, d1, d2, (uint32_t)frames};
    const int c = (int)(st & 15u), k = (int)(st & 0xF0u);
    int i;
    for (i = 0; i < 4; i++)
        mhash = (mhash ^ v[i]) * 16777619u;
    midi_n++;
    if (st >= 0xF0u)
        return;
    if (k == 0x90 && d2) {
        ons[c]++;
        if (c == 9)
            drum_on[d1]++;
        down[c][d1]++;
        nd[c]++;
    } else if (k == 0x80 || k == 0x90) {
        if (down[c][d1] > 0) {
            down[c][d1]--;
            nd[c]--;
        }
    } else if (k == 0xB0 && d1 == 123u) {
        memset(down[c], 0, sizeof down[c]);
        nd[c] = 0;
    }
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

/* ---- rendering */
static int32_t buf[2 * 256];
static double e_sum;
static uint32_t e_n;
static int ledger;
static void clear_counts(void)
{
    midi_n = 0u;
    memset(ons, 0, sizeof ons);
    memset(drum_on, 0, sizeof drum_on);
}
static void start(uint32_t seed, int style)
{
    en_init();
    frames = 0u;
    mhash = 2166136261u;
    memset(down, 0, sizeof down);
    memset(nd, 0, sizeof nd);
    clear_counts();
    en_set(P_STYLE, style);
    en_seed(seed);
}
/* so many frames, in blocks of this size; each frame given to f if there is one */
static void run(uint32_t n, uint32_t blk, void (*f)(const int32_t *, uint32_t, void *), void *arg)
{
    while (n) {
        const uint32_t k = n < blk ? n : blk;
        uint32_t i;
        en_render(buf, k, GAIN);
        for (i = 0; i < 2u * k; i++) {
            const double x = (double)buf[i] / 8300000.0;
            e_sum += x * x;
        }
        e_n += 2u * k;
        if (f)
            f(buf, k, arg);
        if (ledger)
            led_check();
        frames += k;
        n -= k;
    }
}
static double rms_of(uint32_t n)
{
    e_sum = 0.0;
    e_n = 0u;
    run(n, 256u, 0, 0);
    return sqrt(e_sum / (double)(e_n ? e_n : 1u));
}
static void bars(uint32_t n)                     /* to n bar lines on from here, and one block past the last */
{
    uint32_t b = en_test_song() / 16u + n;
    while (en_test_song() / 16u < b || !(en_test_song() & 15u))
        run(256u, 256u, 0, 0);
}

/* every level at nothing but one layer's, the echo and the room as they are */
static void only(int layer)
{
    en_set(P_KEYS, layer == EN_L_KEYS ? 60 : 0);
    en_set(P_MOTIFS, layer == EN_L_MEL ? 80 : 0);
    en_set(P_BASS, layer == EN_L_BASS ? 60 : 0);
    en_set(P_DRUMS, layer == EN_L_DRUMS || layer == EN_L_HATS ? 45 : 0);
    en_set(P_HATS, layer == EN_L_HATS ? 50 : 0);
    if (layer == EN_L_HATS)
        en_layer(EN_L_DRUMS, 0);
}
static int mute_of(int l)
{
    return l == EN_L_KEYS ? en_mute_keys : l == EN_L_MEL ? en_mute_mel : l == EN_L_BASS ? en_mute_bass : l == EN_L_DRUMS ? en_mute_drums : en_mute_hats;
}
static const char *const LNAME[EN_NLAYERS] = {"keys", "melody", "bass", "drums", "hats"};
static const int LCH[EN_NLAYERS] = {0, 1, 2, 9, 9};

/* ---- Energy, as a table: every style, every value */
static void test_hands_part(void)
{
    int st, e;
    static const uint8_t RANK[5] = {0, 1, 3, 4, 2};
    for (st = 0; st < EN_NSTYLES; st++) {
        int last_rank = -1, last_char = -1, last_bass = -1;
        for (e = 0; e <= 100; e++) {
            en_part_t p;
            int want = e == 0 ? 0 : e < 10 ? 1 : e < 35 ? 4 : e < 60 ? 2 : 3;
            if (st == EN_ST_DRIFT && want != 0 && want != 1)
                want = 4;                        /* its peak is half time: its own numbers */
            en_hands_part(st, e, &p);
            CHECK(p.level == want, "style %d energy %d: level %d, wanted %d", st, e, p.level, want);
            CHECK(RANK[p.level] >= last_rank && p.character >= last_char && p.bass >= last_bass, "style %d energy %d goes down somewhere", st, e);
            CHECK(p.bass > 0u, "style %d energy %d: no bass for the player to have", st, e);
            CHECK(p.ramp == 1u, "style %d energy %d: drums take %d bars", st, e, p.ramp);
            CHECK(p.character <= 3u && p.section < EN_NSECTIONS, "style %d energy %d", st, e);
            if (e < 60)
                CHECK(p.character < 3u, "style %d energy %d: the peak under the full rhythm", st, e);
            last_rank = RANK[p.level];
            last_char = p.character;
            last_bass = p.bass;
        }
        {
            en_part_t a, b;
            en_hands_part(st, 0, &a);
            en_hands_part(st, 100, &b);
            CHECK(a.character == 0u && b.character == 3u, "style %d: its character runs from %d to %d", st, a.character, b.character);
        }
    }
}

/* ---- a layer off: silent in the audio, its MIDI going on */
static void test_silent(void)
{
    int l;
    for (l = 0; l < EN_NLAYERS; l++) {
        double on, off;
        uint32_t n0;
        start(1001u, EN_ST_LIQUID);
        en_arrange(EN_HANDS, 1);
        en_set(P_ENERGY, 75);
        only(l);
        en_play(1);
        run(SEC(5.0f), 256u, 0, 0);
        on = rms_of(SEC(30.0f));
        CHECK(on > 0.001, "%s by itself is hardly there: %f", LNAME[l], on);
        CHECK(!mute_of(l), "%s starts off", LNAME[l]);
        en_layer(l, 0);
        run(1024u, 256u, 0, 0);
        CHECK(mute_of(l), "%s: the state does not say off", LNAME[l]);
        run(SEC(30.0f), 256u, 0, 0);                 /* the room rings out */
        clear_counts();
        off = rms_of(SEC(30.0f));
        CHECK(off < on * 0.003, "%s off: %f, on %f", LNAME[l], off, on);
        n0 = l == EN_L_HATS ? drum_on[42] + drum_on[46] : l == EN_L_DRUMS ? drum_on[36] + drum_on[38] : ons[LCH[l]];
        CHECK(n0 > 3u, "%s off: its MIDI stops (%u)", LNAME[l], n0);
        en_layer(l, 1);
        run(SEC(3.0f), 256u, 0, 0);
        on = rms_of(SEC(30.0f));
        CHECK(on > 0.001, "%s on again is hardly there: %f", LNAME[l], on);
    }
}

/* what a layer off is, to the sample: that layer at level 0, kept off from before anything sounds */
static uint32_t ahash;
static void hash_block(const int32_t *b, uint32_t n, void *arg)
{
    uint32_t i;
    (void)arg;
    for (i = 0; i < 2u * n; i++)
        ahash = (ahash ^ (uint32_t)b[i]) * 16777619u;
}
static void test_like_level_0(void)
{
    static const int P[3] = {P_KEYS, P_MOTIFS, P_BASS};
    int l;
    for (l = 0; l < 3; l++) {
        uint32_t a, b, ma, mb;
        start(58u, EN_ST_ROLLER);
        en_set(P[l], 0);
        run(SEC(1.5f), 256u, 0, 0);
        en_play(1);
        ahash = 2166136261u;
        run(SEC(90.0f), 256u, hash_block, 0);
        a = ahash;
        ma = mhash;
        start(58u, EN_ST_ROLLER);
        en_layer(l, 0);
        run(SEC(1.5f), 256u, 0, 0);
        en_play(1);
        ahash = 2166136261u;
        run(SEC(90.0f), 256u, hash_block, 0);
        b = ahash;
        mb = mhash;
        CHECK(a == b, "%s off is not %s at level 0, in the audio", LNAME[l], LNAME[l]);
        CHECK(ma == mb, "%s off is not %s at level 0, in MIDI", LNAME[l], LNAME[l]);
    }
}

/* ---- every note the same, on or off: all five layers going on and off at random, seeds and styles, both modes */
static uint32_t lfsr = 0xACE1u;
static uint32_t rnd16(void)
{
    lfsr ^= lfsr << 13;
    lfsr ^= lfsr >> 17;
    lfsr ^= lfsr << 5;
    return lfsr >> 8 & 0xFFFFu;
}
static uint32_t music_sig(void)                  /* where the song has got to */
{
    uint32_t h = 2166136261u;
    int i;
    for (i = 0; i < EN_NPAD; i++)
        h = (h ^ en_chord[i]) * 16777619u;
    h = (h ^ en_section) * 16777619u;
    h = (h ^ en_drum_level) * 16777619u;
    h = (h ^ en_test_song()) * 16777619u;
    h = (h ^ en_hits) * 16777619u;
    h = (h ^ en_hat_hits) * 16777619u;
    return h;
}
static void test_same_notes(void)
{
    static const uint32_t SEEDS[3] = {7u, 58u, 424242u};
    int st, s, m;
    for (st = 0; st < EN_NSTYLES; st++)
        for (s = 0; s < 3; s++)
            for (m = 0; m < 2; m++) {
                uint32_t a, b, sa, sb, t;
                start(SEEDS[s], st);
                en_arrange(m, 1);
                en_play(1);
                for (t = 0u; t < 150u; t++)
                    run(SEC(1.0f), 256u, 0, 0);
                a = mhash;
                sa = music_sig();
                start(SEEDS[s], st);
                en_arrange(m, 1);
                en_play(1);
                for (t = 0u; t < 150u; t++) {
                    if (rnd16() & 1u) {
                        const uint32_t r = rnd16();
                        en_layer((int)(r % EN_NLAYERS), (int)(r >> 8 & 1u));
                    }
                    run(SEC(1.0f), 256u, 0, 0);
                }
                b = mhash;
                sb = music_sig();
                CHECK(a == b, "style %d seed %u mode %d: MIDI moved with the layers off", st, SEEDS[s], m);
                CHECK(sa == sb, "style %d seed %u mode %d: the song moved with the layers off", st, SEEDS[s], m);
            }
}

/* the whole sound comes back. Dry (no room, no echo, so nothing remembers): each layer away for a while, then to the sample what it would have
 * been. If muting skipped any voice, its phases, its noise and its filters would give it away */
static int32_t *rec_a;
static void rec_block(const int32_t *b, uint32_t n, void *arg)
{
    uint32_t *at = arg;
    memcpy(rec_a + 2u * *at, b, 2u * n * sizeof(int32_t));
    *at += n;
}
#define REC_LEN SEC(40.0f)
static void dry_start(uint32_t seed, int style)
{
    start(seed, style);
    en_arrange(EN_HANDS, 1);
    en_set(P_ENERGY, 80);
    en_set(P_ECHO, 0);
    en_set(P_REVERB, 0);
    en_set(P_BREAKS, 100);
    en_play(1);
}
static void test_comes_back(void)
{
    int l;
    int32_t *a = malloc(2u * REC_LEN * sizeof(int32_t)), *b = malloc(2u * REC_LEN * sizeof(int32_t));
    uint32_t at = 0u;
    rec_a = a;
    dry_start(1006u, EN_ST_ROLLER);
    run(REC_LEN, 256u, rec_block, &at);
    for (l = 0; l <= EN_NLAYERS; l++) {             /* each, then all */
        uint32_t i, first = REC_LEN, away = 0u, k;
        rec_a = b;
        at = 0u;
        dry_start(1006u, EN_ST_ROLLER);
        run(SEC(4.0f), 256u, rec_block, &at);
        for (k = 0; k < EN_NLAYERS; k++)
            if (l == EN_NLAYERS || (int)k == l)
                en_layer((int)k, 0);
        run(SEC(14.0f), 256u, rec_block, &at);
        for (k = 0; k < EN_NLAYERS; k++)
            en_layer((int)k, 1);
        run(SEC(5.0f), 256u, rec_block, &at);       /* a bar for the drums and the bass, a second for the keys */
        k = at;
        run(REC_LEN - at, 256u, rec_block, &at);
        for (i = 0u; i < 2u * REC_LEN; i++)
            if (a[i] != b[i]) {
                if (first == REC_LEN)
                    first = i / 2u;
                if (i / 2u >= k)
                    away++;
            }
        CHECK(first < SEC(16.0f), "layer %d: off changes nothing in the audio", l);
        CHECK(!away, "layer %d: %u samples of the dry mix differ after it came back", l, away);
    }
    free(a);
    free(b);
}

/* ---- on the bar. Two runs, one with a layer asked off in the middle of a bar: identical until the bar line (in sixteen-frame blocks, so to
 * that), and the keys and melody move at once */
static int32_t *cmp_a;
typedef struct {
    uint32_t at, first;
} cmp_t;
static void cmp_block(const int32_t *b, uint32_t n, void *arg)
{
    cmp_t *c = arg;
    uint32_t i;
    for (i = 0u; i < 2u * n && c->first == 0xFFFFFFFFu; i++)
        if (cmp_a[2u * c->at + i] != b[i])
            c->first = c->at + i / 2u;
    c->at += n;
}
static uint32_t frame_of_bar;
static void test_on_the_bar(void)
{
    int l, dir;
    int32_t *a = malloc(2u * SEC(12.0f) * sizeof(int32_t));
    for (dir = 0; dir < 2; dir++)
        for (l = 0; l < EN_NLAYERS; l++) {
            uint32_t at = 0u, asked, bar_at = 0u, was;
            cmp_t c = {0u, 0xFFFFFFFFu};
            rec_a = a;
            dry_start(424242u, EN_ST_LIQUID);
            if (dir)
                en_layer(l, 0);
            run(SEC(12.0f), 16u, rec_block, &at);
            cmp_a = a;
            dry_start(424242u, EN_ST_LIQUID);
            if (dir)
                en_layer(l, 0);
            while (c.at < SEC(5.0f) || en_test_song() % 16u != 5u)
                run(16u, 16u, cmp_block, &c);
            CHECK(c.first == 0xFFFFFFFFu, "%s: a difference before it was asked", LNAME[l]);
            en_layer(l, dir);
            asked = c.at;
            was = en_test_song() / 16u;
            while (c.at < SEC(12.0f)) {
                run(16u, 16u, cmp_block, &c);
                if (!bar_at && en_test_song() / 16u > was && en_test_song() & 15u)
                    bar_at = c.at;                   /* (the bar's first step has been played) */
            }
            frame_of_bar = bar_at;
            CHECK(c.first != 0xFFFFFFFFu, "%s (%d): nothing changed", LNAME[l], dir);
            if (l == EN_L_KEYS || l == EN_L_MEL) {
                if (l == EN_L_KEYS)              /* (the melody may be between notes) */
                    CHECK(c.first < asked + SEC(0.1f), "%s (%d): waited %u frames", LNAME[l], dir, c.first - asked);
            } else {
                CHECK(c.first + 32u >= bar_at, "%s (%d): moved %d frames before the bar line", LNAME[l], dir, (int)bar_at - (int)c.first);
                if (l == EN_L_BASS)              /* (under a bass note, or a little later) */
                    CHECK(c.first < bar_at + SEC(3.0f), "%s (%d): %u frames past the bar line", LNAME[l], dir, c.first - bar_at);
                else
                    CHECK(c.first < bar_at + SEC(0.5f), "%s (%d): %u frames past the bar line", LNAME[l], dir, c.first - bar_at);
            }
        }
    free(a);
}

/* the gain itself: bar-line layers hold until the bar and pass in a few ms; the others take about a second, and fall no faster than that */
static void test_ramps(void)
{
    int l;
    for (l = 0; l < EN_NLAYERS; l++) {
        const int slow = l == EN_L_KEYS || l == EN_L_MEL;
        uint32_t n, t = 0u, steps = 0u, moved_at = 0u;
        float last = 1.0f, most = 0.0f;
        uint32_t bar;
        start(7u, EN_ST_LIQUID);
        en_play(1);
        run(SEC(3.0f), 256u, 0, 0);
        while (en_test_song() % 16u != 6u)
            run(16u, 16u, 0, 0);
        bar = en_test_song() / 16u;
        en_layer(l, 0);
        for (n = 0u; n < SEC(3.0f); n += 16u, t += 16u) {
            float g;
            run(16u, 16u, 0, 0);
            g = en_test_mute_gain(l);
            CHECK(g >= 0.0f && g <= 1.0f, "%s: a gain of %f", LNAME[l], (double)g);
            if (g != last) {
                if (!moved_at)
                    moved_at = t + 16u;
                steps++;
                if (last - g > most)
                    most = last - g;
                if (!slow)
                    CHECK(en_test_song() / 16u > bar && en_test_song() & 15u, "%s: on its way down before the bar", LNAME[l]);
            }
            CHECK(g <= last, "%s: going off it came up", LNAME[l]);
            last = g;
        }
        CHECK(last == 0.0f, "%s: not off after three seconds: %f", LNAME[l], (double)last);
        if (slow) {
            const float secs = (float)steps * 16.0f / 44100.0f;
            CHECK(moved_at < SEC(0.05f), "%s: starts late", LNAME[l]);
            CHECK(secs > 0.8f && secs < 1.25f, "%s: takes %f s", LNAME[l], (double)secs);
            CHECK(most < 0.02f, "%s: a step of %f in 16 frames", LNAME[l], (double)most);
        } else {
            const float ms = (float)steps * 16000.0f / 44100.0f;
            CHECK(steps >= 4u, "%s: a jump, in %u blocks of 16", LNAME[l], steps);
            CHECK(ms < 40.0f, "%s: takes %f ms", LNAME[l], (double)ms);
        }
        /* and back */
        en_layer(l, 1);
        for (n = 0u; n < SEC(3.0f); n += 16u)
            run(16u, 16u, 0, 0);
        CHECK(en_test_mute_gain(l) == 1.0f, "%s: not back after three seconds: %f", LNAME[l], (double)en_test_mute_gain(l));
    }
}

/* no click: what is taken away (one run less the other) moves no faster, sample to sample, than the sound itself does. A gain that jumped would
 * take a whole sample's value away at once. Drums and hats are noise and edges: their ramps are asked of the gain, above */
typedef struct {
    int32_t *other;
    uint32_t at;
    double step_a, step_d;
    int32_t pa, pd;
} click_t;
static void click_block(const int32_t *b, uint32_t n, void *arg)
{
    click_t *c = arg;
    uint32_t i;
    for (i = 0u; i < n; i++, c->at++) {
        const int32_t a = c->other[2u * c->at], d = a - b[2u * i];
        const double sa = fabs((double)a - c->pa), sd = fabs((double)d - c->pd);
        if (sa > c->step_a)
            c->step_a = sa;
        if (sd > c->step_d)
            c->step_d = sd;
        c->pa = a;
        c->pd = d;
    }
}
static void test_no_click(void)
{
    static const int L[3] = {EN_L_KEYS, EN_L_MEL, EN_L_BASS};
    int l, dir;
    int32_t *a = malloc(2u * SEC(26.0f) * sizeof(int32_t));
    for (l = 0; l < 3; l++)
        for (dir = 0; dir < 2; dir++) {
            uint32_t at = 0u, k;
            click_t c;
            memset(&c, 0, sizeof c);
            rec_a = a;
            start(1001u, EN_ST_LIQUID);
            en_arrange(EN_HANDS, 1);
            en_set(P_ENERGY, 75);
            only(L[l]);
            en_play(1);
            run(SEC(26.0f), 256u, rec_block, &at);
            c.other = a;
            start(1001u, EN_ST_LIQUID);
            en_arrange(EN_HANDS, 1);
            en_set(P_ENERGY, 75);
            only(L[l]);
            en_play(1);
            if (dir)
                en_layer(L[l], 0);
            run(SEC(20.0f), 256u, 0, 0);
            c.at = SEC(20.0f);
            c.pa = a[2u * c.at - 2u];
            c.pd = dir ? c.pa : 0;                   /* (off, what is missing is all of it) */
            en_layer(L[l], dir);
            for (k = 0u; k < SEC(6.0f); k += 64u)
                run(64u, 64u, click_block, &c);
            CHECK(c.step_a > 500.0, "%s (%d): it does not sound", LNAME[L[l]], dir);
            CHECK(c.step_d < c.step_a * 1.02 + 2000.0, "%s (%d): a click: a step of %.0f, the sound's own %.0f", LNAME[L[l]], dir, c.step_d, c.step_a);
        }
    free(a);
}

/* a kick nobody hears pulls nothing down */
static void test_duck(void)
{
    float most = 0.0f;
    uint32_t i;
    start(7u, EN_ST_LIQUID);
    en_arrange(EN_HANDS, 1);
    en_set(P_ENERGY, 80);
    en_play(1);
    for (i = 0u; i < 400u; i++) {
        run(1024u, 256u, 0, 0);
        most = fmaxf(most, en_test_duck());
    }
    CHECK(most > 0.3f, "the kick's duck: %f", (double)most);
    en_layer(EN_L_DRUMS, 0);
    bars(1u);
    most = 0.0f;
    for (i = 0u; i < 400u; i++) {
        run(1024u, 256u, 0, 0);
        most = fmaxf(most, en_test_duck());
    }
    CHECK(most == 0.0f, "the drums off, the keys still duck: %f", (double)most);
}

/* ---- Auto: what is off stays off, through every section. Rests, panics and restores do not touch it */
static void test_auto_keeps(void)
{
    uint32_t i, seen = 0u;
    int l;
    double r;
    start(7u, EN_ST_ROLLER);
    only(EN_L_BASS);
    en_play(1);
    run(1024u, 256u, 0, 0);
    en_layer(EN_L_BASS, 0);
    for (i = 0u; i < 1200u; i++) {
        run(SEC(1.0f), 256u, 0, 0);
        seen |= 1u << en_section;
        CHECK(en_mute_bass == 1u && en_mute_keys + en_mute_mel + en_mute_drums + en_mute_hats == 0u, "a section changed what is off, %u s", i);
    }
    CHECK(seen == 0x3Fu, "the sections did not all come by in 20 minutes: %02x", seen);
    r = rms_of(SEC(10.0f));
    CHECK(r < 0.001, "the bass off: %f", r);

    for (l = 0; l < EN_NLAYERS; l++)
        en_layer(l, 0);
    en_next();
    run(SEC(4.0f), 256u, 0, 0);
    en_panic();
    run(SEC(1.0f), 256u, 0, 0);
    en_restore();
    run(SEC(4.0f), 256u, 0, 0);
    for (l = 0; l < EN_NLAYERS; l++) {
        CHECK(mute_of(l), "%s back on after a panic", LNAME[l]);
        CHECK(en_test_mute_gain(l) == 0.0f, "%s's gain back after a panic", LNAME[l]);
    }
    en_init();
    for (l = 0; l < EN_NLAYERS; l++) {
        CHECK(!mute_of(l), "%s off after a start", LNAME[l]);
        CHECK(en_test_mute_gain(l) == 1.0f, "%s's gain after a start", LNAME[l]);
    }
    CHECK(en_mode == EN_AUTO, "a start is in Auto until told");
}

/* resting there is no bar to wait for */
static void test_resting(void)
{
    int l;
    start(7u, EN_ST_LIQUID);
    en_play(1);
    run(SEC(5.0f), 256u, 0, 0);
    en_play(0);
    run(SEC(1.0f), 256u, 0, 0);
    for (l = EN_L_BASS; l <= EN_L_HATS; l++) {
        const uint32_t song = en_test_song();
        en_layer(l, 0);
        run(SEC(0.1f), 256u, 0, 0);
        CHECK(en_test_mute_gain(l) == 0.0f, "%s, resting: waits", LNAME[l]);
        CHECK(en_test_song() == song, "resting, the grid moved");
    }
    en_layers_on();
    run(SEC(0.1f), 256u, 0, 0);
    for (l = 0; l < EN_NLAYERS; l++)
        CHECK(!mute_of(l), "%s, all on: still off", LNAME[l]);
    CHECK(en_test_mute_gain(EN_L_DRUMS) == 1.0f, "all on, resting: the drums");
    /* all on, growing: the drums and the bass on the bar, like any other */
    en_play(1);
    run(SEC(1.0f), 256u, 0, 0);
    en_layer(EN_L_DRUMS, 0);
    bars(1u);
    run(2048u, 256u, 0, 0);
    CHECK(en_test_mute_gain(EN_L_DRUMS) == 0.0f, "growing: the drums off on the bar");
    while (en_test_song() % 16u != 4u)
        run(16u, 16u, 0, 0);
    en_layers_on();
    run(SEC(0.05f), 256u, 0, 0);
    CHECK(en_test_mute_gain(EN_L_DRUMS) == 0.0f, "all on, growing: the drums do not wait for the bar");
    bars(1u);
    run(2048u, 256u, 0, 0);
    CHECK(en_test_mute_gain(EN_L_DRUMS) == 1.0f, "all on: the drums not back after the bar");
}

/* ---- Hands: nothing moves by itself, for as long as you like */
static void test_hands_stays(void)
{
    int st, as_saved;
    for (st = 0; st < EN_NSTYLES; st++)
        for (as_saved = 0; as_saved < 2; as_saved++) {
            const uint32_t HOURS = 16u * 8u * 2000u;    /* 16000 bars */
            uint32_t i, sec, lev;
            start(1001u + (uint32_t)st, st);
            en_play(1);
            run(SEC(40.0f), 256u, 0, 0);
            en_arrange(EN_HANDS, as_saved);
            run(SEC(3.0f), 256u, 0, 0);
            CHECK(en_mode == EN_HANDS, "the mode");
            sec = en_section;
            lev = en_drum_level;
            for (i = 0u; i < HOURS / 256u; i++) {
                en_test_ticks(256u);
                CHECK(en_section == sec, "style %d: a section came by itself, at %u", st, en_test_song());
                CHECK(en_drum_level == lev, "style %d: the drums moved by themselves, at %u", st, en_test_song());
                CHECK(en_test_section_bar() < en_test_section_bars(), "style %d: past its section: %u of %u", st, en_test_section_bar(), en_test_section_bars());
            }
        }
    /* and Auto, as a control, does move */
    {
        uint32_t i, seen = 0u;
        start(7u, EN_ST_LIQUID);
        en_play(1);
        run(1024u, 256u, 0, 0);
        for (i = 0u; i < 400u; i++) {
            en_test_ticks(256u);
            seen |= 1u << en_section;
        }
        CHECK(seen == 0x3Fu, "Auto does not go round: %02x", seen);
    }
}

/* Energy across its range: the drums at each place, as heard (MIDI), over several seeds */
static uint32_t count_at(uint32_t seed, int style, int energy, uint32_t *kick, uint32_t *snare, uint32_t *hat)
{
    start(seed, style);
    en_arrange(EN_HANDS, 1);
    en_set(P_ENERGY, energy);
    en_set(P_BREAKS, 0);
    en_set(P_EVOLVE, 50);
    en_play(1);
    run(1024u, 256u, 0, 0);
    bars(4u);
    clear_counts();
    bars(32u);
    *kick += drum_on[36];
    *snare += drum_on[38] + drum_on[37] + drum_on[45];
    *hat += drum_on[42] + drum_on[46];
    return midi_n;
}
static void test_energy_range(void)
{
    static const int E[] = {0, 5, 20, 45, 70, 100};
    static const uint32_t SEEDS[4] = {7u, 58u, 1001u, 424242u};
    int st, i, s;
    for (st = 0; st < EN_NSTYLES; st++) {
        uint32_t sum[6];
        for (i = 0; i < 6; i++) {
            uint32_t k = 0u, n = 0u, h = 0u;
            for (s = 0; s < 4; s++)
                count_at(SEEDS[s], st, E[i], &k, &n, &h);
            sum[i] = k + n + h;
            if (i == 0)
                CHECK(sum[i] == 0u, "style %d, Energy 0: %u drums", st, sum[i]);
            if (i == 1) {
                CHECK(h > 0u, "style %d, low Energy: no hats", st);
                CHECK(k + n == 0u, "style %d, low Energy: %u kicks and snares", st, k + n);
            }
            if (i >= 2)
                CHECK(k > 0u && n > 0u, "style %d, Energy %d: kicks %u, snares %u", st, E[i], k, n);
            if (i)
                CHECK(sum[i] >= sum[i - 1], "style %d: Energy %d has fewer drums (%u) than Energy %d (%u)", st, E[i], sum[i], E[i - 1], sum[i - 1]);
        }
        if (st != EN_ST_DRIFT)
            CHECK(sum[5] > sum[2], "style %d: Energy does not raise the density: %u, %u", st, sum[5], sum[2]);
    }
    /* the level on the bar, whatever bar the knob moves in */
    for (i = 0; i < 6; i++) {
        uint32_t b;
        start(7u, EN_ST_LIQUID);
        en_arrange(EN_HANDS, 1);
        en_set(P_ENERGY, E[i]);
        en_play(1);
        run(1024u, 256u, 0, 0);
        bars(2u);
        while (en_test_song() % 16u != 7u)
            run(16u, 16u, 0, 0);
        {
            const int was = en_drum_level, now = E[(i + 3) % 6] == E[i] ? E[0] : E[(i + 3) % 6];
            en_part_t p;
            en_set(P_ENERGY, now);
            en_hands_part(EN_ST_LIQUID, now, &p);
            b = en_test_song() / 16u;
            while (en_test_song() / 16u == b || !(en_test_song() & 15u)) {
                CHECK(en_drum_level == was, "Energy %d: the level moved before the bar", now);
                run(16u, 16u, 0, 0);
            }
            CHECK(en_drum_level == p.level, "Energy %d: level %d after the bar, wanted %d", now, en_drum_level, p.level);
        }
    }
}

/* the harmony's and the melody's side: the chord's character at each place */
static int last_char, hat_char;
static void trace_c(uint32_t step, const en_gchord_t *g, int character)
{
    (void)step, (void)g;
    last_char = character;
    hat_char |= 1 << character;
}
static void test_character(void)
{
    static const int E[4] = {0, 20, 45, 90}, WANT[4] = {0, 1, 2, 3};
    int i;
    en_trace_chord = trace_c;
    for (i = 0; i < 4; i++) {
        start(58u, EN_ST_LIQUID);
        en_arrange(EN_HANDS, 1);
        en_set(P_ENERGY, E[i]);
        en_play(1);
        hat_char = 0;
        run(SEC(40.0f), 256u, 0, 0);
        CHECK(hat_char == 1 << WANT[i], "Energy %d: the chords' characters %x", E[i], (unsigned)hat_char);
    }
    en_trace_chord = 0;
}

/* Auto to Hands: all as it is, until Energy is turned. Hands to Auto: on from there */
static void test_switching(void)
{
    uint32_t i;
    int sec, lev;
    start(7u, EN_ST_LIQUID);
    en_set(P_ENERGY, 90);
    en_play(1);
    /* into the full section, by way of EDIT */
    while (en_section != EN_SEC_FULL) {
        en_next();
        bars(1u);
    }
    bars(4u);
    sec = en_section;
    lev = en_drum_level;
    CHECK(lev == 3, "the full section at Energy 90: level %d", lev);
    en_arrange(EN_HANDS, 0);
    for (i = 0u; i < 400u; i++) {
        bars(1u);
        CHECK(en_section == sec && en_drum_level == lev, "the switch did not hold: section %d level %d at %u", en_section, en_drum_level, i);
    }
    /* a level kept does not fall with an Energy unmoved: the same value told again is no turn */
    en_set(P_ENERGY, 90);
    bars(2u);
    CHECK(en_section == sec && en_drum_level == lev, "the same Energy again took over");
    /* Energy turned: it is the drums now */
    en_set(P_ENERGY, 20);
    bars(2u);
    CHECK(en_drum_level == 4, "Energy 20 after the hold: level %d", en_drum_level);
    CHECK(en_section == EN_SEC_DOWN, "Energy 20: section %d", en_section);
    /* back to Auto: it is that section, at the phase that has that level, and from there it goes on */
    en_arrange(EN_AUTO, 0);
    run(1024u, 256u, 0, 0);
    CHECK(en_mode == EN_AUTO, "the mode");
    bars(1u);
    CHECK(en_section == EN_SEC_DOWN && en_drum_level == 4, "back in Auto: section %d, level %d", en_section, en_drum_level);
    CHECK(en_test_section_bar() < en_test_section_bars(), "back in Auto: bar %u of %u", en_test_section_bar(), en_test_section_bars());
    {
        int went = 0;
        const uint32_t pos = en_test_song() / 16u % 8u;
        (void)pos;
        for (i = 0u; i < 400u && !went; i++) {
            bars(1u);
            went = en_section != EN_SEC_DOWN;
        }
        CHECK(went, "back in Auto: the section never ends");
        CHECK(en_section == EN_SEC_FULL || en_section == EN_SEC_OUT, "back in Auto: from the breakdown to section %d", en_section);
        CHECK(en_test_song() % (16u * 8u) < 32u, "back in Auto: a section starts away from a phrase: %u", en_test_song());
    }
    /* every place, every style, back to Auto: it goes all the way round */
    {
        int st, e;
        static const int E[6] = {0, 5, 20, 45, 70, 90};
        for (st = 0; st < EN_NSTYLES; st++)
            for (e = 0; e < 6; e++) {
                uint32_t seen = 0u;
                start(11u, st);
                en_set(P_ENERGY, 100);
                en_arrange(EN_HANDS, 1);
                en_set(P_ENERGY, E[e]);
                en_play(1);
                run(1024u, 256u, 0, 0);
                bars(3u);
                en_set(P_ENERGY, 100);
                en_arrange(EN_AUTO, 0);
                run(1024u, 256u, 0, 0);
                for (i = 0u; i < 2000u; i++) {
                    en_test_ticks(16u * 8u);
                    CHECK(en_test_section_bar() < en_test_section_bars() || en_test_section_bar() == 0u, "style %d: past its section", st);
                    seen |= 1u << en_section;
                }
                CHECK((seen & 0x2Fu) == 0x2Fu, "style %d, from Energy %d: only %02x came by", st, E[e], seen);
            }
    }
}

/* EDIT in Hands: the next section, and it stays, Energy still its ceiling; turn Energy and Energy is the drums again */
static void test_edit(void)
{
    int k;
    start(7u, EN_ST_LIQUID);
    en_arrange(EN_HANDS, 1);
    en_set(P_ENERGY, 100);
    en_play(1);
    run(1024u, 256u, 0, 0);
    bars(2u);
    CHECK(en_section == EN_SEC_FULL && en_drum_level == 3, "Energy 100: section %d, level %d", en_section, en_drum_level);
    for (k = 0; k < 8; k++) {
        const int want = (EN_SEC_FULL + 1 + k) % EN_NSECTIONS;
        uint32_t i;
        en_next();
        bars(1u);
        CHECK(en_section == want, "EDIT %d: section %d, wanted %d", k, en_section, want);
        CHECK(en_test_song() % (16u * 8u) < 16u + 1u, "EDIT: a section starts a phrase: %u", en_test_song());
        for (i = 0u; i < 300u; i++) {
            en_test_ticks(16u);
            CHECK(en_section == want, "EDIT %d: section %d by itself after %u bars", k, en_section, i);
        }
        CHECK(en_test_section_bar() < en_test_section_bars(), "EDIT %d: past its end", k);
    }
    /* the light, as written: its first phrase without a bass, then with it, and there it stays */
    while (en_section != EN_SEC_LIGHT) {
        en_next();
        bars(1u);
    }
    bars(1u);
    {
        uint32_t i;
        for (i = 0u; i < 300u; i++)
            en_test_ticks(16u);
        CHECK(en_drum_level == 2 && en_section == EN_SEC_LIGHT, "the light stays: level %d", en_drum_level);
        CHECK(en_test_section_bar() + 8u >= en_test_section_bars(), "the light stays on its last phrase: %u of %u", en_test_section_bar(), en_test_section_bars());
    }
    /* the last touch: Energy */
    en_set(P_ENERGY, 75);
    bars(2u);
    CHECK(en_section == EN_SEC_FULL && en_drum_level == 3, "Energy after EDIT: section %d, level %d", en_section, en_drum_level);
    /* Energy is the ceiling while a section stands */
    en_next();
    bars(1u);
    en_next();
    bars(1u);
    en_next();
    bars(1u);
    CHECK(en_section == EN_SEC_HATS, "section %d", en_section);
    en_next();
    bars(1u);
    CHECK(en_section == EN_SEC_LIGHT && en_drum_level == 2, "section %d, level %d", en_section, en_drum_level);
}

/* a break asked for, in Hands: it comes, ends with its phrase, and the place is as it was */
static void test_break(void)
{
    int k;
    start(7u, EN_ST_JUNGLE);
    en_arrange(EN_HANDS, 1);
    en_set(P_ENERGY, 75);
    en_set(P_BREAKS, 0);
    en_play(1);
    run(1024u, 256u, 0, 0);
    bars(2u);
    for (k = 0; k < 6; k++) {
        int got = 0;
        uint32_t i;
        en_break();
        run(256u, 256u, 0, 0);
        for (i = 0u; i < 16u; i++) {
            bars(1u);
            if (en_break_bars)
                got++;
            CHECK(en_section == EN_SEC_FULL && en_drum_level == 3, "a break moved the place: section %d level %d", en_section, en_drum_level);
        }
        CHECK(got > 0, "the break %d did not come", k);
        CHECK(!en_break_bars, "the break %d did not end", k);
    }
}

/* ---- the ledger: every combination, both modes, Energy moving, EDIT and breaks, and rests; and MIDI out off at the end of it, all let go */
static void test_ledger(void)
{
    int m, c, l;
    ledger = 1;
    for (m = 0; m < 2; m++)
        for (c = 0; c < 1 << EN_NLAYERS; c++) {
            start(1001u + (uint32_t)c, c % EN_NSTYLES);
            en_arrange(m, 1);
            en_play(1);
            run(SEC(1.0f), 256u, 0, 0);
            for (l = 0; l < EN_NLAYERS; l++)
                en_layer(l, !(c >> l & 1));
            run(SEC(6.0f), 256u, 0, 0);
            en_set(P_ENERGY, 90);
            en_next();
            run(SEC(6.0f), 256u, 0, 0);
            en_break();
            en_set(P_ENERGY, 20);
            run(SEC(6.0f), 256u, 0, 0);
            en_play(0);
            run(SEC(1.0f), 256u, 0, 0);
            CHECK(nd[2] == 0, "resting: a bass note down, combination %d", c);
            en_play(1);
            run(SEC(4.0f), 256u, 0, 0);
            for (l = 0; l < EN_NLAYERS; l++)
                en_layer(l, c >> l & 1);
            run(SEC(4.0f), 256u, 0, 0);
            en_set(P_MIDI, 0);
            ledger = 0;                              /* (from here the engine plays on and says nothing) */
            run(1024u, 256u, 0, 0);
            ledger = 1;
            CHECK(nd[0] + nd[1] + nd[2] + nd[3] + nd[9] == 0, "MIDI off, combination %d mode %d: notes hanging", c, m);
        }
    ledger = 0;
    CHECK(!led_bad, "the ledger: %u bad blocks", led_bad);
}

/* ---- a long, careless performance: nothing goes wrong */
static void test_careless(void)
{
    uint32_t i;
    start(99u, EN_ST_ROLLER);
    en_play(1);
    ledger = 1;
    for (i = 0u; i < 1800u; i++) {
        const uint32_t r = rnd16();
        switch (r % 12u) {
        case 0: case 1: case 2: en_layer((int)(r >> 4) % EN_NLAYERS, (int)(r >> 8 & 1u)); break;
        case 3: en_arrange((int)(r >> 4 & 1u), (int)(r >> 5 & 1u)); break;
        case 4: en_set(P_ENERGY, (int)(r >> 4) % 101); break;
        case 5: en_next(); break;
        case 6: en_break(); break;
        case 7: en_set(P_STYLE, (int)(r >> 4) % EN_NSTYLES); break;
        case 8: en_hold((int)(r >> 4 & 1u)); break;
        case 9: en_play((int)(r >> 4 & 3u) != 0); break;
        case 10: en_layers_on(); break;
        default: break;
        }
        run(SEC(1.0f), 256u, 0, 0);
        CHECK(en_test_section_bar() < en_test_section_bars() + 8u, "at %u: bar %u of %u", i, en_test_section_bar(), en_test_section_bars());
    }
    ledger = 0;
    CHECK(en_test_nans == 0u, "%u NaNs", en_test_nans);
    CHECK(en_test_peak_in < 4.0f, "a peak of %f", (double)en_test_peak_in);
}

int main(void)
{
    test_hands_part();
    test_silent();
    test_like_level_0();
    test_same_notes();
    test_comes_back();
    test_on_the_bar();
    test_ramps();
    test_no_click();
    test_duck();
    test_auto_keeps();
    test_resting();
    test_hands_stays();
    test_energy_range();
    test_character();
    test_switching();
    test_edit();
    test_break();
    test_ledger();
    test_careless();
    (void)frame_of_bar;
    printf(fails ? "hands_test: %d FAILED\n" : "hands_test: ok\n", fails);
    return fails != 0;
}
