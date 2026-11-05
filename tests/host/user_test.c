/* SPDX-License-Identifier: MPL-2.0 */
/* The player's own sounds (en_user_*, P_*SND at User). What is asked of them, by measurement:
 *  - with User not selected nothing has moved: pinned renders of the commit before, to the bit;
 *  - a set just loaded from a sound IS that sound: audio and MIDI to the bit, in Auto and in Hands, however it got there, in mid-song too;
 *  - each layer in User takes the values, and each value does what its name says;
 *  - MIDI is the same whatever the values, and whenever they change;
 *  - at every corner of the values: no NaN, into the limiter no more than the bound, no louder than the bound by a level that hears as an ear does;
 *  - a change in mid-note steps nothing: ratio, depth and the rest, one at a time and all at once, quickly, over a key held;
 *  - no flood, no tear: the queue has nothing to do with it, and what is written last is what stays;
 *  - a mute is a mute, a panic a panic, and what was held comes up.
 * What the panel does is tests/scenarios/user.amb. */
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

/* the bounds */
#define PEAK_RATIO 1.5f                          /* into the limiter, in the song: against the first sounds at the same place. As the list's own sounds are held to (sound_test) */
#define PEAK_MOST 2.5f                           /* and at all */
#define RMS_DB 1.5f                              /* a note's level, plain: no more than this over the sound it started from */
#define HEARD_DB 9.0f                            /* a level weighted as an ear hears (A): a harsh setting no more than this over the layer's first sound, on the note where it counts most */
#define CLICK_RATIO 1.6f                         /* a change in mid-note: the largest second difference, against what either sound does by itself. As sound_test holds the list's changes */
#define CLICK_FLOOR 0.0003f                      /* and against this, -70 dB of full scale: where a low sine's second difference is only the table's corners */

