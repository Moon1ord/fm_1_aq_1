/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal */
/* The knobs, one at a time and all at once: each does what its page says at both ends of its range, the first piece at its
 * defaults is still the sound that was approved, nothing at any extreme gives a NaN or runs into full scale, a level at 0 is
 * silence. Audio where the claim is about sound, MIDI and counters where it is about notes. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "amb.h"

static int fails;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); fails++; } } while (0)

/* ---- the MIDI that comes out */
#define NOTES_MAX 40000
static struct {
    uint32_t n[4];
    uint8_t note[4][NOTES_MAX];
} out;
static uint32_t midi_all;
void en_midi_out(uint32_t st, uint32_t d1, uint32_t d2)
{
    (void)d2;
    if ((st & 0xF0u) == 0x90u && (st & 15u) < 4u && out.n[st & 15u] < NOTES_MAX)
        out.note[st & 15u][out.n[st & 15u]++] = (uint8_t)d1;
    if ((st & 0xF0u) == 0x90u)
        midi_all++;
}

/* ---- rendering, measured */
typedef struct {
    double e, hf;                                /* hf: what is over about 1.5 kHz, two poles, of the left */
    float peak;
    uint64_t frames;
    uint32_t hash;
} meas_t;
static double hp1[2], lp1[2];
#define GAIN 2430u
static void render(float secs, meas_t *m)
{
    static int32_t b[512];
    uint32_t left = (uint32_t)(secs * 44100.0f);
    memset(m, 0, sizeof *m);
    m->hash = 2166136261u;
    while (left) {
        const uint32_t k = left > 256u ? 256u : left;
        uint32_t i;
        en_render(b, k, GAIN);
        for (i = 0; i < 2u * k; i += 2u) {
            const double l = (double)b[i] / 8388608.0, r = (double)b[i + 1] / 8388608.0;
            double dl;
            const float a = (float)(l < 0.0 ? -l : l);
            lp1[0] += 0.192 * (l - lp1[0]);
            hp1[0] = l - lp1[0];
            lp1[1] += 0.192 * (hp1[0] - lp1[1]);
            dl = hp1[0] - lp1[1];
            m->e += l * l + r * r;
            m->hf += dl * dl;
            m->peak = a > m->peak ? a : m->peak;
            m->hash = (m->hash ^ (uint32_t)b[i]) * 16777619u;
            m->hash = (m->hash ^ (uint32_t)b[i + 1]) * 16777619u;
        }
        m->frames += k;
        left -= k;
    }
}
static double rms(const meas_t *m) { return m->frames ? __builtin_sqrt(m->e / (2.0 * (double)m->frames)) : 0.0; }
static double hf_ratio(const meas_t *m) { return m->e > 0.0 ? m->hf / m->e : 0.0; }

static void start(uint32_t seed)
{
    static int32_t b[512];
    en_trace = 0;
    en_trace_chord = 0;
    en_test_pad_off = 0;
    en_init();
    memset(&out, 0, sizeof out);
    midi_all = 0u;
    en_seed(seed);
    en_render(b, 16, GAIN);
}
static void go(void)
{
    static int32_t b[512];
    en_play(1);
    en_render(b, 16, GAIN);
}
static void only(int layer)                      /* one layer, dry. -1: none, the room left up */
{
    static const int L[4] = {P_KEYS, P_MOTIFS, P_BASS, P_DRUMS};
    int i;
    for (i = 0; i < 4; i++)
        if (L[i] != layer)
            en_set(L[i], 0);
    if (layer >= 0) {
        en_set(P_ECHO, 0);
        en_set(P_REVERB, 0);
    }
}
static void to_level(int level)                  /* on to the drums' level, not a sample of sound on the way */
{
    int i;
    for (i = 0; i < 4000 && en_drum_level != (uint8_t)level; i++)
        en_test_ticks(16u);
    CHECK(en_drum_level == (uint8_t)level, "level %d never came", level);
}
static void sync_ticks(uint32_t bars)            /* so many bars, no sound */
{
    en_test_ticks(bars * 16u);
}

