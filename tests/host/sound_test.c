/* SPDX-License-Identifier: MPL-2.0 */
/* The sounds a layer can have (P_KEYSND, P_PADSND, P_BASSSND, P_MELSND). Nothing else may change with them: the first of each is the sound as it was, to the bit; MIDI is the same whatever
 * is chosen and whenever; every sound is bounded at every knob's end, sits within a stated range of its layer's first sound, and is measurably itself; and a change in mid-phrase, however
 * quick, neither clicks nor leaves a note down. What the panel does is tests/scenarios/sounds.amb. The owner's limits are asked of the measurements: only whole harmonics (no bell), the
 * bass held for its whole length, a chord that rises rather than strikes */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "amb.h"

static int fails;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); fails++; } } while (0)
#define GAIN 2430u
#define BLOCK 256u
#define SECS(s) ((uint32_t)((float)(s) * 44100.0f / (float)BLOCK))
#define FS 44100.0f

/* the bounds. A switch must not jump: at most this far from the layer's first sound, in dB of RMS over a whole phrase (keys and pad and bass held; the melody's own timbres, by their first half second) */
#define LEVEL_DB 1.5f
#define MELODY_DB 5.0f                           /* the six timbres were set by ear, as they are heard in turn: not moved here, and this is how far apart they are */
#define MEASURE_DB 3.0f                          /* two sounds of a layer differ by at least this over the harmonics: a table can't make two names of one sound */