static uint32_t midi_hash, midi_n, midi_block;
static int down[16][128], nd[16];
void en_midi_out(uint32_t st, uint32_t d1, uint32_t d2)
{
    const int c = (int)(st & 15u), k = (int)(st & 0xF0u);
    midi_hash = (midi_hash ^ (st << 16 | d1 << 8 | d2)) * 16777619u;
    midi_hash = (midi_hash ^ midi_block) * 16777619u;
    midi_n++;
    if (st >= 0xF0u)
        return;
    if (k == 0x90 && d2) {
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
    midi_n = midi_block = 0u;
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
static const char *const LN[EN_NUSER] = {"keys", "melody", "bass", "pad"};

/* ---- to measure */
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
static float db(float x) { return 20.0f * log10f(x > 1e-7f ? x : 1e-7f); }
static float rms_of(uint32_t from, uint32_t n)
{
    double s = 0.0;
    uint32_t j;
    for (j = from * 2u; j < (from + n) * 2u && j < ncap; j++)
        s += (double)cap[j] * (double)cap[j];
    return (float)sqrt(s / (double)(2u * n));
}
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
static float note_hz(int n) { return 440.0f * powf(2.0f, ((float)n - 69.0f) / 12.0f); }
/* a level as an ear has it: the A curve's corners, as one-poles, one run over the left and the right together */
static float heard_of(uint32_t from, uint32_t n)
{
    static const float HP[4] = {20.6f, 20.6f, 107.7f, 737.9f}, LP[2] = {12194.0f, 12194.0f};
    float hs[4] = {0.0f, 0.0f, 0.0f, 0.0f}, ls[2] = {0.0f, 0.0f}, hc[4], lc[2];
    double s = 0.0;
    uint32_t j;
    int k;
    for (k = 0; k < 4; k++)
        hc[k] = 1.0f - expf(-2.0f * (float)M_PI * HP[k] / FS);
    for (k = 0; k < 2; k++)
        lc[k] = 1.0f - expf(-2.0f * (float)M_PI * LP[k] / FS);
    for (j = from; j < from + n && j * 2u + 1u < ncap; j++) {
        float x = 0.5f * (cap[j * 2u] + cap[j * 2u + 1u]);
        for (k = 0; k < 4; k++) {
            hs[k] += hc[k] * (x - hs[k]);
            x -= hs[k];
        }
        for (k = 0; k < 2; k++) {
            ls[k] += lc[k] * (x - ls[k]);
            x = ls[k];
        }
        s += (double)x * (double)x;
    }
    return (float)sqrt(s / (double)n) * 2.4f;    /* (0 dB at 1 kHz, about) */
}
static float d2_of(uint32_t from, uint32_t to)
{
    float m = 0.0f;
    uint32_t j;
    int c;
    for (c = 0; c < 2; c++)
        for (j = from < 2u ? 2u : from; j < to && (j * 2u + (uint32_t)c) < ncap; j++) {
            const float d = fabsf(cap[j * 2u + (uint32_t)c] - 2.0f * cap[(j - 1u) * 2u + (uint32_t)c] + cap[(j - 2u) * 2u + (uint32_t)c]);
            m = d > m ? d : m;
        }
    return m;
}

/* ---- a set by its values */
static void corner(en_user_t *u, int l, int bits)
{
    int p;
    for (p = 0; p < EN_NUP; p++)
        u->v[p] = (uint8_t)(bits >> p & 1 ? en_user_steps(l, p) - 1 : 0);
}
static int ratio_step(int l, const char *t)      /* the step whose ratio says this */
{
    en_user_t u;
    char b[12];
    int s;
    en_user_load(l, 0, &u);
    for (s = 0; s < en_user_steps(l, EN_UP_RATIO); s++) {
        u.v[EN_UP_RATIO] = (uint8_t)s;
        en_user_text(l, EN_UP_RATIO, &u, b);
        if (!strcmp(b, t))
            return s;
    }
    CHECK(0, "%s: no ratio %s", LN[l], t);
    return 0;
}
static void pick_user(int l, const en_user_t *u)
{
    en_user_set(l, u);
    en_set(en_user_par(l), en_user_value(l));
}

/* ---- the lists */
static void t_lists(void)
{
    int l, p, f, s;
    char a[12], b[12];
    for (l = 0; l < EN_NUSER; l++) {
        const en_param_t *pi = en_param_info(en_user_par(l));
        CHECK(pi->hi == en_user_value(l), "%s: User is the last of its list", LN[l]);
        en_param_text(en_user_par(l), en_user_value(l), a);
        CHECK(!strcmp(a, "User"), "%s: it is called User: %s", LN[l], a);
        CHECK(pi->def == 0, "%s: not by default", LN[l]);
        CHECK(en_user_nfrom(l) == (l == EN_U_MEL ? EN_NTIMBRES : 4), "%s: %d sounds to start from", LN[l], en_user_nfrom(l));
        for (f = 0; f < en_user_nfrom(l); f++) {
            en_user_t u;
            en_user_load(l, f, &u);
            CHECK(u.from == f, "%s %d: it says where it came from", LN[l], f);
            CHECK(en_user_ok(l, &u), "%s %d: its own values in range", LN[l], f);
            CHECK(!en_user_moved(l, &u), "%s %d: just loaded, not moved", LN[l], f);
            en_param_text(en_user_par(l), l == EN_U_MEL ? f + 1 : f, a);
            CHECK(!strcmp(a, en_user_from_name(l, f)), "%s %d: \"%s\", the list says \"%s\"", LN[l], f, en_user_from_name(l, f), a);
        }
        for (p = 0; p < EN_NUP; p++) {
            en_user_t u, bad;
            const int n = en_user_steps(l, p);
            CHECK(n >= 2 && n <= 255, "%s %s: %d steps", LN[l], en_user_name(l, p), n);
            CHECK(strlen(en_user_name(l, p)) >= 3, "%s %d: a name", LN[l], p);
            en_user_load(l, 0, &u);
            for (s = 0; s < n; s++) {
                u.v[p] = (uint8_t)s;
                CHECK(en_user_ok(l, &u), "%s %s %d: in range", LN[l], en_user_name(l, p), s);
                en_user_text(l, p, &u, b);
                CHECK(strlen(b) >= 1 && strlen(b) <= 9, "%s %s %d: \"%s\"", LN[l], en_user_name(l, p), s, b);
            }
            bad = u;
            bad.v[p] = (uint8_t)n;
            CHECK(!en_user_ok(l, &bad), "%s %s: step %d refused", LN[l], en_user_name(l, p), n);
            bad.v[p] = 255u;
            CHECK(!en_user_ok(l, &bad), "%s %s: step 255 refused", LN[l], en_user_name(l, p));
        }
        {
            en_user_t bad;
            en_user_load(l, 0, &bad);
            bad.from = (uint8_t)en_user_nfrom(l);
            CHECK(!en_user_ok(l, &bad), "%s: from %d refused", LN[l], bad.from);
        }
    }
    /* whole numbers, useful fractions, the bells' */
    {
        static const char *const WANT[] = {"0.5", "1", "1.5", "2", "3", "4", "5", "7", "14", "1.41", "2.76", "3.14"};
        unsigned i;
        for (l = 0; l < EN_NUSER; l++)
            for (i = 0; i < sizeof WANT / sizeof WANT[0]; i++)
                (void)ratio_step(l, WANT[i]);
    }
    CHECK(P_NPARAMS + 6 <= EN_ROOM_MAX, "a piece's commands fit the queue");
    CHECK(P_NPARAMS == 46, "not a command more: %d", P_NPARAMS);
    CHECK(EN_ROOM_MAX == 56u, "the queue as it was");
}

/* ---- with User not selected: the commit before, to the bit. The same renders fx_test pins (tests of that commit's code: t13's report has the whole of the listening files) */
static const struct {
    int arr, style, mel;
    uint32_t audio, midi, n;
} PIN[] = {
    {0, 0, 3, 0x9cb8b925u, 0x8c4263d8u, 5101u},
    {0, 2, 4, 0x226b125cu, 0x0bc27902u, 4822u},
    {0, 5, 1, 0x4f78f349u, 0x48d78526u, 4383u},
    {1, 0, 2, 0xfce9c1b0u, 0xfe65891bu, 5639u},
    {1, 1, 3, 0x64f5cdbau, 0xd5797925u, 6668u},
    {1, 4, 1, 0x3d5f9c4fu, 0xe336c257u, 4924u},
};
static void t_pinned(void)
{
    unsigned r;
    for (r = 0; r < sizeof PIN / sizeof PIN[0]; r++) {
        int l, st;
        /* whatever the User sets are, while nothing has selected them */
        for (st = 0; st < 2; st++) {
            en_init();
            en_seed(4000u + r);
            en_arrange(PIN[r].arr);
            en_set(P_STYLE, PIN[r].style);
            en_set(P_MELMODE, PIN[r].mel == 3 ? EN_MEL_ARP : EN_MEL_PHRASES);
            for (l = 0; l < EN_NLAYERS; l++)
                en_layer(l, 1);
            en_play(1);
            if (st)
                for (l = 0; l < EN_NUSER; l++) {
                    en_user_t u;
                    en_user_load(l, 1, &u);
                    corner(&u, l, 0x2A + l);
                    en_user_set(l, &u);
                }
            clear_counts();
            {
                uint32_t t, j, ah = 2166136261u;
                for (t = 0u; t < 44100u * 60u / 256u; t++) {
                    midi_block++;
                    en_render(blk, 256u, 2430u);
                    for (j = 0u; j < 512u; j++)
                        ah = (ah ^ (uint32_t)blk[j]) * 16777619u;
                }
                CHECK(ah == PIN[r].audio, "pin %u, sets %d: audio %08x, not %08x", r, st, ah, PIN[r].audio);
                CHECK(midi_hash == PIN[r].midi && midi_n == PIN[r].n, "pin %u, sets %d: MIDI %08x, %u messages, not %08x, %u", r, st, midi_hash, midi_n, PIN[r].midi, PIN[r].n);
            }
        }
    }
}

/* ---- a set just loaded is the sound: audio and MIDI, to the bit */
static void hash_with(int hands, int mode, int l, int v, uint32_t seed, uint32_t *a, uint32_t *m, int late)
{
    start_playing(seed, hands);
    en_set(P_MRATE, 90);
    en_set(P_MELMODE, mode);
    if (mode >= EN_MEL_KEYS)
        en_keys(0x1101u);
    if (v == en_user_value(l)) {                 /* (from: the sound in late) */
        en_user_t u;
        en_user_load(l, late & 0xFF, &u);
        if (late >> 8) {                         /* it has been playing in the sound; the set comes in mid-song */
            en_set(en_user_par(l), l == EN_U_MEL ? (late & 0xFF) + 1 : late & 0xFF);
            render(SECS(8));
            pick_user(l, &u);
        } else {
            pick_user(l, &u);
        }
        render(SECS((late >> 8) ? 22 : 30));
    } else {
        en_set(en_user_par(l), v);
        render(SECS(30));
    }
    *a = audio_hash;
    *m = midi_hash;
}
static void t_from_is_the_sound(void)
{
    int l, f, hands, mode;
    for (l = 0; l < EN_NUSER; l++)
        for (f = 0; f < en_user_nfrom(l); f++)
            for (hands = 0; hands < 2; hands++) {
                const int sound = l == EN_U_MEL ? f + 1 : f;
                static const int MODES[2] = {EN_MEL_PHRASES, EN_MEL_KEYS};
                for (mode = 0; mode < (l == EN_U_MEL ? 2 : 1); mode++) {
                    uint32_t a0, m0, a1, m1, a2, m2;
                    hash_with(hands, MODES[mode], l, sound, 800u + (uint32_t)f, &a0, &m0, 0);
                    hash_with(hands, MODES[mode], l, en_user_value(l), 800u + (uint32_t)f, &a1, &m1, f);
                    CHECK(a0 == a1, "%s from %s, Hands %d, mode %d: the audio differs", LN[l], en_user_from_name(l, f), hands, MODES[mode]);
                    CHECK(m0 == m1, "%s from %s, Hands %d, mode %d: the MIDI differs", LN[l], en_user_from_name(l, f), hands, MODES[mode]);
                    hash_with(hands, MODES[mode], l, en_user_value(l), 800u + (uint32_t)f, &a2, &m2, f | 1 << 8);
                    CHECK(a0 == a2, "%s, into it in mid-song from %s, Hands %d, mode %d: the audio differs", LN[l], en_user_from_name(l, f), hands, MODES[mode]);
                    CHECK(m0 == m2, "%s, into it in mid-song from %s, Hands %d, mode %d: the MIDI differs", LN[l], en_user_from_name(l, f), hands, MODES[mode]);
                }
            }
    /* a step moved and brought back is the sound again */
    {
        en_user_t u;
        uint32_t a0, m0, a1, m1;
        for (l = 0; l < EN_NUSER; l++) {
            int p;
            en_user_load(l, 1, &u);
            for (p = 0; p < EN_NUP; p++) {
                const uint8_t was = u.v[p];
                u.v[p] = (uint8_t)(was ? was - 1 : was + 1);
                CHECK(en_user_moved(l, &u), "%s %s moved: not moved?", LN[l], en_user_name(l, p));
                u.v[p] = was;
                CHECK(!en_user_moved(l, &u), "%s %s back: moved?", LN[l], en_user_name(l, p));
            }
        }
        hash_with(0, EN_MEL_PHRASES, EN_U_PAD, 1, 91u, &a0, &m0, 0);
        hash_with(0, EN_MEL_PHRASES, EN_U_PAD, en_user_value(EN_U_PAD), 91u, &a1, &m1, 1);
        CHECK(a0 == a1 && m0 == m1, "a pad from Airy: not Airy");
    }
}

/* ---- each value is what its name says. A note alone */
#define KEY_NOTE 57
#define MEL_NOTE 76
#define BASS_NOTE 36
static void quiet_room(void)
{
    en_set(P_ECHO, 0);
    en_set(P_REVERB, 0);
}
/* one layer, a note, nothing else: the sound's first seconds in cap */
static void note_of(int l, const en_user_t *u, uint32_t blocks)
{
    start_engine(3u);
    en_set(P_MOTIFS, 60);
    en_set(P_KEYS, 60);
    en_set(P_BASS, 70);
    en_set(P_DRUMS, 0);
    en_set(P_HATS, 0);
    quiet_room();
    if (u)
        pick_user(l, u);
    render(2u);
    ncap = 0u;
    if (l == EN_U_KEYS) {
        en_test_key_note(KEY_NOTE, 0.9f);
        en_test_pad_off = 1;
    } else if (l == EN_U_MEL) {
        en_test_bell_note(u ? EN_TB_USER : EN_TB_FLUTE, MEL_NOTE, 0.6f);
    } else if (l == EN_U_BASS) {
        en_test_bass_note(BASS_NOTE, 32u);
    } else {
        en_test_keys_gain = 0.0f;
        en_restore();
    }
    capture(blocks);
}
static uint32_t half_at_of(uint32_t frames)      /* where it has come to half of its peak */
{
    uint32_t j;
    float peak = 0.0f;
    for (j = 0; j + 256u < frames; j += 64u) {
        const float r = rms_of(j, 256u);
        peak = r > peak ? r : peak;
    }
    for (j = 0; j + 256u < frames; j += 64u)
        if (rms_of(j, 256u) >= 0.5f * peak)
            return j;
    return frames;
}
static void t_uses_values(void)
{
    int l;
    /* the ratio: a carrier at f and a modulator at k f make partials at f + k f */
    for (l = 0; l < EN_U_PAD; l++) {
        static const float KS[3] = {2.0f, 1.41f, 2.76f};
        static const char *const KT[3] = {"2", "1.41", "2.76"};
        int i;
        for (i = 0; i < 3; i++) {
            en_user_t u;
            const int note = l == EN_U_KEYS ? KEY_NOTE : l == EN_U_MEL ? MEL_NOTE : BASS_NOTE;
            const float f = note_hz(note);
            float side, near, carrier;
            en_user_load(l, 0, &u);
            u.v[EN_UP_RATIO] = (uint8_t)ratio_step(l, KT[i]);
            u.v[EN_UP_DEPTH] = (uint8_t)((en_user_steps(l, EN_UP_DEPTH) - 1) * 70 / 100);
            note_of(l, &u, SECS(1.5f));
            CHECK(fabsf(en_test_user_num(l, EN_UP_RATIO) - KS[i]) < 1e-3f, "%s: the ratio in the render is %.3f, not %.3f", LN[l], (double)en_test_user_num(l, EN_UP_RATIO), (double)KS[i]);
            carrier = tone_at((uint32_t)(0.3f * FS), 16384u, f);
            side = tone_at((uint32_t)(0.3f * FS), 16384u, f * (1.0f + KS[i]));
            near = tone_at((uint32_t)(0.3f * FS), 16384u, f * (1.5f + KS[i]));   /* (where nothing should be) */
            CHECK(side > 4.0f * near, "%s ratio %s: a partial at %.0f Hz %.1f dB, %.1f dB where there should be none", LN[l], KT[i], (double)(f * (1.0f + KS[i])), (double)db(side), (double)db(near));
            CHECK(carrier > 0.0f, "%s ratio %s: a carrier", LN[l], KT[i]);
        }
    }
    /* the depth: at 0 a sine, up it has partials */
    for (l = 0; l < EN_U_PAD; l++) {
        en_user_t u;
        const int note = l == EN_U_KEYS ? KEY_NOTE : l == EN_U_MEL ? MEL_NOTE : BASS_NOTE;
        const float f = note_hz(note);
        float none, some, hi;
        en_user_load(l, 0, &u);
        u.v[EN_UP_RATIO] = (uint8_t)ratio_step(l, "2");
        u.v[EN_UP_DEPTH] = 0u;
        if (l == EN_U_BASS)
            u.v[EN_UP_MOD] = 0u;
        note_of(l, &u, SECS(1.5f));
        none = tone_at((uint32_t)(0.5f * FS), 16384u, f * 3.0f) / (tone_at((uint32_t)(0.5f * FS), 16384u, f) + 1e-7f);
        u.v[EN_UP_DEPTH] = (uint8_t)((en_user_steps(l, EN_UP_DEPTH) - 1) / 2);
        note_of(l, &u, SECS(1.5f));
        some = tone_at((uint32_t)(0.5f * FS), 16384u, f * 3.0f) / (tone_at((uint32_t)(0.5f * FS), 16384u, f) + 1e-7f);
        u.v[EN_UP_DEPTH] = (uint8_t)(en_user_steps(l, EN_UP_DEPTH) - 1);
        note_of(l, &u, SECS(1.5f));
        hi = tone_at((uint32_t)(0.5f * FS), 16384u, f * 3.0f) / (tone_at((uint32_t)(0.5f * FS), 16384u, f) + 1e-7f);
        printf("  %s: the partial's share at depth 0, half, all: %.1f, %.1f, %.1f dB\n", LN[l], (double)db(none), (double)db(some), (double)db(hi));
        CHECK(some > 4.0f * none, "%s: depth 50 no more than depth 0: %.1f, %.1f dB", LN[l], (double)db(some), (double)db(none));
    }
    /* attack: how long it takes to its peak. Fast and slow */
    for (l = 0; l < EN_U_PAD; l++) {
        en_user_t u;
        char t[12];
        uint32_t fast, slow;
        en_user_load(l, 0, &u);
        u.v[EN_UP_ATT] = 0u;
        en_user_text(l, EN_UP_ATT, &u, t);
        note_of(l, &u, SECS(2.0f));
        fast = half_at_of(ncap / 2u);
        u.v[EN_UP_ATT] = (uint8_t)(en_user_steps(l, EN_UP_ATT) * 3 / 4);
        note_of(l, &u, SECS(2.0f));
        slow = half_at_of(ncap / 2u);
        CHECK(slow > fast + 2000u, "%s: attack fast at %u frames, slow at %u", LN[l], fast, slow);
        CHECK(fast < 0.05f * FS, "%s: attack %s: half of its peak at %.0f ms", LN[l], t, (double)((float)fast / FS * 1000.0f));
    }
    /* decay: gone soon, by a short one, against its first sound */
    for (l = 0; l < EN_U_PAD; l++) {
        en_user_t u;
        float gone, here;
        en_user_load(l, 0, &u);
        u.v[EN_UP_ATT] = 1u;
        u.v[EN_UP_DEC] = 0u;
        note_of(l, &u, SECS(2.5f));
        gone = rms_of((uint32_t)(0.45f * FS), 2048u) / (rms_of(0u, 4096u) + 1e-7f);
        CHECK(gone < 0.03f, "%s: decay at its shortest, after 450 ms: %.1f dB of the first part", LN[l], (double)db(gone));
        u.v[EN_UP_DEC] = (uint8_t)(en_user_steps(l, EN_UP_DEC) - 1);
        note_of(l, &u, SECS(2.5f));
        here = rms_of((uint32_t)(0.45f * FS), 2048u) / (rms_of(0u, 4096u) + 1e-7f);
        CHECK(here > 0.3f, "%s: decay at its longest, after 450 ms: %.1f dB of the first part", LN[l], (double)db(here));
    }
    /* the pad: its second saw's ratio, its filter, its envelope with the chords */
    {
        en_user_t u;
        float a, b;
        en_user_load(EN_U_PAD, 0, &u);
        note_of(EN_U_PAD, 0, SECS(4.0f));
        {
            const float lo = rms_of((uint32_t)(3.0f * FS), 16384u);
            a = lo;
        }
        u.v[EN_UP_DEPTH] = (uint8_t)(en_user_steps(EN_U_PAD, EN_UP_DEPTH) - 1);
        note_of(EN_U_PAD, &u, SECS(4.0f));
        b = heard_of((uint32_t)(3.0f * FS), 16384u) / (rms_of((uint32_t)(3.0f * FS), 16384u) + 1e-7f);
        en_user_load(EN_U_PAD, 0, &u);
        note_of(EN_U_PAD, &u, SECS(4.0f));
        a = heard_of((uint32_t)(3.0f * FS), 16384u) / (rms_of((uint32_t)(3.0f * FS), 16384u) + 1e-7f);
        CHECK(b > 1.4f * a, "the pad's filter open: %.2f, as it was %.2f, of what is heard against all of it", (double)b, (double)a);
        /* the decay, on the chords: it falls away inside a chord */
        en_user_load(EN_U_PAD, 0, &u);
        u.v[EN_UP_ATT] = 0u;
        u.v[EN_UP_DEC] = 0u;
        note_of(EN_U_PAD, &u, SECS(6.0f));
        a = rms_of((uint32_t)(5.0f * FS), 8192u);
        note_of(EN_U_PAD, 0, SECS(6.0f));
        b = rms_of((uint32_t)(5.0f * FS), 8192u);
        CHECK(a < 0.25f * b, "the pad's decay at its shortest: %.1f dB, the first sound %.1f dB", (double)db(a), (double)db(b));
        /* an attack by itself: every chord comes up from under: the level goes well down, again and again, as it does not in the first sound */
        {
            static const int STEP = 4096;
            float lo[2], hi[2];
            int w, t;
            for (w = 0; w < 2; w++) {
                en_user_load(EN_U_PAD, 0, &u);
                u.v[EN_UP_ATT] = (uint8_t)(en_user_steps(EN_U_PAD, EN_UP_ATT) - 7);
                note_of(EN_U_PAD, w ? &u : 0, SECS(12.0f));
                lo[w] = 1e9f;
                hi[w] = 0.0f;
                for (t = (int)(5.0f * FS); t + STEP < (int)(ncap / 2u); t += STEP) {
                    const float r = rms_of((uint32_t)t, (uint32_t)STEP);
                    lo[w] = r < lo[w] ? r : lo[w];
                    hi[w] = r > hi[w] ? r : hi[w];
                }
            }
            CHECK(lo[1] < 0.25f * hi[1], "the pad's attack alone: %.1f to %.1f dB", (double)db(lo[1]), (double)db(hi[1]));
            CHECK(lo[0] > 0.3f * hi[0], "the first sound's level goes %.1f to %.1f dB", (double)db(lo[0]), (double)db(hi[0]));
        }
    }
    /* in the song, each layer's voice is the set's, not a list's */
    {
        en_user_t u;
        en_user_load(EN_U_MEL, EN_TB_VIBES, &u);
        start_playing(99u, 0);
        en_set(P_MRATE, 100);
        en_user_set(EN_U_MEL, &u);
        en_set(P_MELSND, EN_MELSND_USER);
        {
            uint32_t b, in_it = 0u, other = 0u;
            for (b = 0; b < SECS(120); b++) {
                en_test_bell_struck = -1;
                render(1u);
                if (en_test_bell_struck >= 0) {
                    if (en_test_bell_struck == EN_TB_USER)
                        in_it++;
                    else
                        other++;
                }
            }
            CHECK(in_it > 4u && !other, "the melody in User: %u notes in it, %u in another", in_it, other);
        }
    }
}

/* ---- MIDI: whatever the values, and whenever */
static void midi_run(int hands, int mode, const en_user_t *us, int pick, int moving, uint32_t *h, uint32_t *n)
{
    uint32_t t;
    int l;
    start_playing(5150u, hands);
    en_set(P_MRATE, 100);
    en_set(P_NOTES, 3);
    en_set(P_ENERGY, 80);
    en_set(P_MELMODE, mode);
    en_set(P_ARPSKIP, 0);
    render(4u);
    if (mode >= EN_MEL_KEYS)
        en_keys(0x1111u);
    if (pick)
        for (l = 0; l < EN_NUSER; l++)
            pick_user(l, &us[l]);
    for (t = 0u; t < SECS(60); t += 40u) {
        render(40u);
        if (moving) {
            const int lay = (int)(t / 40u) % EN_NUSER, par = (int)(t / 160u) % EN_NUP;
            en_user_t u = us[lay];
            u.v[par] = (uint8_t)((t / 40u * 7u) % (uint32_t)en_user_steps(lay, par));
            en_user_set(lay, &u);
            en_user_hear(lay);
        }
    }
    *h = midi_hash;
    *n = midi_n;
}
static void t_midi(void)
{
    static const int MODES[4] = {EN_MEL_PHRASES, EN_MEL_ARP, EN_MEL_KEYS, EN_MEL_KEYS_ARP};
    int hands, m, c, l;
    for (hands = 0; hands < 2; hands++)
        for (m = 0; m < 4; m++) {
            uint32_t h0, n0, h, n;
            en_user_t us[EN_NUSER];
            midi_run(hands, MODES[m], us, 0, 0, &h0, &n0);
            CHECK(n0 > 300u, "MIDI to compare: %u", n0);
            for (c = 0; c < 4; c++) {
                static const int CORN[4] = {0, 0x3F, 0x15, 0x2A};
                for (l = 0; l < EN_NUSER; l++) {
                    en_user_load(l, c % en_user_nfrom(l), &us[l]);
                    corner(&us[l], l, CORN[c]);
                }
                midi_run(hands, MODES[m], us, 1, 0, &h, &n);
                CHECK(h == h0 && n == n0, "Hands %d, mode %d, corner %x: the MIDI differs: %u messages, not %u", hands, MODES[m], CORN[c], n, n0);
            }
            for (l = 0; l < EN_NUSER; l++)
                en_user_load(l, 0, &us[l]);
            midi_run(hands, MODES[m], us, 1, 1, &h, &n);
            CHECK(h == h0 && n == n0, "Hands %d, mode %d, changing all the time: the MIDI differs: %u messages, not %u", hands, MODES[m], n, n0);
        }
}

/* ---- every corner: no NaN, no more into the limiter than the bound */
static float song_peak(int l, const en_user_t *u, const int (*knobs)[2], int nk)
{
    int k;
    start_playing(5u, 0);
    for (k = 0; k < nk; k++)
        en_set(knobs[k][0], knobs[k][1]);
    if (u)
        pick_user(l, u);
    render(SECS(10));
    return en_test_peak_in;
}
static void t_extremes(void)
{
    static const int ENDS[3][5][2] = {
        {{P_ENERGY, 80}, {P_MRATE, 100}, {P_NOTES, 3}, {P_ENERGY, 80}, {P_ENERGY, 80}},
        {{P_KEYS, 100}, {P_BRIGHT, 100}, {P_BLOOM, 0}, {P_BASS, 100}, {P_MOTIFS, 100}},
        {{P_MOTIFS, 100}, {P_MTONE, 100}, {P_MOCT, 2}, {P_MRATE, 100}, {P_BASSOCT, 1}},
    };
    int e, l, c;
    float worst = 0.0f, most = 0.0f;
    for (e = 0; e < 3; e++) {
        const float base = song_peak(0, 0, ENDS[e], 5);
        CHECK(!en_test_nans, "first sounds, end %d: %u NaNs", e, en_test_nans);
        for (l = 0; l < EN_NUSER; l++)
            for (c = 0; c < 64; c++) {
                en_user_t u;
                float p, r;
                if (e && (c & 5) != 5 && (c & 5) != 0)   /* at the knobs' ends, a part of the corners */
                    continue;
                en_user_load(l, c & 3, &u);
                corner(&u, l, c);
                p = song_peak(l, &u, ENDS[e], 5);
                r = p / (base > 0.05f ? base : 0.05f);
                CHECK(!en_test_nans, "%s corner %x, end %d: %u NaNs", LN[l], c, e, en_test_nans);
                CHECK(p < PEAK_MOST, "%s corner %x, end %d: %.2f into the limiter", LN[l], c, e, (double)p);
                CHECK(r < PEAK_RATIO, "%s corner %x, end %d: %.2f into the limiter, %.2f the first sounds'", LN[l], c, e, (double)p, (double)base);
                worst = r > worst ? r : worst;
                most = p > most ? p : most;
            }
    }
    printf("  every corner, every knob's end: into the limiter at most %.2f (%.2f times the first sounds')\n", (double)most, (double)worst);
}

/* ---- and a note's level, plain and as heard. On the note and at the moment it counts: a bass where the ear has little of its first sound */
static void t_levels(void)
{
    int l, c;
    for (l = 0; l < EN_NUSER; l++) {
        float first_rms, first_heard, most_rms = -200.0f, most_heard = -200.0f;
        int at_r = 0, at_h = 0;
        note_of(l, 0, SECS(l == EN_U_PAD ? 5.0f : 1.2f));
        {
            const uint32_t from = l == EN_U_PAD ? (uint32_t)(4.0f * FS) : (uint32_t)(0.25f * FS);
            first_rms = db(rms_of(from, 16384u));
            first_heard = db(heard_of(from, 16384u));
        }
        for (c = 0; c < EN_NUP * 27 * 2; c++) {
            en_user_t u;
            int rat = c % 27 < en_user_steps(l, EN_UP_RATIO) ? c % 27 : 0, dep = c / 27 % EN_NUP;
            float r, h;
            en_user_load(l, 0, &u);
            u.v[EN_UP_RATIO] = (uint8_t)rat;
            u.v[EN_UP_DEPTH] = (uint8_t)((en_user_steps(l, EN_UP_DEPTH) - 1) * (dep + 1) / EN_NUP);
            if (c >= EN_NUP * 27) {                  /* the modulation's time at its longest: all the index, all the time */
                u.v[EN_UP_MOD] = (uint8_t)(en_user_steps(l, EN_UP_MOD) - 1);
                if (l == EN_U_PAD)
                    u.v[EN_UP_MOD] = 0u;
            }
            note_of(l, &u, SECS(l == EN_U_PAD ? 5.0f : 1.2f));
            {
                const uint32_t from = l == EN_U_PAD ? (uint32_t)(4.0f * FS) : (uint32_t)(0.25f * FS);
                r = db(rms_of(from, 16384u));
                h = db(heard_of(from, 16384u));
            }
            CHECK(!en_test_nans, "%s: NaN", LN[l]);
            if (r > most_rms)
                most_rms = r, at_r = c;
            if (h > most_heard)
                most_heard = h, at_h = c;
        }
        printf("  %s: first sound %.1f dB, heard %.1f dB; the most of any set: %.1f dB (%+.1f), heard %.1f dB (%+.1f)\n", LN[l], (double)first_rms, (double)first_heard, (double)most_rms, (double)(most_rms - first_rms), (double)most_heard, (double)(most_heard - first_heard));
        CHECK(most_rms - first_rms < RMS_DB, "%s: a set is %.1f dB over its first sound (%d)", LN[l], (double)(most_rms - first_rms), at_r);
        CHECK(most_heard - first_heard < HEARD_DB, "%s: a set is %.1f dB over its first sound as heard (%d)", LN[l], (double)(most_heard - first_heard), at_h);
    }
}

/* ---- a change in mid-note: one at a time, from a set to another, on a held voice */
static void held_up(int l, const en_user_t *u)
{
    static const int8_t CH[4] = {48, 60, 64, 67};
    int k;
    start_engine(3u);
    en_set(P_KEYS, 60);
    en_set(P_MOTIFS, 60);
    en_set(P_BASS, 70);
    en_set(P_DRUMS, 0);
    en_set(P_HATS, 0);
    quiet_room();
    if (l == EN_U_MEL)
        en_set(P_MELMODE, EN_MEL_KEYS);
    pick_user(l, u);
    render(2u);
    if (l == EN_U_KEYS) {
        en_test_pad_off = 1;
        for (k = 0; k < 4; k++)
            en_test_key_note(CH[k], 0.9f);
    } else if (l == EN_U_MEL) {
        en_keys(0x1081u);
    } else if (l == EN_U_BASS) {
        en_test_bass_note(BASS_NOTE + 7, 256u);
    } else {
        en_test_keys_gain = 0.0f;
        en_restore();
    }
    render(SECS(l == EN_U_PAD ? 3.0f : 0.8f));
}
static float before, across, after;              /* (the last change's, to say) */
static float change_ratio(int l, const en_user_t *a, const en_user_t *b)
{
    held_up(l, a);
    ncap = 0u;
    capture(20u);
    before = d2_of(0u, ncap / 2u);
    en_user_set(l, b);
    capture(60u);
    across = d2_of(20u * BLOCK, ncap / 2u);
    capture(60u);
    after = d2_of(80u * BLOCK, ncap / 2u);
    /* (a note going on from the last is what a bass does) */
    return across / ((before > after ? before : after) + CLICK_FLOOR);
}
static void t_no_click(void)
{
    int l, p, off;
    for (l = 0; l < EN_NUSER; l++) {
        float worst = 0.0f;
        for (p = 0; p < EN_NUP; p++) {
            const int n = en_user_steps(l, p);
            static const int TO[3][2] = {{0, 2}, {1, 3}, {2, 3}};
            for (off = 0; off < 3; off++) {
                en_user_t a, b;
                float r;
                en_user_load(l, 0, &a);
                if (p != EN_UP_DEPTH)
                    a.v[EN_UP_DEPTH] = (uint8_t)((en_user_steps(l, EN_UP_DEPTH) - 1) * 6 / 10);   /* an index to step on */
                a.v[EN_UP_DEC] = (uint8_t)(en_user_steps(l, EN_UP_DEC) - 1);
                b = a;
                a.v[p] = (uint8_t)((n - 1) * TO[off][0] / 3);
                b.v[p] = (uint8_t)((n - 1) * TO[off][1] / 3);
                r = change_ratio(l, &a, &b);
                worst = r > worst ? r : worst;
                CHECK(r < CLICK_RATIO, "%s %s %d to %d: %.2f times what either does: %.5f across it, %.5f before, %.5f after", LN[l], en_user_name(l, p), a.v[p], b.v[p], (double)r, (double)across, (double)before, (double)after);
            }
        }
        printf("  a value changed in mid-note, %s: at most %.2f times what the sounds do by themselves\n", LN[l], (double)worst);
    }
}

/* the ratio, on the index's way: how much the sound steps as a ratio goes by, whatever it is: the second difference of the voice over the slide, against the sound's own at the two ends. And from a whole number to another across an inharmonic one, and back */
static void t_ratio_no_steps(void)
{
    static const char *const R[][2] = {{"1", "2"}, {"2", "1.41"}, {"1.41", "3"}, {"3.14", "1"}, {"0.5", "14"}, {"14", "0.25"}};
    int l;
    unsigned i;
    for (l = 0; l < EN_U_PAD; l++) {
        float worst = 0.0f;
        for (i = 0; i < sizeof R / sizeof R[0]; i++) {
            en_user_t a, b;
            float r;
            en_user_load(l, 0, &a);
            a.v[EN_UP_DEPTH] = (uint8_t)((en_user_steps(l, EN_UP_DEPTH) - 1) * 6 / 10);
            a.v[EN_UP_DEC] = (uint8_t)(en_user_steps(l, EN_UP_DEC) - 1);
            b = a;
            a.v[EN_UP_RATIO] = (uint8_t)ratio_step(l, R[i][0]);
            b.v[EN_UP_RATIO] = (uint8_t)ratio_step(l, R[i][1]);
            r = change_ratio(l, &a, &b);
            worst = r > worst ? r : worst;
            CHECK(r < CLICK_RATIO, "%s ratio %s to %s: %.2f times what either does: %.5f across it, %.5f before, %.5f after", LN[l], R[i][0], R[i][1], (double)r, (double)across, (double)before, (double)after);
        }
        printf("  a ratio changed in mid-note, %s: at most %.2f\n", LN[l], (double)worst);
    }
}

/* a ratio moved while the tine is still ringing: the tine's phase is not to step: the voice comes round when that is over */
static void t_ratio_over_the_tine(void)
{
    en_user_t u;
    float b4, acr;
    start_engine(3u);
    en_set(P_KEYS, 60);
    en_set(P_BRIGHT, 100);
    quiet_room();
    en_user_load(EN_U_KEYS, 0, &u);
    pick_user(EN_U_KEYS, &u);
    render(2u);
    en_test_pad_off = 1;
    en_test_key_note(72, 0.9f);
    ncap = 0u;
    capture(9u);
    b4 = d2_of(0u, ncap / 2u);
    u.v[EN_UP_RATIO] = (uint8_t)ratio_step(EN_U_KEYS, "2.76");
    en_user_set(EN_U_KEYS, &u);
    capture(5u);
    acr = d2_of(9u * BLOCK, ncap / 2u);
    printf("  a ratio moved over the tine: %.4f, %.4f before\n", (double)acr, (double)b4);
    CHECK(acr < CLICK_RATIO * b4 + CLICK_FLOOR, "a ratio moved over the tine: %.5f, %.5f before", (double)acr, (double)b4);
}

/* a knob turned fast over a held voice, all the way and back, every value in turn, then all six: nothing steps out of what the sound does, and what is held comes up */
static void t_rapid(void)
{
    int l, p;
    for (l = 0; l < EN_NUSER; l++) {
        float ref = 0.0f, worst = 0.0f;
        en_user_t hi, lo;
        en_user_load(l, 0, &lo);
        lo.v[EN_UP_DEC] = (uint8_t)(en_user_steps(l, EN_UP_DEC) - 1);
        hi = lo;
        hi.v[EN_UP_DEPTH] = (uint8_t)(en_user_steps(l, EN_UP_DEPTH) - 1);
        hi.v[EN_UP_RATIO] = (uint8_t)(en_user_steps(l, EN_UP_RATIO) - 1);
        /* what the sound itself does at its sharpest */
        held_up(l, &hi);
        ncap = 0u;
        capture(40u);
        ref = d2_of(0u, ncap / 2u);
        held_up(l, &lo);
        ncap = 0u;
        for (p = 0; p < 2; p++) {
            const int n = en_user_steps(l, p);
            int s;
            en_user_t u = lo;
            for (s = 0; s < 2 * n; s++) {
                u.v[p] = (uint8_t)(s < n ? s : 2 * n - 1 - s);
                en_user_set(l, &u);
                en_user_hear(l);
                if (ncap + 8u * BLOCK * 2u < sizeof cap / sizeof cap[0])
                    capture(2u);
                else
                    render(2u);
            }
        }
        worst = d2_of(0u, ncap / 2u) / (ref + 1e-6f);
        printf("  a fast turn, %s: at most %.2f times what its sharpest set does by itself\n", LN[l], (double)worst);
        CHECK(worst < CLICK_RATIO, "%s: a fast turn: %.2f times the sharpest set's own", LN[l], (double)worst);
        CHECK(!en_test_nans, "%s: NaN", LN[l]);
    }
    /* in the song: all the layers' values, every few blocks; a ledger that agrees; a rest that lets go; a panic */
    {
        int hands;
        for (hands = 0; hands < 2; hands++) {
            uint32_t s;
            start_playing(55u, hands);
            en_set(P_MRATE, 100);
            en_set(P_NOTES, 3);
            for (l = 0; l < EN_NUSER; l++) {
                en_user_t u;
                en_user_load(l, 0, &u);
                pick_user(l, &u);
            }
            for (s = 0; s < 1500u; s++) {
                en_user_t u;
                l = (int)(s % EN_NUSER);
                en_user_load(l, (int)(s / 64u) % en_user_nfrom(l), &u);
                for (p = 0; p < EN_NUP; p++)
                    u.v[p] = (uint8_t)((s * (unsigned)(p + 3)) % (unsigned)en_user_steps(l, p));
                en_user_set(l, &u);
                en_user_hear(l);
                render(3u);
                if (!(s % 32u)) {
                    CHECK(nd[0] == en_test_keys_down(), "changing: keys %d down, MIDI says %d", en_test_keys_down(), nd[0]);
                    CHECK(nd[1] == en_test_bells_on(), "changing: melody %d down, MIDI says %d", en_test_bells_on(), nd[1]);
                    CHECK(nd[2] == en_test_bass_down(), "changing: bass %d down, MIDI says %d", en_test_bass_down(), nd[2]);
                }
            }
            CHECK(!en_test_nans, "%u NaNs", en_test_nans);
            en_play(0);
            render(SECS(40));
            CHECK(!nd[1] && !nd[2], "at rest: melody %d, bass %d down", nd[1], nd[2]);
            CHECK(!en_test_bell_voices(), "at rest, %d melody voices still sound", en_test_bell_voices());
            en_panic();
            render(4u);
            CHECK(!nd[0] && !nd[1] && !nd[2] && !nd[3], "after a panic: %d %d %d %d down", nd[0], nd[1], nd[2], nd[3]);
            CHECK(!en_test_keys_tails(), "after a panic: %d tails", en_test_keys_tails());
        }
    }
}

/* ---- the keys of Task 11a: four keys held, every value turned over them, released: nothing hangs, a press plays, in Keys and in Keys Arp */
static void t_hand(void)
{
    int mode;
    for (mode = EN_MEL_KEYS; mode <= EN_MEL_KEYS_ARP; mode++) {
        en_user_t u;
        uint32_t s;
        int p;
        start_playing(66u, 1);
        en_set(P_MELMODE, mode);
        en_user_load(EN_U_MEL, EN_TB_GLASS, &u);
        pick_user(EN_U_MEL, &u);
        render(8u);
        en_keys(0x1248u);
        render(4u);
        for (s = 0; s < 600u; s++) {
            p = (int)(s / 32u) % EN_NUP;
            u.v[p] = (uint8_t)((s * 5u) % (unsigned)en_user_steps(EN_U_MEL, p));
            en_user_set(EN_U_MEL, &u);
            render(2u);
            CHECK(nd[1] <= 4, "mode %d: %d keys down", mode, nd[1]);
            if (mode == EN_MEL_KEYS)
                CHECK(nd[1] == 4, "mode %d: %d keys down of the four held", mode, nd[1]);
        }
        en_keys(0u);
        render(SECS(0.5f));
        CHECK(!nd[1], "mode %d: let go, %d down", mode, nd[1]);
        for (p = 0; p < 8; p++) {
            en_keys(1u << p);
            render(3u);
            CHECK(mode != EN_MEL_KEYS || nd[1] == 1, "mode %d: one key, %d down", mode, nd[1]);
            en_keys(0u);
            render(3u);
        }
        render(SECS(1));
        CHECK(!nd[1], "mode %d: after, %d down", mode, nd[1]);
        /* a key held, its decay turned down under it: the player hears it now, not on the next note. And its release, when it comes up */
        if (mode == EN_MEL_KEYS) {
            float was_l, now_l;
            en_user_t h = u;
            start_engine(66u);
            en_set(P_MELMODE, EN_MEL_KEYS);
            quiet_room();
            en_set(P_KEYS, 0);
            en_set(P_BASS, 0);
            en_set(P_DRUMS, 0);
            en_set(P_HATS, 0);
            h.v[EN_UP_DEC] = (uint8_t)(en_user_steps(EN_U_MEL, EN_UP_DEC) - 1);
            pick_user(EN_U_MEL, &h);
            render(4u);
            en_keys(1u << 5);
            ncap = 0u;
            capture(SECS(0.5f));
            was_l = rms_of(ncap / 2u - 4096u, 4096u);
            h.v[EN_UP_DEC] = 0u;
            en_user_set(EN_U_MEL, &h);
            ncap = 0u;
            capture(SECS(0.5f));
            now_l = rms_of(ncap / 2u - 4096u, 4096u);
            CHECK(now_l < 0.1f * was_l, "a key held, its decay turned down: %.1f dB, %.1f dB before", (double)db(now_l), (double)db(was_l));
            en_keys(0u);
            render(4u);
        }
        /* a released long voice is let down, however long its release; a quick release too; and no more than four sound */
        u.v[EN_UP_REL] = (uint8_t)(en_user_steps(EN_U_MEL, EN_UP_REL) - 1);
        en_user_set(EN_U_MEL, &u);
        for (p = 0; p < 12; p++) {
            en_keys(1u << p);
            render(4u);
            en_keys(0u);
            render(4u);
            CHECK(en_test_bell_voices() <= 4, "%d voices", en_test_bell_voices());
        }
        CHECK(!en_test_nans, "mode %d: %u NaNs", mode, en_test_nans);
    }
}

/* ---- no flood: the queue full, in every way, and what was written last is what the engine has. A set caught half written is not taken */
static int same_user(const en_user_t *a, const en_user_t *b) { return !memcmp(a, b, sizeof *a); }
static void t_flood(void)
{
    en_user_t u, got, half;
    int l, i;
    start_playing(9u, 1);
    render(4u);
    /* a queue with no room: not one of a set is lost, nor does it want room */
    for (i = 0; i < 200; i++)
        en_set(P_BLOOM, 40 + i % 3);
    CHECK(en_room() == 0u, "the queue: %u of room", en_room());
    CHECK(en_dropped(), "it was flooded");
    for (l = 0; l < EN_NUSER; l++) {
        en_user_load(l, 1, &u);
        u.v[EN_UP_RATIO] = (uint8_t)(l + 1);
        u.v[EN_UP_DEC] = 1u;
        en_user_set(l, &u);
        CHECK(!en_dropped(), "%s: a set dropped something", LN[l]);
    }
    CHECK(en_room() == 0u, "a set took a place in the queue, or gave one");
    render(1u);
    for (l = 0; l < EN_NUSER; l++) {
        en_user_load(l, 1, &u);
        u.v[EN_UP_RATIO] = (uint8_t)(l + 1);
        u.v[EN_UP_DEC] = 1u;
        en_test_user_get(l, &got);
        CHECK(same_user(&got, &u), "%s: the set did not come through a flood", LN[l]);
    }
    CHECK(en_room() == EN_ROOM_MAX, "the queue: it does not stay full");
    /* the last of many, with no block between: last piece wins */
    for (i = 0; i < 300; i++) {
        en_user_load(EN_U_KEYS, i % 4, &u);
        u.v[EN_UP_DEPTH] = (uint8_t)(i % 101);
        en_user_set(EN_U_KEYS, &u);
    }
    render(1u);
    en_test_user_get(EN_U_KEYS, &got);
    CHECK(same_user(&got, &u), "the last of many: the engine has another");
    /* an audio interrupt in the middle of a write: the render finds the sequence word odd, half of the new values and half of the old, and takes none of them */
    for (l = 0; l < EN_NUSER; l++) {
        en_user_t before;
        en_user_load(l, 2, &u);
        u.v[EN_UP_RATIO] = 3u;
        en_user_set(l, &u);
        render(1u);
        en_test_user_get(l, &before);
        half = before;
        half.from = 0u;
        half.v[EN_UP_RATIO] = (uint8_t)(en_user_steps(l, EN_UP_RATIO) - 1);
        en_test_user_torn(l, &half);
        render(3u);
        en_test_user_get(l, &got);
        CHECK(same_user(&got, &before), "%s: a set half written was taken", LN[l]);
        en_test_user_finish(l, &half);
        render(1u);
        en_test_user_get(l, &got);
        CHECK(same_user(&got, &half), "%s: the set, finished, was not taken", LN[l]);
    }
    /* a set out of range, by whatever way: refused by the render too */
    for (l = 0; l < EN_NUSER; l++) {
        en_user_t before, bad;
        en_test_user_get(l, &before);
        bad = before;
        bad.v[EN_UP_REL] = 255u;
        en_user_set(l, &bad);
        render(1u);
        en_test_user_get(l, &got);
        CHECK(same_user(&got, &before), "%s: a set out of range came in", LN[l]);
    }
    /* a new engine: the first sounds' */
    en_init();
    for (l = 0; l < EN_NUSER; l++) {
        en_user_load(l, 0, &u);
        en_test_user_get(l, &got);
        CHECK(same_user(&got, &u), "%s: en_init left a set", LN[l]);
    }
}

/* ---- mutes, the panic */
static void t_mutes_panic(void)
{
    int l;
    /* a layer off has nothing of its own in the mix, in User, whatever it has been told */
    static const int LAY[EN_NUSER] = {EN_L_KEYS, EN_L_MEL, EN_L_BASS, EN_L_KEYS};
    for (l = 0; l < EN_NUSER; l++) {
        en_user_t u;
        en_user_load(l, 0, &u);
        corner(&u, l, 0x2B);
        note_of(l, &u, 8u);
        en_layer(LAY[l], 0);
        render(SECS(1.5f));
        ncap = 0u;
        {
            const uint32_t was = en_test_user_heard[l];
            en_user_hear(l);
            capture(SECS(1));
            CHECK(en_test_user_heard[l] == was, "%s off: an audition all the same", LN[l]);
        }
        CHECK(rms_of(0u, ncap / 2u) < 1e-5f, "%s off, in User: %.1f dB", LN[l], (double)db(rms_of(0u, ncap / 2u)));
    }
    /* and there's no audition to a layer that's off, with nothing sounding either */
    for (l = 0; l < EN_NUSER; l++) {
        en_user_t u;
        uint32_t was;
        start_engine(77u);
        en_user_load(l, 0, &u);
        u.v[EN_UP_DEC] = 0u;
        pick_user(l, &u);
        en_layer(LAY[l], 0);
        render(SECS(1));
        if (l == EN_U_PAD) {
            en_restore();
            render(SECS(5));
        }
        was = en_test_user_heard[l] + (l == EN_U_MEL ? en_test_mel_heard : 0u);
        en_user_hear(l);
        render(SECS(1));
        CHECK(en_test_user_heard[l] + (l == EN_U_MEL ? en_test_mel_heard : 0u) == was, "%s off, nothing sounding: an audition all the same", LN[l]);
    }
    /* the panic takes all of it now, and comes back as it was asked */
    for (l = 0; l < EN_NUSER; l++) {
        en_user_t u;
        en_user_load(l, 0, &u);
        u.v[EN_UP_REL] = (uint8_t)(en_user_steps(l, EN_UP_REL) - 1);
        start_playing(21u, 0);
        en_user_set(l, &u);
        en_set(en_user_par(l), en_user_value(l));
        render(SECS(8));
        en_user_hear(l);
        en_panic();
        render(2u);
        ncap = 0u;
        capture(SECS(1));
        CHECK(rms_of(0u, ncap / 2u) < 1e-6f, "%s: after a panic, %.1f dB", LN[l], (double)db(rms_of(0u, ncap / 2u)));
        CHECK(en_test_silent(), "%s: silent", LN[l]);
        CHECK(!en_test_keys_tails(), "%s: %d tails after a panic", LN[l], en_test_keys_tails());
        en_restore();
        render(SECS(6));
        CHECK(!en_test_nans, "%s: NaN after a panic", LN[l]);
    }
}

/* ---- the audition: when the layer is silent and on; once for a turn; never on MIDI */
static void t_audition(void)
{
    int l;
    for (l = 0; l < EN_NUSER; l++) {
        en_user_t u;
        uint32_t h, n, was;
        int d;
        start_engine(77u);
        en_set(P_ECHO, 0);
        en_set(P_REVERB, 0);
        en_layer(EN_L_DRUMS, 0);
        en_layer(EN_L_HATS, 0);
        en_user_load(l, 0, &u);
        u.v[EN_UP_ATT] = 0u;
        u.v[EN_UP_DEC] = 0u;
        u.v[EN_UP_REL] = 0u;
        pick_user(l, &u);
        render(2u);
        if (l == EN_U_PAD) {                     /* (it has only its own envelope to wake) */
            en_restore();
            render(SECS(5));
        }
        h = midi_hash;
        n = midi_n;
        was = en_test_user_heard[l] + (l == EN_U_MEL ? en_test_mel_heard : 0u);
        ncap = 0u;
        capture(SECS(0.5f));
        {
            const float quiet = rms_of(0u, ncap / 2u);
            for (d = 0; d < 12; d++) {
                en_user_hear(l);
                capture(2u);
            }
            capture(SECS(1));
            CHECK(rms_of(ncap / 2u - (uint32_t)(1.0f * FS), (uint32_t)(0.8f * FS)) > quiet + 2e-4f, "%s: silent, and a note after a turn: %.1f dB, %.1f dB before", LN[l], (double)db(rms_of(ncap / 2u - (uint32_t)(1.0f * FS), (uint32_t)(0.8f * FS))), (double)db(quiet));
        }
        if (l != EN_U_PAD) {
            CHECK(midi_hash == h && midi_n == n, "%s: an audition on MIDI", LN[l]);
        }
        CHECK(en_test_user_heard[l] + (l == EN_U_MEL ? en_test_mel_heard : 0u) == was + 1u, "%s: %u notes for one turn", LN[l], en_test_user_heard[l] + (l == EN_U_MEL ? en_test_mel_heard : 0u) - was);
    }
    /* a layer playing: not a note more */
    {
        uint32_t was = en_test_user_heard[EN_U_KEYS];
        en_user_t u;
        int k;
        start_engine(77u);
        en_user_load(EN_U_KEYS, 0, &u);
        pick_user(EN_U_KEYS, &u);
        render(2u);
        for (k = 0; k < 3; k++)
            en_test_key_note(60 + 4 * k, 0.8f);
        render(10u);
        en_user_hear(EN_U_KEYS);
        render(SECS(0.5f));
        CHECK(en_test_user_heard[EN_U_KEYS] == was, "the keys sounding, an audition all the same");
    }
    /* an audition's voice is taken by the first note that wants it, softly, and MIDI none the wiser */
    {
        en_user_t u;
        int k;
        start_engine(77u);
        en_user_load(EN_U_KEYS, 0, &u);
        u.v[EN_UP_REL] = (uint8_t)(en_user_steps(EN_U_KEYS, EN_UP_REL) - 1);
        pick_user(EN_U_KEYS, &u);
        render(2u);
        en_user_hear(EN_U_KEYS);
        render(SECS(0.4f));
        CHECK(en_test_keys_tails() == 1, "an audition's voice: %d", en_test_keys_tails());
        CHECK(!en_test_keys_on() && !nd[0], "it is nobody's: %d on, %d down", en_test_keys_on(), nd[0]);
        for (k = 0; k < 12; k++)
            en_test_key_note(50 + k, 0.5f);
        CHECK(!en_test_keys_tails(), "twelve notes, and %d tails", en_test_keys_tails());
    }
}

/* ---- the release: the keys' voice for MIDI ends where it always did; what is left of the sound goes on, on the same voices */
static void t_tails(void)
{
    en_user_t u;
    uint32_t b;
    int most = 0;
    start_engine(3u);
    en_set(P_KEYS, 60);
    quiet_room();
    en_user_load(EN_U_KEYS, 0, &u);
    u.v[EN_UP_REL] = (uint8_t)(en_user_steps(EN_U_KEYS, EN_UP_REL) - 1);
    u.v[EN_UP_ATT] = 3u;
    pick_user(EN_U_KEYS, &u);
    render(2u);
    en_test_pad_off = 1;
    en_test_key_note(60, 0.9f);
    render(SECS(0.5f));
    en_panic();                                  /* (let go: what it does to a key held) */
    render(2u);
    en_restore();
    render(2u);
    CHECK(!en_test_keys_tails(), "a panic left a tail");
    en_test_key_note(60, 0.9f);
    render(SECS(0.5f));
    /* a key's note let go by the song's way: a chord without it */
    en_play(1);
    for (b = 0; b < SECS(60); b++) {
        render(1u);
        if (en_test_keys_tails() > most)
            most = en_test_keys_tails();
        CHECK(!en_test_keys_tails() || en_test_keys_on() + en_test_keys_tails() <= 9, "%d voices and %d tails: no more than the song ever has", en_test_keys_on(), en_test_keys_tails());
    }
    printf("  a long release: at most %d voices go on past what MIDI knows of\n", most);
    CHECK(most > 0, "a long release: the sound never outlived the voice");
    en_play(0);
    /* the overload's six: what was past them goes in about a quarter of a second, tails too */
    en_shed();
    render(SECS(0.6f));
    CHECK(en_test_keys_tails() <= 6, "after a shed: %d tails", en_test_keys_tails());
    /* and they end: however long the release, a voice left to itself is free again, with nothing left of it */
    start_engine(3u);
    en_user_load(EN_U_KEYS, 0, &u);
    u.v[EN_UP_REL] = 3u;
    pick_user(EN_U_KEYS, &u);
    render(2u);
    en_user_hear(EN_U_KEYS);
    render(SECS(0.4f));
    CHECK(en_test_keys_tails() == 1, "an audition's voice: %d", en_test_keys_tails());
    for (b = 0; b < SECS(30) && en_test_keys_tails(); b++)
        render(1u);
    CHECK(!en_test_keys_tails(), "an audition's voice never ended");
    CHECK(!en_test_nans, "%u NaNs", en_test_nans);
}

int main(void)
{
    t_lists();
    t_pinned();
    t_from_is_the_sound();
    t_uses_values();
    t_midi();
    t_extremes();
    t_levels();
    t_no_click();
    t_ratio_no_steps();
    t_ratio_over_the_tine();
    t_rapid();
    t_hand();
    t_flood();
    t_mutes_panic();
    t_audition();
    t_tails();
    if (fails) {
        printf("%d failures\n", fails);
        return 1;
    }
    printf("ok\n");
    return 0;
}