/* ---- the table: what the pages are, what each knob's name says */
static void t_table(void)
{
    static const struct {
        int p;
        int lo, hi, def;
    } T[] = {
        {P_TEMPO, 150, 180, 170}, {P_ENERGY, 0, 100, 70}, {P_EVOLVE, 0, 100, 50}, {P_STYLE, 0, EN_NSTYLES - 1, 0},
        {P_KEYS, 0, 100, 60}, {P_BLOOM, 0, 100, 50}, {P_BRIGHT, 0, 100, 50}, {P_NOTES, 0, 3, 3},
        {P_MOTIFS, 0, 100, 60}, {P_MRATE, 0, 100, 50}, {P_MTONE, 0, 100, 50}, {P_MOCT, -2, 2, 0},
        {P_BASS, 0, 100, 50}, {P_BMOVE, 0, 100, 50}, {P_GLIDE, 0, 100, 10}, {P_BASSOCT, -1, 1, 0},
        {P_DRUMS, 0, 100, 45}, {P_BREAKS, 0, 100, 50}, {P_HATS, 0, 100, 50}, {P_KIT, 0, 1, 0},
        {P_ECHO, 0, 100, 40}, {P_ETIME, 0, 5, 3}, {P_REVERB, 0, 100, 65}, {P_SIZE, 0, 100, 80},
        {P_ROOT, 0, 11, 2}, {P_TUNE, -50, 50, 0}, {P_MIDI, 0, 1, 1}, {P_MODE, 0, EN_NMODES - 1, 3},
    };
    static const char *const STYLES[EN_NSTYLES] = {"Liquid", "Roller", "Jungle", "Half", "Drift", "Loading"};
    const int n = (int)(sizeof T / sizeof T[0]);
    int i;
    char t[12];
    CHECK(n == P_NPARAMS, "%d parameters, the table has %d", P_NPARAMS, n);
    for (i = 0; i < n; i++) {
        const en_param_t *p = en_param_info(T[i].p);
        CHECK(p->lo == T[i].lo && p->hi == T[i].hi && p->def == T[i].def, "%s: %d %d %d", p->name, p->lo, p->hi, p->def);
        en_param_text(T[i].p, p->lo, t);
        CHECK(t[0], "%s has no text at its low end", p->name);
        en_param_text(T[i].p, p->hi, t);
        CHECK(t[0], "%s has no text at its high end", p->name);
    }
    for (i = 0; i < EN_NSTYLES; i++) {
        en_param_text(P_STYLE, i, t);
        CHECK(!strcmp(t, STYLES[i]), "style %d says %s", i, t);
    }
    en_param_text(P_KIT, 0, t);
    CHECK(!strcmp(t, "Glass"), "kit 0: %s", t);
    en_param_text(P_KIT, 1, t);
    CHECK(!strcmp(t, "Moss"), "kit 1: %s", t);
    en_param_text(P_NOTES, 2, t);
    CHECK(!strcmp(t, "2"), "notes 2: %s", t);
    en_param_text(P_MOCT, 1, t);
    CHECK(!strcmp(t, "+1"), "octave +1: %s", t);
    en_param_text(P_MOCT, -2, t);
    CHECK(!strcmp(t, "-2"), "octave -2: %s", t);
}

/* ---- the sound that was approved: these hashes are of the render with the melody as it is (0772c0a's until then, the same but for
 * the melody, the hand's top under it, Jungle's numbers). If one moves, the default sound has moved: only ever on purpose, and say
 * so. They hold for an IEEE machine at -ffp-contract=off */
static void t_golden(void)
{
    static const struct {
        int style;
        uint32_t seed, hash;
    } G[] = {{0, 1001u, 0x2d472da5u}, {1, 7u, 0x84fcc055u}, {2, 424242u, 0x39c5619bu}, {3, 5u, 0x9a05dd4bu}, {4, 6u, 0x983fee21u}};
    meas_t m;
    unsigned i;
    for (i = 0; i < sizeof G / sizeof G[0]; i++) {
        start(G[i].seed);
        en_set(P_STYLE, G[i].style);
        go();
        render(180.0f, &m);
        CHECK(m.hash == G[i].hash, "style %d seed %u: %08x, it was %08x: the default sound has moved", G[i].style, G[i].seed, m.hash, G[i].hash);
    }
}