/* ---- MIDI: all of it hashed, in order and by the block it went out in; and what is down */
static uint32_t midi_hash, midi_n, midi_block;
static int down[16][128], nd[16];
static uint32_t melody_ons;
void en_midi_out(uint32_t st, uint32_t d1, uint32_t d2)
{
    const int c = (int)(st & 15u), k = (int)(st & 0xF0u);
    midi_hash = (midi_hash ^ (st << 16 | d1 << 8 | d2)) * 16777619u;
    midi_hash = (midi_hash ^ midi_block) * 16777619u;
    midi_n++;
    if (st >= 0xF0u)
        return;
    if (k == 0x90 && d2) {
        melody_ons += c == 1;
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

static int32_t blk[2 * BLOCK];
static uint32_t audio_hash;
static void render(uint32_t blocks)
{
    uint32_t b, j;
    for (b = 0; b < blocks; b++) {
        midi_block++;
        en_render(blk, BLOCK, GAIN);
        for (j = 0; j < 2u * BLOCK; j++)
            audio_hash = (audio_hash ^ (uint32_t)blk[j]) * 16777619u;
    }
}
static void clear_counts(void)
{
    midi_hash = audio_hash = 2166136261u;
    midi_n = midi_block = melody_ons = 0u;
    memset(down, 0, sizeof down);
    memset(nd, 0, sizeof nd);
}
static void start_engine(uint32_t seed)
{
    en_init();
    en_seed(seed);
    clear_counts();
    en_test_pad_off = 0;
    en_test_keys_gain = 1.0f;
}
static void start_playing(uint32_t seed, int hands)
{
    int l;
    start_engine(seed);
    en_arrange(hands);
    for (l = 0; l < EN_NLAYERS; l++)
        en_layer(l, 1);
    en_restore();
}

static const struct {
    int par, count;
    const char *name;
} LAYER[4] = {{P_KEYSND, 4, "keys"}, {P_PADSND, 4, "pad"}, {P_BASSSND, 4, "bass"}, {P_MELSND, 7, "melody"}};

/* ---- the table */
static void t_table(void)
{
    int l, v, i;
    char a[12], b[12];
    for (l = 0; l < 4; l++) {
        const en_param_t *pi = en_param_info(LAYER[l].par);
        CHECK(pi->lo == 0 && pi->def == 0, "%s: from 0, and the first by default", LAYER[l].name);
        CHECK(pi->hi >= 2 && pi->hi + 1 <= LAYER[l].count, "%s: %d sounds, 3 at least", LAYER[l].name, pi->hi + 1);
        for (v = 0; v <= pi->hi; v++) {
            en_param_text(LAYER[l].par, v, a);
            CHECK(strlen(a) >= 3 && strlen(a) <= 9, "%s %d: a name that fits: \"%s\"", LAYER[l].name, v, a);
            for (i = 0; i < v; i++) {
                en_param_text(LAYER[l].par, i, b);
                CHECK(strcmp(a, b), "%s: \"%s\" twice", LAYER[l].name, a);
            }
        }
    }
    en_param_text(P_MELSND, 0, a);
    CHECK(!strcmp(a, "Auto"), "the melody's first is Auto: \"%s\"", a);
    for (v = 0; v < EN_NTIMBRES; v++) {
        en_param_text(P_MELSND, v + 1, a);
        CHECK(!strcmp(a, EN_TIMBRE_NAME[v]), "the melody's %d: \"%s\"", v + 1, a);
    }
    CHECK(en_param_info(P_MELSND)->hi == EN_NTIMBRES, "the melody has Auto and every timbre");
    for (i = 0; i < EN_NPIECES; i++) {           /* the recipes' sounds are the first: a choice of the owner's to change */
        int16_t par[P_NPARAMS];
        uint16_t pool;
        en_piece(i, par, &pool);
        for (l = 0; l < 4; l++)
            CHECK(par[LAYER[l].par] == 0, "%s: %s's sound is the first", en_piece_name(i), LAYER[l].name);
    }
    CHECK(P_KEYSND == P_MODE + 1 && P_MELSND == P_MODE + 4, "the sounds come after the old parameters: a scenario's numbers stay");
    CHECK(P_MELMODE == P_MELSND + 1 && P_ARPPAT == P_MELSND + 2 && P_NPARAMS == P_MODE + 7, "the melody's mode and pattern after the sounds: a scenario's numbers stay");
    CHECK(P_NPARAMS + 6 <= EN_ROOM_MAX, "a piece's commands fit the queue: %d in %d", P_NPARAMS + 6, (int)EN_ROOM_MAX);
    CHECK(EN_ROOM_MAX == 56u, "the queue as it was");
}

/* ---- the first sound of each is the sound as it was: a render of the commit before the sounds, to the bit (the whole proof, on nine scenarios, is in the report of the change: this keeps it) */
#define OLD_AUDIO 0x27e1795du
#define OLD_MIDI 0x50eca8beu
#define OLD_N 14927u
static void t_first_is_as_it_was(void)
{
    start_engine(1234u);
    en_arrange(EN_AUTO);
    en_restore();
    render(SECS(180));
    printf("  first sounds, 180 s of Auto, seed 1234: audio %08x midi %08x, %u messages\n", audio_hash, midi_hash, midi_n);
    CHECK(audio_hash == OLD_AUDIO, "the audio: %08x, not %08x", audio_hash, OLD_AUDIO);
    CHECK(midi_hash == OLD_MIDI && midi_n == OLD_N, "the MIDI: %08x and %u, not %08x and %u", midi_hash, midi_n, OLD_MIDI, OLD_N);
}

/* ---- MIDI, whatever is chosen */
static void set_keen(void)                       /* what brings out the most of the melody and the keys */
{
    en_set(P_MRATE, 100);
    en_set(P_NOTES, 3);
    en_set(P_ENERGY, 80);
}
static void midi_of(int hands, int par, int v, int tone, uint32_t *hash, uint32_t *n)
{
    start_playing(77u, hands);
    set_keen();
    en_set(P_MTONE, tone);
    if (par >= 0)
        en_set(par, v);
    render(SECS(200));
    *hash = midi_hash;
    *n = midi_n;
}
static void t_midi_unchanged(void)
{
    int hands, l, v, t;
    uint32_t base, n0, h, n;
    static const int TONE[2] = {50, 100};
    uint32_t before = en_test_bell_case(0), before2 = en_test_bell_case(1);
    for (hands = 0; hands < 2; hands++)
        for (t = 0; t < 2; t++) {
            midi_of(hands, -1, 0, TONE[t], &base, &n0);
            CHECK(n0 > 1000u, "there is MIDI to compare: %u", n0);
            CHECK(melody_ons > 8u, "a melody to compare: %u notes", melody_ons);
            for (l = 0; l < 4; l++)
                for (v = 1; v <= en_param_info(LAYER[l].par)->hi; v++) {
                    midi_of(hands, LAYER[l].par, v, TONE[t], &h, &n);
                    CHECK(h == base && n == n0, "%s %d, Hands %d, Tone %d: the MIDI differs: %u messages, not %u", LAYER[l].name, v, hands, TONE[t], n, n0);
                }
        }
    /* both ways round have happened: a chosen timbre that dies before the one it stands for, and one that outlives it, or this has not been asked */
    CHECK(en_test_bell_case(0) > before, "a sound over before its note's MIDI: never happened");
    CHECK(en_test_bell_case(1) > before2, "a note's MIDI over before its sound: never happened");
    /* the same notes while the sound changes under them, every few blocks */
    midi_of(0, -1, 0, 50, &base, &n0);
    start_playing(77u, 0);
    set_keen();
    en_set(P_MTONE, 50);
    {
        uint32_t s, k = 0u;
        for (s = 0; s < SECS(200) / 40u; s++) {
            render(40u);
            for (l = 0; l < 4; l++)
                en_set(LAYER[l].par, (int)((k * (unsigned)(l + 3)) % (unsigned)(en_param_info(LAYER[l].par)->hi + 1)));
            en_mel_hear();
            k++;
        }
        render(SECS(200) - s * 40u);
    }
    CHECK(midi_hash == base && midi_n == n0, "changing all the time, the MIDI differs: %u messages, not %u", midi_n, n0);
}

/* ---- the melody's choice: the phrases are as ever, the voice is what was asked */
static uint32_t phrases_hash;
static void on_phrase(uint32_t step, const en_phrase_t *p)
{
    int i;
    phrases_hash = (phrases_hash ^ step) * 16777619u;
    phrases_hash = (phrases_hash ^ p->timbre) * 16777619u;
    for (i = 0; i < p->n; i++)
        phrases_hash = (phrases_hash ^ (uint32_t)(p->note[i].note << 8 | p->note[i].vel)) * 16777619u;
}
static void t_melody_fixed(void)
{
    int v;
    uint32_t base = 0u;
    en_trace_mel = on_phrase;
    for (v = 0; v <= EN_NTIMBRES; v++) {
        uint32_t b, right = 0u, wrong = 0u;
        phrases_hash = 2166136261u;
        start_playing(99u, 0);
        en_set(P_MRATE, 100);
        en_set(P_MELSND, v);
        en_test_bell_struck = -1;
        for (b = 0; b < SECS(180); b++) {
            int was = en_test_bell_struck;
            en_test_bell_struck = -1;
            render(1u);
            if (en_test_bell_struck >= 0 && v) {
                if (en_test_bell_struck == v - 1)
                    right++;
                else
                    wrong++;
            }
            (void)was;
        }
        if (!v)
            base = phrases_hash;
        else {
            CHECK(right > 8u, "%s: %u notes in it", EN_TIMBRE_NAME[v - 1], right);
            CHECK(!wrong, "%s: %u notes in another timbre", EN_TIMBRE_NAME[v - 1], wrong);
        }
        CHECK(phrases_hash == base, "with %d the phrases are not those of Auto", v);
    }
    en_trace_mel = 0;
}

/* ---- every knob's end: no NaN, and bounded. The bound: what the first sounds do at the same place, and a little. The limiter's knee is at 1 */
static float run_extreme(const int (*set)[2], int nset, int keys, int padv, int bass, int mel)
{
    int i;
    start_playing(5u, 0);
    for (i = 0; i < nset; i++)
        en_set(set[i][0], set[i][1]);
    en_set(P_KEYSND, keys);
    en_set(P_PADSND, padv);
    en_set(P_BASSSND, bass);
    en_set(P_MELSND, mel);
    render(SECS(40));
    return en_test_peak_in;
}
static void t_extremes(void)
{
    static const int ENDS[][4][2] = {
        {{P_KEYS, 100}, {P_BRIGHT, 100}, {P_BLOOM, 0}, {P_BASS, 100}},
        {{P_KEYS, 100}, {P_BRIGHT, 0}, {P_BLOOM, 100}, {P_MOTIFS, 100}},
        {{P_MOTIFS, 100}, {P_MTONE, 100}, {P_MOCT, 2}, {P_MRATE, 100}},
        {{P_MOTIFS, 100}, {P_MTONE, 0}, {P_MOCT, -2}, {P_MRATE, 100}},
        {{P_BASS, 100}, {P_GLIDE, 100}, {P_BASSOCT, 1}, {P_BMOVE, 100}},
        {{P_BASS, 100}, {P_GLIDE, 0}, {P_BASSOCT, -1}, {P_BMOVE, 0}},
    };
    const int nends = (int)(sizeof ENDS / sizeof ENDS[0]);
    int e, l, v;
    float worst_ratio = 0.0f;
    for (e = 0; e < nends; e++) {
        const float base = run_extreme(ENDS[e], 4, 0, 0, 0, 0);
        CHECK(!en_test_nans, "the first sounds at end %d: %u NaNs", e, en_test_nans);
        for (l = 0; l < 4; l++)
            for (v = 1; v <= en_param_info(LAYER[l].par)->hi; v++) {
                const float p = run_extreme(ENDS[e], 4, l == 0 ? v : 0, l == 1 ? v : 0, l == 2 ? v : 0, l == 3 ? v : 0);
                const float r = p / (base > 0.05f ? base : 0.05f);
                CHECK(!en_test_nans, "%s %d at end %d: %u NaNs", LAYER[l].name, v, e, en_test_nans);
                CHECK(p < 2.5f, "%s %d at end %d: %.2f into the limiter", LAYER[l].name, v, e, (double)p);
                CHECK(r < 1.5f, "%s %d at end %d: %.2f into the limiter, %.2f the first sound's", LAYER[l].name, v, e, (double)p, (double)base);
                if (r > worst_ratio)
                    worst_ratio = r;
            }
    }
    printf("  every knob's end: into the limiter at most %.2f times what the first sounds put in at the same place\n", (double)worst_ratio);
}

/* ---- the sound by itself, to measure */
#define NFFT 8192
static float cap[2 * 44100 * 12];
static uint32_t ncap;
static void capture(uint32_t blocks)
{
    uint32_t b, j;
    for (b = 0; b < blocks; b++) {
        midi_block++;
        en_render(blk, BLOCK, GAIN);
        for (j = 0; j < 2u * BLOCK && ncap < sizeof cap / sizeof cap[0]; j++)
            cap[ncap++] = (float)blk[j] * (1.0f / 8300000.0f);
    }
}
static float rms_of(uint32_t from, uint32_t n)   /* frames, both channels */
{
    double s = 0.0;
    uint32_t j;
    for (j = from * 2u; j < (from + n) * 2u && j < ncap; j++)
        s += (double)cap[j] * (double)cap[j];
    return (float)sqrt(s / (double)(2u * n));
}
static float db(float x) { return 20.0f * log10f(x > 1e-7f ? x : 1e-7f); }

/* a harmonic's level over a stretch, by a Goertzel under a Hann window */
static float tone_at(uint32_t from, uint32_t n, float hz)
{
    const double w = 2.0 * M_PI * (double)hz / (double)FS, c = 2.0 * cos(w);
    double s0, s1 = 0.0, s2 = 0.0;
    uint32_t j;
    for (j = 0; j < n; j++) {
        const double h = 0.5 - 0.5 * cos(2.0 * M_PI * (double)j / (double)n);
        const double x = (double)cap[(from + j) * 2u] + (double)cap[(from + j) * 2u + 1u];
        s0 = x * h + c * s1 - s2;
        s2 = s1;
        s1 = s0;
    }
    return (float)(sqrt(s1 * s1 + s2 * s2 - c * s1 * s2) * 4.0 / (double)n);
}
static float note_hz(int n) { return 440.0f * powf(2.0f, (float)(n - 69) / 12.0f); }
#define NHARM 10
typedef struct { float h[NHARM], rms, inharm, centroid; } print_t;
static void fft(float *re, float *im, int n);
/* what lies away from every harmonic, in dB under all of it, by a spectrum under a Hann window: a bell, or anything not a whole ratio, would have it */
static float away_from_harmonics(uint32_t from, int note)
{
    static float re[32768], im[32768];
    const int N = 32768;
    const float f0 = note_hz(note), bin = FS / (float)N;
    double away = 0.0, all = 0.0;
    int i;
    for (i = 0; i < N; i++) {
        const float h = 0.5f - 0.5f * cosf(2.0f * (float)M_PI * (float)i / (float)N);
        re[i] = (cap[(from + (uint32_t)i) * 2u] + cap[(from + (uint32_t)i) * 2u + 1u]) * h;
        im[i] = 0.0f;
    }
    fft(re, im, N);
    for (i = 4; i < N / 2; i++) {
        const float hz = (float)i * bin, nh = roundf(hz / f0), dist = fabsf(hz - nh * f0) / bin;
        const double p = (double)re[i] * (double)re[i] + (double)im[i] * (double)im[i];
        all += p;
        if (dist > 6.0f)
            away += p;
    }
    return 10.0f * log10f((float)(away / (all > 0.0 ? all : 1.0)) + 1e-12f);
}
/* the harmonics in dB under the strongest, over n frames; and what lies away from them */
static void print_of(print_t *p, uint32_t from, uint32_t n, int note)
{
    const float f0 = note_hz(note);
    float top = 1e-9f, cn = 0.0f, cd = 0.0f;
    int k;
    for (k = 0; k < NHARM; k++) {
        const float a = tone_at(from, n, f0 * (float)(k + 1));
        p->h[k] = a;
        top = a > top ? a : top;
        cn += a * a * f0 * (float)(k + 1);
        cd += a * a;
    }
    for (k = 0; k < NHARM; k++)
        p->h[k] = db(p->h[k] / top);
    p->inharm = away_from_harmonics(from, note);
    p->rms = rms_of(from, n);
    p->centroid = cn / (cd > 0.0f ? cd : 1.0f);
}
static float print_gap(const print_t *a, const print_t *b)
{
    float s = 0.0f;
    int k;
    for (k = 0; k < NHARM; k++) {
        const float x = a->h[k] < -50.0f ? -50.0f : a->h[k], y = b->h[k] < -50.0f ? -50.0f : b->h[k];
        s += (x - y) * (x - y);
    }
    return sqrtf(s / NHARM);
}
static void print_show(const char *layer, int v, const print_t *p, const char *when)
{
    char nm[12];
    const int par = !strcmp(layer, "keys") ? P_KEYSND : !strcmp(layer, "bass") ? P_BASSSND : P_PADSND;
    int k;
    en_param_text(par, v, nm);
    printf("  %-6s %-9s %-6s rms %6.1f dB, centroid %5.0f Hz, away %6.1f dB, H:", layer, nm, when, (double)db(p->rms), (double)p->centroid, (double)p->inharm);
    for (k = 0; k < 6; k++)
        printf(" %5.1f", (double)p->h[k]);
    printf("\n");
}
static void quiet_room(void)
{
    en_set(P_ECHO, 0);
    en_set(P_REVERB, 0);
}

/* ---- keys: a note alone. Whole harmonics only, no bell; the level; how it rises: it may not strike */
#define KEY_NOTE 60
static void key_note(int v, print_t *early, print_t *late, float *rise_ms)
{
    uint32_t j, peak_at = 0u;
    float peak = 0.0f;
    start_engine(3u);
    en_set(P_KEYS, 30);
    en_set(P_KEYSND, v);
    quiet_room();
    render(2u);
    en_test_key_note(KEY_NOTE, 0.9f);
    ncap = 0u;
    capture(SECS(4));
    print_of(early, (uint32_t)(0.15f * FS), 8192u, KEY_NOTE);
    print_of(late, (uint32_t)(2.5f * FS), 8192u, KEY_NOTE);
    for (j = 0; j + 256u < ncap / 2u; j += 64u) {
        const float r = rms_of(j, 256u);
        if (r > peak) {
            peak = r;
            peak_at = j;
        }
    }
    *rise_ms = (float)peak_at / FS * 1000.0f;
}
static void t_keys_measured(void)
{
    print_t e[4], l[4];
    float rise[4];
    int a, b;
    const int n = en_param_info(P_KEYSND)->hi + 1;
    for (a = 0; a < n; a++) {
        key_note(a, &e[a], &l[a], &rise[a]);
        print_show("keys", a, &e[a], "early");
        print_show("keys", a, &l[a], "late");
        printf("         its loudest %.0f ms in\n", (double)rise[a]);
        CHECK(e[a].inharm < -50.0f && l[a].inharm < -50.0f, "keys %d: %.1f, %.1f dB away from the harmonics: a bell", a, (double)e[a].inharm, (double)l[a].inharm);
        CHECK(rise[a] > 60.0f, "keys %d: at its loudest %.0f ms in: it strikes", a, (double)rise[a]);
    }
    for (a = 0; a < n; a++)
        for (b = 0; b < a; b++)
            CHECK(print_gap(&e[a], &e[b]) > MEASURE_DB || print_gap(&l[a], &l[b]) > MEASURE_DB, "keys %d and %d: %.1f and %.1f dB apart", a, b, (double)print_gap(&e[a], &e[b]), (double)print_gap(&l[a], &l[b]));
}

/* ---- bass: a note alone, for a long time. Long: at the end of its length it is where it was. Whole harmonics. Its level */
#define BASS_NOTE 38
static void bass_alone(int v, print_t *p, float *hold)
{
    start_engine(3u);
    en_set(P_BASS, 50);
    en_set(P_BASSSND, v);
    quiet_room();
    render(2u);
    en_test_bass_note(BASS_NOTE, 256u);
    ncap = 0u;
    capture(SECS(4));
    print_of(p, (uint32_t)(0.5f * FS), 16384u, BASS_NOTE);
    *hold = rms_of((uint32_t)(3.2f * FS), 8192u) / rms_of((uint32_t)(0.3f * FS), 8192u);
}
static void t_bass_measured(void)
{
    print_t p[4];
    float hold[4];
    int a, b;
    const int n = en_param_info(P_BASSSND)->hi + 1;
    for (a = 0; a < n; a++) {
        bass_alone(a, &p[a], &hold[a]);
        print_show("bass", a, &p[a], "");
        printf("         held: %.2f of its level after 3 s\n", (double)hold[a]);
        CHECK(p[a].inharm < -50.0f, "bass %d: %.1f dB away from the harmonics", a, (double)p[a].inharm);
        CHECK(hold[a] > 0.8f, "bass %d: %.2f left after 3 s: not long", a, (double)hold[a]);
        CHECK(p[a].h[0] > -1.0f, "bass %d: the fundamental is not its strongest: %.1f", a, (double)p[a].h[0]);
        CHECK(fabsf(db(p[a].rms / p[0].rms)) < LEVEL_DB, "bass %d: %.1f dB from the first", a, (double)db(p[a].rms / p[0].rms));
    }
    for (a = 0; a < n; a++)
        for (b = 0; b < a; b++)
            CHECK(print_gap(&p[a], &p[b]) > MEASURE_DB, "bass %d and %d: %.1f dB apart", a, b, (double)print_gap(&p[a], &p[b]));
}

/* ---- a spectrum, for what has many notes */
static void fft(float *re, float *im, int n)
{
    int i, j = 0, m, k;
    for (i = 1; i < n; i++) {
        int bit = n >> 1;
        for (; j & bit; bit >>= 1)
            j ^= bit;
        j ^= bit;
        if (i < j) {
            float t = re[i]; re[i] = re[j]; re[j] = t;
            t = im[i]; im[i] = im[j]; im[j] = t;
        }
    }
    for (m = 2; m <= n; m <<= 1) {
        const double a = -2.0 * M_PI / (double)m;
        for (i = 0; i < n; i += m)
            for (k = 0; k < m / 2; k++) {
                const float wr = (float)cos(a * k), wi = (float)sin(a * k);
                const int u = i + k, v = i + k + m / 2;
                const float tr = re[v] * wr - im[v] * wi, ti = re[v] * wi + im[v] * wr;
                re[v] = re[u] - tr; im[v] = im[u] - ti;
                re[u] += tr; im[u] += ti;
            }
    }
}
typedef struct { float rms, centroid, bands[4], corr, flux; } band_t;
/* over a long stretch: a spectrum by frames: its centroid; the energy under 300, to 1000, to 3000, over, in dB of the whole; left against right; how much the level moves from frame to frame (detune beats) */
static void bands_of(band_t *o, uint32_t from, uint32_t frames)
{
    static float re[NFFT], im[NFFT];
    double pw[4] = {0, 0, 0, 0}, cn = 0, cd = 0, lr = 0, ll = 0, rr = 0, mv = 0, last = -1.0;
    uint32_t s, nseg = (frames - NFFT) / (NFFT / 2);
    int i, b;
    for (s = 0; s < nseg; s++) {
        const uint32_t at = from + s * (NFFT / 2);
        double e = 0.0;
        for (i = 0; i < NFFT; i++) {
            const float h = 0.5f - 0.5f * cosf(2.0f * (float)M_PI * (float)i / (float)NFFT);
            const float l = cap[(at + (uint32_t)i) * 2u], r = cap[(at + (uint32_t)i) * 2u + 1u];
            re[i] = (l + r) * h;
            im[i] = 0.0f;
            lr += (double)l * (double)r;
            ll += (double)l * (double)l;
            rr += (double)r * (double)r;
            e += (double)l * (double)l;
        }
        fft(re, im, NFFT);
        for (i = 1; i < NFFT / 2; i++) {
            const double p = (double)re[i] * (double)re[i] + (double)im[i] * (double)im[i];
            const double hz = (double)i * (double)FS / NFFT;
            b = hz < 300.0 ? 0 : hz < 1000.0 ? 1 : hz < 3000.0 ? 2 : 3;
            pw[b] += p;
            cn += p * hz;
            cd += p;
        }
        if (last >= 0.0)
            mv += fabs(sqrt(e) - sqrt(last));
        last = e;
    }
    for (b = 0; b < 4; b++)
        o->bands[b] = 10.0f * log10f((float)(pw[b] / (cd > 0.0 ? cd : 1.0)) + 1e-9f);
    o->centroid = (float)(cn / (cd > 0.0 ? cd : 1.0));
    o->rms = rms_of(from, frames);
    o->corr = (float)(lr / sqrt(ll * rr + 1e-20));
    o->flux = (float)(mv / (double)nseg / (sqrt(ll / (double)nseg) + 1e-9));
}
static void band_show(const char *layer, int par, int v, const band_t *b)
{
    char nm[12];
    en_param_text(par, v, nm);
    printf("  %-6s %-9s rms %6.1f dB, centroid %5.0f Hz, <300 %5.1f, <1k %5.1f, <3k %5.1f, over %5.1f, L/R %.2f, moves %.3f\n", layer, nm, (double)db(b->rms), (double)b->centroid, (double)b->bands[0],
           (double)b->bands[1], (double)b->bands[2], (double)b->bands[3], (double)b->corr, (double)b->flux);
}
static float band_gap(const band_t *a, const band_t *b)
{
    float s = 0.0f;
    int k;
    for (k = 0; k < 4; k++)
        s += (a->bands[k] - b->bands[k]) * (a->bands[k] - b->bands[k]);
    return sqrtf(s / 4.0f);
}

/* ---- the layer in the real song, alone: its level and its spectrum over a phrase of chords */
static void layer_alone(band_t *o, int par, int v, int pad_only, uint32_t seed)
{
    start_engine(seed);
    en_set(P_KEYS, 40);
    en_set(P_MOTIFS, 0);
    en_set(P_BASS, 0);
    en_set(P_DRUMS, 0);
    en_set(P_HATS, 0);
    en_set(par, v);
    quiet_room();
    en_restore();
    en_test_pad_off = !pad_only;
    en_test_keys_gain = pad_only ? 0.0f : 1.0f;
    render(SECS(4));
    ncap = 0u;
    capture(SECS(10));
    bands_of(o, 0u, (uint32_t)(ncap / 2u));
}
static void t_keys_pad_in_the_song(void)
{
    int a, b, pad_only;
    for (pad_only = 0; pad_only < 2; pad_only++) {
        const int par = pad_only ? P_PADSND : P_KEYSND;
        const int n = en_param_info(par)->hi + 1;
        band_t o[4];
        for (a = 0; a < n; a++) {
            layer_alone(&o[a], par, a, pad_only, 21u);
            band_show(pad_only ? "pad" : "keys", par, a, &o[a]);
            CHECK(fabsf(db(o[a].rms / o[0].rms)) < LEVEL_DB, "%s %d: %.1f dB from the first", pad_only ? "pad" : "keys", a, (double)db(o[a].rms / o[0].rms));
        }
        for (a = 0; a < n; a++)
            for (b = 0; b < a; b++)
                CHECK(band_gap(&o[a], &o[b]) > 2.0f || fabsf(o[a].flux - o[b].flux) > 0.05f, "%s %d and %d: %.1f dB apart over the bands", pad_only ? "pad" : "keys", a, b, (double)band_gap(&o[a], &o[b]));
    }
}

/* ---- the melody's timbres: within their stated range of each other, and what Auto picks by default */
static void t_melody_level(void)
{
    int t;
    float lo = 1e9f, hi = -1e9f;
    for (t = 0; t < EN_NTIMBRES; t++) {
        float r;
        start_engine(3u);
        en_set(P_MOTIFS, 40);
        en_set(P_MELSND, t + 1);
        quiet_room();
        render(2u);
        en_test_bell_note(t, 76, 0.6f);
        ncap = 0u;
        capture(SECS(0.6f));
        r = db(rms_of(0u, ncap / 2u));
        printf("  melody %-9s rms over its first 0.6 s %6.1f dB\n", EN_TIMBRE_NAME[t], (double)r);
        lo = r < lo ? r : lo;
        hi = r > hi ? r : hi;
    }
    CHECK(hi - lo < 2.0f * MELODY_DB, "the timbres: %.1f dB from the softest to the loudest", (double)(hi - lo));
}

/* ---- a change in mid-note: nothing that a sample's step would say. The measure: the largest second difference. A step or a kink stands out of a low smooth wave at once. It may be no worse than
 * what either sound does by itself at its loudest: for the bass, with a note going on from the last, since that is what it does all the time */
static float d2_of(uint32_t from, uint32_t to)
{
    float m = 0.0f;
    uint32_t j;
    int c;
    for (c = 0; c < 2; c++)
        for (j = from < 2u ? 2u : from; j < to && (j * 2u + (uint32_t)c) < ncap; j++) {   /* (from itself: a change's step lies on the block's first samples) */
            const float d = fabsf(cap[j * 2u + (uint32_t)c] - 2.0f * cap[(j - 1u) * 2u + (uint32_t)c] + cap[(j - 2u) * 2u + (uint32_t)c]);
            m = d > m ? d : m;
        }
    return m;
}
typedef void (*set_up_t)(int v);
static void up_bass(int v)
{
    start_engine(3u);
    en_set(P_BASS, 70);
    en_set(P_BASSSND, v);
    quiet_room();
    render(2u);
    en_test_bass_note(BASS_NOTE, 256u);
    render(SECS(0.5f));
}
static void up_keys(int v)
{
    int k;
    static const int8_t CH[4] = {48, 60, 64, 67};
    start_engine(3u);
    en_set(P_KEYS, 60);
    en_set(P_KEYSND, v);
    quiet_room();
    render(2u);
    for (k = 0; k < 4; k++)
        en_test_key_note(CH[k], 0.9f);
    render(SECS(0.8f));
}
static void up_pad(int v)
{
    start_engine(3u);
    en_set(P_KEYS, 60);
    en_set(P_PADSND, v);
    en_set(P_MOTIFS, 0);
    en_set(P_BASS, 0);
    en_set(P_DRUMS, 0);
    en_set(P_HATS, 0);
    quiet_room();
    en_restore();
    en_test_keys_gain = 0.0f;
    render(SECS(3));
}
static float worst_change(const char *name, set_up_t up, int par)
{
    const int n = en_param_info(par)->hi + 1;
    int a, b, off;
    float worst = 0.0f;
    for (a = 0; a < n; a++)
        for (b = 0; b < n; b++) {
            if (a == b)
                continue;
            for (off = 0; off < 3; off++) {
                float before, across, after;
                up(a);
                render((uint32_t)off);
                ncap = 0u;
                if (par == P_BASSSND)
                    en_test_bass_note(BASS_NOTE + 7, 256u);
                capture(20u);                    /* what it was doing */
                before = d2_of(0u, ncap / 2u);
                en_set(par, b);
                capture(60u);                    /* the change, and a third of a second */
                across = d2_of(20u * BLOCK, ncap / 2u);
                if (par == P_BASSSND)
                    en_test_bass_note(BASS_NOTE, 256u);
                capture(100u);
                after = d2_of(80u * BLOCK, ncap / 2u);
                {
                    const float ref = (before > after ? before : after) + 1e-6f, r = across / ref;
                    worst = r > worst ? r : worst;
                    CHECK(r < 1.6f, "%s %d to %d: %.5f across the change, %.5f before, %.5f after", name, a, b, (double)across, (double)before, (double)after);
                }
            }
        }
    printf("  a change in mid-note, %s: at most %.2f times what the sounds do by themselves\n", name, (double)worst);
    return worst;
}
static void t_no_click(void)
{
    worst_change("bass", up_bass, P_BASSSND);
    worst_change("keys", up_keys, P_KEYSND);
    worst_change("pad", up_pad, P_PADSND);
}

/* a quick change, over and over, in the song itself: no click against the same song without it, and notes down are what MIDI says, all the way, and after */
static void ledger_agrees(const char *when)
{
    CHECK(nd[0] == en_test_keys_down(), "%s: keys %d down, MIDI says %d", when, en_test_keys_down(), nd[0]);
    CHECK(nd[1] == en_test_bells_on(), "%s: melody %d down, MIDI says %d", when, en_test_bells_on(), nd[1]);
    CHECK(nd[2] == en_test_bass_down(), "%s: bass %d down, MIDI says %d", when, en_test_bass_down(), nd[2]);
}
/* a chosen timbre that outlives the phrase's own has told MIDI it is over and is still sounding: it is a key all the same. One at a time, and let go by a chord it is not in: no longer outside the chord than Auto's own notes are (a long note can be: the phrase's way, not the sound's) */
static uint32_t off_chord_blocks(int sound, uint32_t *most)
{
    uint32_t b, outside = 0u;
    *most = 0u;
    start_playing(31u, 0);
    en_set(P_MRATE, 100);
    en_set(P_MTONE, 50);
    en_set(P_MELSND, sound);
    for (b = 0; b < SECS(600); b++) {
        render(1u);
        if (!(b % 900u))
            en_next();                           /* a chord that was not foreseen */
        if ((uint32_t)en_test_bell_keys() > *most)
            *most = (uint32_t)en_test_bell_keys();
        outside += (uint32_t)en_test_bell_off_chord();
    }
    return outside;
}
static void t_keys_of_the_melody(void)
{
    uint32_t most, gone_first = en_test_bell_case(1);
    const uint32_t own = off_chord_blocks(0, &most), flute = off_chord_blocks(EN_TB_FLUTE + 1, &most);
    CHECK(en_test_bell_case(1) > gone_first, "a note's MIDI over before its sound: never happened");
    CHECK(most == 1u, "%u keys of the melody down at once", most);
    CHECK(flute <= own, "%u blocks of a key down that its chord does not have, Auto has %u", flute, own);
}

static void t_rapid(void)
{
    int hands;
    for (hands = 0; hands < 2; hands++) {
        uint32_t s, k = 0u;
        int l;
        start_playing(55u, hands);
        set_keen();
        en_set(P_DRUMS, 0);
        for (s = 0; s < 600u; s++) {
            render(3u);
            for (l = 0; l < 4; l++)
                en_set(LAYER[l].par, (int)((k * (unsigned)(l + 3)) % (unsigned)(en_param_info(LAYER[l].par)->hi + 1)));
            en_mel_hear();
            k++;
            if (!(s % 16u))
                ledger_agrees("changing");
        }
        ledger_agrees("after");
        CHECK(!en_test_nans, "%u NaNs", en_test_nans);
        en_play(0);
        render(SECS(20));                        /* a rest: what is struck lets go, whatever it was */
        CHECK(!nd[1] && !nd[2], "at rest: melody %d, bass %d down", nd[1], nd[2]);
        CHECK(!en_test_bells_on(), "at rest, the melody down");
        CHECK(!en_test_bell_voices(), "at rest, %d melody voices still sound", en_test_bell_voices());
        en_panic();
        render(4u);
        CHECK(!nd[0] && !nd[1] && !nd[2] && !nd[3], "after a panic: %d %d %d %d down", nd[0], nd[1], nd[2], nd[3]);
    }
    /* a timbre's sound over before its MIDI, then a panic: nothing stays down */
    {
        uint32_t s;
        start_playing(55u, 0);
        en_set(P_MRATE, 100);
        en_set(P_MELSND, EN_TB_MARIMBA + 1);
        for (s = 0; s < 4000u; s++) {
            render(1u);
            if (nd[1] && !en_test_bell_voices())
                break;
        }
        CHECK(nd[1] == 1, "a note down, its voice over: never came");
        en_panic();
        render(4u);
        CHECK(!nd[1], "after a panic: %d down", nd[1]);
        en_restore();
        render(SECS(2));
        CHECK(!nd[1] || en_test_bells_on(), "after: nothing left");
    }
}

/* ---- the change is heard soon: keys and pad at once, on what is sounding, resting too; the melody's choice says itself when nothing is playing; the rest of the voices do not move */
static void t_heard_soon(void)
{
    int v;
    /* the hand and the pad at rest, the sound changed: each one's spectrum moves within a second, with nothing struck. Each by itself. Organ's bus is the first's: only the index on what is sounding can say it */
    {
        int pad_only;
        for (pad_only = 0; pad_only < 2; pad_only++) {
            band_t was, now;
            uint32_t notes;
            start_engine(9u);
            en_set(P_KEYS, 40);
            en_set(P_MOTIFS, 0);
            en_set(P_BASS, 0);
            en_set(P_DRUMS, 0);
            en_set(P_HATS, 0);
            quiet_room();
            en_restore();
            en_test_pad_off = !pad_only;
            en_test_keys_gain = pad_only ? 0.0f : 1.0f;
            render(SECS(6));
            en_play(0);
            render(SECS(3));
            notes = en_ep_notes;
            ncap = 0u;
            capture(SECS(1));
            bands_of(&was, 0u, ncap / 2u);
            en_set(pad_only ? P_PADSND : P_KEYSND, pad_only ? 1 : 2);
            render(SECS(1));
            ncap = 0u;
            capture(SECS(1));
            bands_of(&now, 0u, ncap / 2u);
            CHECK(en_ep_notes == notes, "nothing struck at rest");
            CHECK(band_gap(&was, &now) > 1.5f, "%s at rest: the change is not heard: %.2f dB over the bands", pad_only ? "pad" : "keys", (double)band_gap(&was, &now));
        }
        en_test_pad_off = 0;
    }
    /* the pad and the keys' bus go there over a while, not at once, and arrive at the very number */
    {
        start_engine(9u);
        en_set(P_PADSND, 3);
        en_set(P_KEYSND, 3);
        render(1u);
        CHECK(en_test_sound_now(0) < 1.1f, "the pad's level is on its way: %.3f after a block", (double)en_test_sound_now(0));
        CHECK(en_test_sound_now(1) > 0.9f, "the keys' bus is on its way: %.3f after a block", (double)en_test_sound_now(1));
        render(SECS(0.8f));
        CHECK(en_test_sound_now(0) == 1.40f, "the pad's level has arrived: %.5f", (double)en_test_sound_now(0));
        CHECK(en_test_sound_now(1) == 0.55f, "the keys' bus has arrived: %.5f", (double)en_test_sound_now(1));
    }
    /* a melody at rest between phrases: a note in the new timbre, not on MIDI, once however many detents; and none if Auto, none if the layer is off */
    for (v = 0; v < 3; v++) {
        uint32_t n0, m0;
        start_playing(9u, 1);
        en_set(P_MRATE, 0);
        en_layer(EN_L_MEL, v != 2);
        render(SECS(2));
        en_play(0);
        render(SECS(8));
        n0 = en_test_mel_heard;
        m0 = midi_n;
        en_set(P_MELSND, v == 1 ? 0 : 3);
        en_mel_hear();
        render(3u);
        en_set(P_MELSND, v == 1 ? 0 : 4);
        en_mel_hear();
        render(SECS(1));
        CHECK(en_test_mel_heard - n0 == (v == 0 ? 1u : 0u), "case %d: heard %u times", v, en_test_mel_heard - n0);
        CHECK(midi_n == m0, "case %d: %u MIDI messages from it", v, midi_n - m0);
    }
}

int main(void)
{
    t_table();
    t_first_is_as_it_was();
    t_midi_unchanged();
    t_melody_fixed();
    t_extremes();
    t_keys_measured();
    t_bass_measured();
    t_keys_pad_in_the_song();
    t_melody_level();
    t_no_click();
    t_keys_of_the_melody();
    t_rapid();
    t_heard_soon();
    if (fails) {
        printf("%d failures\n", fails);
        return 1;
    }
    printf("sound_test: ok\n");
    return 0;
}