/* ---- level 0: silence */
static void t_levels(void)
{
    static const int L[4] = {P_KEYS, P_MOTIFS, P_BASS, P_DRUMS};
    static const char *const NAME[4] = {"keys", "motifs", "bass", "drums"};
    int l;
    meas_t m, h;
    start(21u);
    only(-1);
    go();
    render(40.0f, &m);
    sync_ticks(200u);
    render(20.0f, &h);
    CHECK(m.peak == 0.0f && h.peak == 0.0f, "all four levels at 0, the room up: peak %g %g", (double)m.peak, (double)h.peak);
    for (l = 0; l < 4; l++) {
        double low, high;
        start(22u);
        only(L[l]);
        go();
        to_level(l == 3 || l == 2 ? 3 : 2);
        en_set(L[l], 30);
        en_set(P_MRATE, 100);
        render(30.0f, &m);
        low = rms(&m);
        CHECK(low > 1e-4, "%s alone at 30 is silent (%g)", NAME[l], low);
        en_set(L[l], 100);
        render(30.0f, &m);
        high = rms(&m);
        CHECK(high > 1.5 * low, "%s at 100 no louder than at 30: %g %g", NAME[l], high, low);
        en_set(L[l], 0);
        render(2.0f, &m);
        render(5.0f, &m);
        CHECK(m.peak == 0.0f, "%s at 0 still sounds: %g", NAME[l], (double)m.peak);
    }
}

/* ---- the keys */
static void onset(int bloom, double *early, double *late, double *brightness)
{
    meas_t a, b;
    start(31u);
    only(P_KEYS);
    en_set(P_BLOOM, bloom);
    en_test_pad_off = 1;
    go();
    render(0.12f, &a);
    render(1.3f, &b);
    *early = a.e / 0.12;
    *late = b.e / 1.3;
    *brightness = hf_ratio(&b);
}
static void t_bloom(void)
{
    double e0, l0, e5, l5, e1, l1, b0, b5, b1;
    onset(0, &e0, &l0, &b0);
    onset(50, &e5, &l5, &b5);
    onset(100, &e1, &l1, &b1);
    printf("  bloom: first 120 ms against the next 1.3 s: %.3f %.3f %.3f\n", e0 / l0, e5 / l5, e1 / l1);
    CHECK(e0 / l0 > 1.5 * (e5 / l5), "Bloom 0 is not quicker than 50: %g %g", e0 / l0, e5 / l5);
    CHECK(e5 / l5 > 3.0 * (e1 / l1), "Bloom 100 does not bloom: %g %g", e5 / l5, e1 / l1);
    CHECK(l1 > 0.02 * l5, "Bloom 100 is silent for over a second: %g %g", l1, l5);
}
static void t_bright(void)
{
    static const int V[3] = {0, 50, 100};
    double r[3];
    int i;
    for (i = 0; i < 3; i++) {
        meas_t m;
        start(32u);
        only(P_KEYS);
        en_set(P_BRIGHT, V[i]);
        en_test_pad_off = 1;
        go();
        render(0.2f, &m);
        render(2.0f, &m);
        r[i] = hf_ratio(&m);
    }
    printf("  bright: high against all: %.3g %.3g %.3g\n", r[0], r[1], r[2]);
    CHECK(r[0] < 0.95 * r[1] && r[1] < 0.6 * r[2], "Bright does not brighten: %g %g %g", r[0], r[1], r[2]);
}
static uint32_t notes_most, notes_at;
static void notes_chord(uint32_t step, const en_gchord_t *g, int character)
{
    const uint32_t d = en_ep_singles - notes_at;
    (void)step, (void)g, (void)character;
    notes_most = d > notes_most ? d : notes_most;
    notes_at = en_ep_singles;
}
static void t_notes(void)
{
    uint32_t got[4];
    int v;
    for (v = 0; v <= 3; v++) {
        start(33u);
        en_set(P_NOTES, v);
        go();
        notes_most = 0u;
        notes_at = en_ep_singles;
        en_trace_chord = notes_chord;
        sync_ticks(3000u);
        got[v] = en_ep_singles;
        CHECK(notes_most <= (uint32_t)v, "Notes %d: a chord had %u", v, notes_most);
    }
    printf("  notes: lone notes in 3000 bars at 0..3: %u %u %u %u\n", got[0], got[1], got[2], got[3]);
    CHECK(got[0] == 0u, "Notes 0 played %u", got[0]);
    CHECK(got[1] > 100u && got[2] > got[1] && got[3] > got[2], "Notes 1, 2, 3: %u %u %u", got[1], got[2], got[3]);
    /* at 0 a key's own note is not one either, and does not wait for later */
    start(34u);
    en_set(P_NOTES, 0);
    go();
    sync_ticks(8u);
    en_pool(EN_MODE_POOL[MODE_LYDIAN] | 1u << 1);
    {
        meas_t m;
        render(0.05f, &m);
    }
    sync_ticks(40u);
    en_set(P_NOTES, 3);
    {
        meas_t m;
        const uint32_t before = en_ep_singles;
        render(0.05f, &m);
        en_test_ticks(1u);
        CHECK(en_ep_singles == before, "a key's note kept waiting through Notes 0");
    }
}

/* ---- the motifs */
static uint32_t motif_notes(int rate, uint32_t seed)
{
    start(seed);
    en_set(P_MRATE, rate);
    go();
    sync_ticks(3000u);
    return out.n[1];
}
static void t_rate(void)
{
    uint32_t c[3] = {0u, 0u, 0u}, sd;
    static const int V[3] = {0, 50, 100};
    int i;
    for (sd = 0u; sd < 6u; sd++)
        for (i = 0; i < 3; i++)
            c[i] += motif_notes(V[i], 41u + sd * 17u);
    printf("  rate: motif notes in 18000 bars at 0, 50, 100: %u %u %u (%.2f, %.2f of 50's)\n", c[0], c[1], c[2], (double)c[0] / c[1], (double)c[2] / c[1]);
    CHECK(c[0] > 20u, "Rate 0 is no motifs at all: %u", c[0]);
    CHECK(c[0] * 3u < c[1], "Rate 0 is not rare: %u of %u", c[0], c[1]);
    /* (a phrase takes its own time, and the rests are what Rate scales: more than half as much again, not more than twice) */
    CHECK(c[2] * 10u >= c[1] * 14u && c[2] * 10u <= c[1] * 24u, "Rate 100 is not up to about twice: %u of %u", c[2], c[1]);
}
static double motif_bright(int tone)
{
    /* over several pieces and minutes, so several timbres of Tone's part of the palette are heard */
    double e = 0.0, hf = 0.0;
    uint32_t sd;
    for (sd = 0u; sd < 3u; sd++) {
        meas_t m;
        start(44u + sd);
        only(P_MOTIFS);
        en_set(P_MTONE, tone);
        en_set(P_MRATE, 100);
        go();
        render(240.0f, &m);
        CHECK(m.peak > 0.005f, "the motifs are silent at Tone %d (%g)", tone, (double)m.peak);
        e += m.e;
        hf += m.hf;
    }
    return e > 0.0 ? hf / e : 0.0;
}
static void t_tone(void)
{
    const double a = motif_bright(0), b = motif_bright(50), c = motif_bright(100);
    int t, k;
    printf("  tone: high against all: %.3g %.3g %.3g\n", a, b, c);
    CHECK(a < b * 0.99 && b < c * 0.7, "Tone does not open: %g %g %g", a, b, c);
    for (t = 0; t <= 100; t++)
        for (k = 0; k < EN_NTIMBRES; k++) {
            en_mel_voice_t v;
            en_mel_voice(k, t, &v);
            CHECK(v.ratio == (float)(int)v.ratio && v.ratio >= 1.0f && v.ratio <= 3.0f, "Tone %d: an inharmonic ratio %f", t, (double)v.ratio);
            CHECK(v.index > 0.0f && v.index + v.floor_idx < 2.4f, "Tone %d: index %f and %f (radians): out of the warm range", t, (double)v.index, (double)v.floor_idx);
            CHECK(v.idec_s > 0.02f && v.idec_s < 0.5f, "Tone %d: the index's decay %f", t, (double)v.idec_s);
        }
}
static void shifted(int p, int by, int ch, int want)
{
    static uint8_t base[3][NOTES_MAX];
    static uint32_t n[3];
    int c;
    uint32_t i;
    start(45u);
    en_set(P_MRATE, 100);
    go();
    sync_ticks(600u);
    for (c = 0; c < 3; c++) {
        n[c] = out.n[c];
        memcpy(base[c], out.note[c], sizeof base[c]);
    }
    start(45u);
    en_set(P_MRATE, 100);
    en_set(p, by);
    go();
    sync_ticks(600u);
    for (c = 0; c < 3; c++) {
        CHECK(out.n[c] == n[c] && n[c] > 0u, "par %d at %d: channel %d has %u notes, %u before", p, by, c + 1, out.n[c], n[c]);
        for (i = 0u; i < n[c] && i < out.n[c]; i++) {
            const int d = c == ch ? want : 0;
            if ((int)out.note[c][i] != (int)base[c][i] + d && !(p == P_BASSOCT && by < 0 && out.note[c][i] == base[c][i])) {
                CHECK(0, "par %d at %d: channel %d's note %u is %d, it was %d", p, by, c + 1, i, out.note[c][i], base[c][i]);
                break;
            }
        }
    }
}
static void t_octaves(void)
{
    uint32_t i;
    shifted(P_MOCT, 2, 1, 24);
    shifted(P_MOCT, -2, 1, -24);
    shifted(P_MOCT, 1, 1, 12);
    shifted(P_BASSOCT, 1, 2, 12);
    shifted(P_BASSOCT, -1, 2, -12);
    /* the lowest bass octave stays a note: nothing under C1 */
    for (i = 0u; i < out.n[2]; i++)
        CHECK(out.note[2][i] >= 24, "bass note %d", out.note[2][i]);
}

/* ---- the bass */
static void t_move(void)
{
    int st;
    for (st = 0; st < EN_NSTYLES; st++) {
        uint32_t n[3];
        static const int V[3] = {0, 50, 100};
        int i;
        for (i = 0; i < 3; i++) {
            start(51u + (uint32_t)st);
            en_set(P_STYLE, st);
            en_set(P_BMOVE, V[i]);
            go();
            sync_ticks(3000u);
            n[i] = en_bass_notes;
        }
        printf("  move: %s, bass notes in 3000 bars at 0, 50, 100: %u %u %u\n", EN_STYLE_NAME[st], n[0], n[1], n[2]);
        CHECK(n[0] <= n[1] && n[1] < n[2], "%s: Move does not move: %u %u %u", EN_STYLE_NAME[st], n[0], n[1], n[2]);
        CHECK(n[2] >= n[0] * 3u / 2u, "%s: Move's range is small: %u %u", EN_STYLE_NAME[st], n[0], n[2]);
    }
    {   /* the pure part: both ends and the middle */
        static const int SETS[4] = {EN_BASS_LONG, EN_BASS_LIGHT, EN_BASS_FULL, EN_BASS_ROLL};
        int s;
        uint32_t h;
        for (s = 0; s < 4; s++)
            for (h = 0u; h < 500u; h++) {
                const uint32_t x = h * 2654435761u;
                CHECK(en_bass_move(SETS[s], 0, x) == EN_BASS_LONG, "Move 0 is not the long roots");
                CHECK(en_bass_move(SETS[s], 100, x) == EN_BASS_ROLL, "Move 100 is not the rolling figures");
                CHECK(en_bass_move(SETS[s], 50, x) == SETS[s], "Move 50 is not the style's own");
            }
    }
}
static uint32_t glide_samples(int v)
{
    /* the same note change each time: how long until the pitch has got there */
    static int32_t b[64];
    uint32_t i, worst = 0u, seen = 0u;
    start(61u);
    only(P_BASS);
    en_set(P_GLIDE, v);
    go();
    to_level(3);
    for (i = 0u; i < 44100u * 40u / 32u && seen < 40u; i++) {
        const float t0 = en_test_bass_target();
        en_render(b, 32, GAIN);
        if (en_test_bass_target() != t0 && t0 > 0.0f) {
            /* a new note, legato: count to within a twentieth of the way */
            const float from = en_test_bass_pitch(), to = en_test_bass_target();
            const float gap = from > to ? from - to : to - from;
            uint32_t k = 32u;
            while (k < 44100u) {
                float now, d;
                if (en_test_bass_target() != to)
                    break;
                now = en_test_bass_pitch();
                d = now > to ? now - to : to - now;
                if (d < 0.05f * gap)
                    break;
                en_render(b, 32, GAIN);
                k += 32u;
            }
            if (gap > 0.0005f) {
                worst = k > worst ? k : worst;
                seen++;
            }
        }
    }
    CHECK(seen >= 10u, "Glide %d: only %u note changes seen", v, seen);
    return worst;
}
static void t_glide(void)
{
    const uint32_t a = glide_samples(0), b = glide_samples(10), c = glide_samples(100);
    printf("  glide: longest slide, ms, at 0, 10, 100: %.0f %.0f %.0f\n", (double)a / 44.1, (double)b / 44.1, (double)c / 44.1);
    CHECK(a < b && b * 4u < c, "Glide does not glide: %u %u %u", a, b, c);
    CHECK(a < 44100u / 50u, "Glide 0 is not a jump: %u", a);
    CHECK(c > 44100u / 3u, "Glide 100 is short: %u", c);
}

/* ---- the drums */
static void t_hats(void)
{
    meas_t m;
    static const int V[3] = {0, 50, 100};
    double r[3];
    uint32_t hits[3];
    int i;
    for (i = 0; i < 3; i++) {
        start(71u);
        only(P_DRUMS);
        en_set(P_HATS, V[i]);
        go();
        to_level(1);
        render(1.0f, &m);
        render(8.0f, &m);
        r[i] = rms(&m);
        hits[i] = en_hat_hits;
    }
    printf("  hats: rms at 0, 50, 100: %.4g %.4g %.4g, hits %u %u %u\n", r[0], r[1], r[2], hits[0], hits[1], hits[2]);
    CHECK(r[0] == 0.0, "Hats 0 still sounds: %g", r[0]);
    CHECK(r[1] > 1e-4 && r[2] > 1.5 * r[1], "Hats is not the level: %g %g", r[1], r[2]);
    CHECK(hits[0] == hits[1] && hits[1] == hits[2] && hits[1] > 10u, "Hats changed how many: %u %u %u", hits[0], hits[1], hits[2]);
}
static void t_breaks(void)
{
    static const int V[3] = {0, 50, 100};
    uint32_t c[3];
    int i, b;
    for (i = 0; i < 3; i++) {
        uint32_t bars = 0u;
        c[i] = 0u;
        start(72u);
        en_set(P_STYLE, EN_ST_JUNGLE);
        en_set(P_BREAKS, V[i]);
        go();
        for (b = 0; b < 12000; b++) {
            sync_ticks(1u);
            bars += en_break_bars != 0u;
        }
        c[i] = bars;
    }
    printf("  breaks: bars of break in 12000, at 0, 50, 100: %u %u %u\n", c[0], c[1], c[2]);
    CHECK(c[0] == 0u, "Breaks 0: %u bars of break", c[0]);
    CHECK(c[1] > 0u && c[2] > c[1], "Breaks: %u %u", c[1], c[2]);
}
static void t_kit(void)
{
    meas_t a, b;
    uint32_t i;
    for (i = 0u; i < 2u; i++) {
        start(73u);
        only(P_DRUMS);
        en_set(P_KIT, (int)i);
        en_set(P_HATS, 0);
        go();
        to_level(3);
        render(8.0f, i ? &b : &a);
    }
    printf("  kit: rms %.4g %.4g, high against all %.4g %.4g\n", rms(&a), rms(&b), hf_ratio(&a), hf_ratio(&b));
    CHECK(a.hash != b.hash, "the kits sound the same");
    CHECK(rms(&a) > 1e-3 && rms(&b) > 1e-3, "a kit is silent: %g %g", rms(&a), rms(&b));
    CHECK(hf_ratio(&a) > 1.1 * hf_ratio(&b) || hf_ratio(&b) > 1.1 * hf_ratio(&a) || rms(&a) > 1.1 * rms(&b) || rms(&b) > 1.1 * rms(&a),
          "the kits are nearly the same: %g %g, %g %g", rms(&a), rms(&b), hf_ratio(&a), hf_ratio(&b));
}

/* ---- the space */
static void wet(int p, int v, double *dry_e, double *tail_e)
{
    meas_t m;
    start(81u);
    only(P_KEYS);
    en_set(P_ECHO, 40);
    en_set(P_REVERB, 65);
    en_set(p, v);
    go();
    render(10.0f, &m);
    *dry_e = m.e;
    en_play(0);
    en_set(P_KEYS, 0);                           /* (a rest keeps the hand and the pad: here they stop feeding the room) */
    render(14.0f, &m);
    render(6.0f, &m);
    *tail_e = m.e;
}
static void t_space(void)
{
    double d0, t0, d1, t1;
    wet(P_ECHO, 0, &d0, &t0);
    wet(P_ECHO, 100, &d1, &t1);
    CHECK(d1 > 1.02 * d0, "Echo does nothing: %g %g", d0, d1);
    wet(P_REVERB, 0, &d0, &t0);
    wet(P_REVERB, 100, &d1, &t1);
    CHECK(d1 > 1.05 * d0, "Reverb does nothing: %g %g", d0, d1);
    wet(P_SIZE, 0, &d0, &t0);
    wet(P_SIZE, 100, &d1, &t1);
    printf("  size: tail at 0, 100: %.3g %.3g\n", t0, t1);
    CHECK(t1 > 1.5 * t0, "Size does nothing to the tail: %g %g", t0, t1);
    {
        meas_t a, b;
        start(82u);
        only(P_KEYS);
        en_set(P_ECHO, 80);
        en_set(P_ETIME, 1);
        go();
        render(8.0f, &a);
        start(82u);
        only(P_KEYS);
        en_set(P_ECHO, 80);
        en_set(P_ETIME, 5);
        go();
        render(8.0f, &b);
        CHECK(a.hash != b.hash, "Echo time does nothing");
    }
}

/* ---- Home and Setup */
static uint32_t chords_seen[64], chords_n;
static void chord_list(uint32_t step, const en_gchord_t *g, int character)
{
    (void)step, (void)character;
    if (chords_n < 64u)
        chords_seen[chords_n++] = (uint32_t)g->ch.root << 8 | g->shape | (uint32_t)g->ch.tonic << 16;
}
static void t_home(void)
{
    static int32_t b[512];
    uint32_t s150, s180;
    meas_t m;
    uint32_t a[64], i, same = 0u;
    start(91u);
    en_set(P_TEMPO, 150);
    go();
    s150 = en_bars;
    render(10.0f, &m);
    s150 = en_bars - s150;
    start(91u);
    en_set(P_TEMPO, 180);
    go();
    s180 = en_bars;
    render(10.0f, &m);
    s180 = en_bars - s180;
    CHECK(s150 >= 99u && s150 <= 101u && s180 >= 119u && s180 <= 121u, "tempo: %u, %u sixteenths in 10 s", s150, s180);
    (void)b;
    /* Energy */
    {
        uint32_t h0, h1;
        start(92u);
        en_set(P_ENERGY, 0);
        go();
        sync_ticks(3000u);
        render(0.1f, &m);
        h0 = en_hits;
        start(92u);
        en_set(P_ENERGY, 100);
        go();
        sync_ticks(3000u);
        render(0.1f, &m);
        h1 = en_hits;
        CHECK(h0 == 0u && h1 > 100u, "Energy: %u hits at 0, %u at 100", h0, h1);
    }
    /* Evolve: 0 the phrase again and again, 100 not */
    {
        int e;
        for (e = 0; e < 2; e++) {
            chords_n = 0u;
            start(93u);
            en_set(P_EVOLVE, e * 100);
            go();
            en_trace_chord = chord_list;
            sync_ticks(600u);
            if (!e)
                memcpy(a, chords_seen, sizeof a);
        }
        for (i = 0u; i < 64u; i++)
            same += a[i] == chords_seen[i];
        CHECK(same < 56u, "Evolve: the same chords at 0 and at 100 (%u of 64)", same);
    }
    /* Style */
    {
        uint32_t k[2];
        int st;
        for (st = 0; st < 2; st++) {
            start(94u);
            en_set(P_STYLE, st ? EN_ST_DRIFT : EN_ST_LIQUID);
            go();
            sync_ticks(3000u);
            render(0.1f, &m);
            k[st] = en_hits;
        }
        CHECK(k[1] < k[0], "Style: Drift's hits %u, Liquid's %u", k[1], k[0]);
    }
}
static void t_setup(void)
{
    meas_t a, b;
    start(95u);
    en_set(P_ROOT, 0);
    go();
    chords_n = 0u;
    en_trace_chord = chord_list;
    sync_ticks(64u);
    CHECK(chords_n > 0u && chords_seen[chords_n - 1u] >> 16 == 0u, "Root 0: the chord's key is %u", chords_seen[chords_n - 1u] >> 16);
    start(95u);
    en_set(P_ROOT, 11);
    go();
    chords_n = 0u;
    en_trace_chord = chord_list;
    sync_ticks(64u);
    CHECK(chords_n > 0u && chords_seen[chords_n - 1u] >> 16 == 11u, "Root 11: the chord's key is %u", chords_seen[chords_n - 1u] >> 16);
    start(96u);
    en_set(P_TUNE, -50);
    go();
    render(4.0f, &a);
    start(96u);
    en_set(P_TUNE, 50);
    go();
    render(4.0f, &b);
    CHECK(a.hash != b.hash, "Tune does nothing");
    start(97u);
    en_set(P_MIDI, 0);
    go();
    render(0.1f, &a);
    sync_ticks(300u);
    CHECK(midi_all == 0u, "MIDI out off: %u notes", midi_all);
    start(97u);
    go();
    sync_ticks(300u);
    CHECK(midi_all > 100u, "MIDI out on: %u notes", midi_all);
}

/* ---- every extreme: no NaN, under full scale, and how hard the limiter is asked to work */
static float loudest, loud_in;
static int loud_p, loud_v;
static void extreme_run(const char *what, int p, int v)
{
    meas_t m;
    int i;
    for (i = 0; i < 1200 && en_drum_level != 3u; i++)   /* as far up as this much energy and this style go */
        en_test_ticks(16u);
    render(4.0f, &m);                            /* (ticks strike notes with no time to fade) */
    en_test_peak_in = 0.0f;
    CHECK(en_test_nans == 0u, "%s: NaN before the render", what);
    render(14.0f, &m);
    CHECK(en_test_nans == 0u, "%s: NaN in the render (%d = %d)", what, p, v);
    CHECK(m.peak < 0.995f, "%s: peak %f (%d = %d)", what, (double)m.peak, p, v);
    if (getenv("KNOB_LOUD"))
        printf("    %s = %d: %.2f into the limiter, %.3f out, rms %.4f\n", what, v, (double)en_test_peak_in, (double)m.peak, rms(&m));
    if (en_test_peak_in > loud_in) {
        loud_in = en_test_peak_in;
        loudest = m.peak;
        loud_p = p;
        loud_v = v;
    }
}
static void t_extremes(void)
{
    int p, side, r;
    uint32_t x = 12345u;
    for (p = 0; p < P_NPARAMS; p++)
        for (side = 0; side < 2; side++) {
            const int v = side ? en_param_info(p)->hi : en_param_info(p)->lo;
            start(101u);
            en_set(p, v);
            go();
            extreme_run(en_param_info(p)->name, p, v);
        }
    printf("  one knob at an end: hottest %s = %d: %.2f into the limiter, %.3f out\n", en_param_info(loud_p)->name, loud_v, (double)loud_in, (double)loudest);
    {
        const float one = loud_in;
        CHECK(one < 2.0f, "a single knob at its end pushes %.2f into the limiter", (double)one);
    }
    loud_in = 0.0f;
    for (side = 0; side < 2; side++) {               /* all down, all up, then corners */
        start(102u);
        for (p = 0; p < P_NPARAMS; p++)
            if (p != P_MIDI && p != P_ENERGY)
                en_set(p, side ? en_param_info(p)->hi : en_param_info(p)->lo);
        en_set(P_ENERGY, 100);
        go();
        extreme_run("all at one end", -1, side);
    }
    for (r = 0; r < 24; r++) {
        start(200u + (uint32_t)r);
        for (p = 0; p < P_NPARAMS; p++) {
            x = x * 1664525u + 1013904223u;
            if (p != P_ENERGY && p != P_MIDI)
                en_set(p, x >> 24 & 1u ? en_param_info(p)->hi : en_param_info(p)->lo);
        }
        en_set(P_ENERGY, 100);
        go();
        extreme_run("a corner", -2, r);
    }
    printf("  all at an end and 24 corners: hottest %.2f into the limiter, %.3f out\n", (double)loud_in, (double)loudest);
}

int main(int argc, char **argv)
{
    const char *only_one = argc > 1 ? argv[1] : 0;
#define RUN(f) do { if (!only_one || !strcmp(only_one, #f)) { printf("%s\n", #f); f(); } } while (0)
    RUN(t_table);
    RUN(t_golden);
    RUN(t_levels);
    RUN(t_bloom);
    RUN(t_bright);
    RUN(t_notes);
    RUN(t_rate);
    RUN(t_tone);
    RUN(t_octaves);
    RUN(t_move);
    RUN(t_glide);
    RUN(t_hats);
    RUN(t_breaks);
    RUN(t_kit);
    RUN(t_space);
    RUN(t_home);
    RUN(t_setup);
    RUN(t_extremes);
    printf(fails ? "knob: %d FAILED\n" : "knob: ok\n", fails);
    return fails != 0;
}
