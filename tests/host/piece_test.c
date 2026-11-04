/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal */
/* The eight pieces: what each recipe says, what each is heard to do, and the change from one to another. It stays in the owner's bounds, a piece has the
 * character its name promises and no two sound alike, measured over minutes of the real engine, and a new piece comes in over the song without a cut: the
 * chord sounding plays out and the next is in the new key, by a new seed. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "amb.h"

static int fails;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); fails++; } } while (0)
#define GAIN 2430u
#define SECONDS(s) ((uint32_t)((s) * 44100.0f) & ~255u)

enum { LOAD_SCREEN, AQUA, SKYLINE, RAINFALL, AURORA, LAGOON, SAVE_POINT, AFTERGLOW };

/* ---- MIDI */
static uint32_t ons[16], drum_on[128], offs[16], all_off;
static int down[16][128], nd[16];
void en_midi_out(uint32_t st, uint32_t d1, uint32_t d2)
{
    const int c = (int)(st & 15u), k = (int)(st & 0xF0u);
    if (st >= 0xF0u)
        return;
    if (k == 0x90 && d2) {
        ons[c]++;
        if (c == 9)
            drum_on[d1]++;
        down[c][d1]++;
        nd[c]++;
    } else if (k == 0x80 || k == 0x90) {
        offs[c]++;
        if (down[c][d1] > 0) {
            down[c][d1]--;
            nd[c]--;
        }
    } else if (k == 0xB0 && d1 == 123u) {
        all_off++;
        memset(down[c], 0, sizeof down[c]);
        nd[c] = 0;
    }
}
static void counts_clear(void)
{
    memset(ons, 0, sizeof ons);
    memset(offs, 0, sizeof offs);
    memset(drum_on, 0, sizeof drum_on);
    all_off = 0u;
}

/* ---- each chord as it starts */
#define CHORDS_MAX 256
static struct {
    uint32_t step, tones;
    int tonic, bars, borrowed;
    char name[16];
} chord[CHORDS_MAX];
static int nchord;
static void on_chord(uint32_t step, const en_gchord_t *g, int character)
{
    (void)character;
    if (nchord < CHORDS_MAX) {
        chord[nchord].step = step;
        chord[nchord].tones = g->ch.tones;
        chord[nchord].tonic = g->ch.tonic;
        chord[nchord].bars = g->bars;
        chord[nchord].borrowed = g->ch.borrowed;
        en_gchord_name(g, chord[nchord].name);
        nchord++;
    }
}

/* ---- as the app does it (project_apply, set_piece) */
static void take_piece(int i, uint32_t seed)
{
    int16_t v[P_NPARAMS];
    uint16_t pl;
    int p;
    en_piece(i, v, &pl);
    for (p = 0; p < P_NPARAMS; p++)
        en_set(p, v[p]);
    en_mask(EN_MASK_ALL);
    en_pool(pl);
    en_arrange(EN_AUTO);       /* (what makes each piece what it is, as the arranger plays it) */
    en_seed(seed);
    en_regrow();
}

/* ---- rendering, measured: a two-pole split at about 150 Hz and 3 kHz, the sides against the middle */
typedef struct {
    double rms, low, high, side;                 /* dB; of the whole for the three shares */
    double peak;
    uint32_t nans;
    double keys, mel, bass, drums, hats;         /* notes a minute */
    uint32_t bars, breaks;
} meas_t;

static int32_t buf[512];
static void measure(uint32_t frames, meas_t *m)
{
    double e = 0.0, lo = 0.0, hi = 0.0, mid = 0.0, side = 0.0, peak = 0.0;
    double l1 = 0.0, l2 = 0.0, h1 = 0.0, h2 = 0.0;
    const double cl = 1.0 - exp(-2.0 * M_PI * 150.0 / 44100.0), ch = 1.0 - exp(-2.0 * M_PI * 3000.0 / 44100.0);
    const double mins = (double)frames / 44100.0 / 60.0;
    const uint32_t step0 = en_test_song();
    uint32_t nans0 = en_test_nans, left = frames, last_b = 0u, brk = 0u;
    counts_clear();
    while (left) {
        const uint32_t k = left < 256u ? left : 256u;
        uint32_t i;
        en_render(buf, k, GAIN);
        for (i = 0; i < 2u * k; i += 2u) {
            const double a = (double)buf[i] / 8388608.0, b = (double)buf[i + 1] / 8388608.0, c = 0.5 * (a + b), d = 0.5 * (a - b);
            double hp;
            e += a * a + b * b;
            mid += c * c;
            side += d * d;
            l1 += cl * (c - l1);
            l2 += cl * (l1 - l2);
            lo += l2 * l2;
            h1 += ch * (c - h1);
            hp = c - h1;
            h2 += ch * (hp - h2);
            hi += (hp - h2) * (hp - h2);
            if (fabs(a) > peak)
                peak = fabs(a);
            if (fabs(b) > peak)
                peak = fabs(b);
        }
        if (en_break_bars && !last_b)
            brk++;
        last_b = en_break_bars;
        left -= k;
    }
    m->rms = 10.0 * log10(e / (2.0 * frames) + 1e-12);
    m->low = 10.0 * log10((lo + 1e-12) / (mid + 1e-12));
    m->high = 10.0 * log10((hi + 1e-12) / (mid + 1e-12));
    m->side = 10.0 * log10((side + 1e-12) / (mid + 1e-12));
    m->peak = peak;
    m->nans = en_test_nans - nans0;
    m->keys = (double)ons[0] / mins;
    m->mel = (double)ons[1] / mins;
    m->bass = (double)ons[2] / mins;
    m->hats = (double)(drum_on[42] + drum_on[46]) / mins;
    m->drums = (double)(drum_on[36] + drum_on[38] + drum_on[37] + drum_on[45]) / mins;
    m->bars = (en_test_song() - step0) / 16u;
    m->breaks = brk;
}

static void play_from_top(int i, uint32_t seed)
{
    en_init();
    take_piece(i, seed);
    en_play(1);
    memset(down, 0, sizeof down);
    memset(nd, 0, sizeof nd);
}

/* ---- the recipes, as numbers */
static void recipes(void)
{
    static const char *const NAME[EN_NPIECES] = {"Load Screen", "Aqua", "Skyline", "Rainfall", "Aurora", "Lagoon", "Save Point", "Afterglow"};
    static const uint8_t STY[EN_NPIECES] = {EN_ST_LIQUID, EN_ST_LIQUID, EN_ST_ROLLER, EN_ST_DRIFT, EN_ST_LOADING, EN_ST_ROLLER, EN_ST_DRIFT, EN_ST_JUNGLE};
    int16_t par[EN_NPIECES][P_NPARAMS];
    uint16_t pl[EN_NPIECES];
    int i, j, p;
    for (i = 0; i < EN_NPIECES; i++) {
        en_piece(i, par[i], &pl[i]);
        CHECK(!strcmp(en_piece_name(i), NAME[i]), "piece %d is %s", i, en_piece_name(i));
        CHECK(par[i][P_STYLE] == STY[i], "%s's style %d", NAME[i], par[i][P_STYLE]);
        CHECK(pl[i] == EN_MODE_POOL[par[i][P_MODE]], "%s's pool is its mode", NAME[i]);
        for (p = 0; p < P_NPARAMS; p++)
            CHECK(par[i][p] >= en_param_info(p)->lo && par[i][p] <= en_param_info(p)->hi, "%s par %d = %d", NAME[i], p, par[i][p]);
        CHECK(par[i][P_TEMPO] >= 150 && par[i][P_TEMPO] <= 180, "%s's tempo %d", NAME[i], par[i][P_TEMPO]);
        /* the player's, not the piece's */
        CHECK(par[i][P_TUNE] == en_param_info(P_TUNE)->def && par[i][P_MIDI] == en_param_info(P_MIDI)->def, "%s has Tune or MIDI of its own", NAME[i]);
        /* where the owner has put the sound, by ear: a break no more active than the style's own, a Jungle a little more active than Liquid and no
         * more; hats no louder to speak of, for crisp and short; the bass never asked to roll past its style; lone notes and bloom for chords that flow */
        CHECK(par[i][P_BREAKS] <= 50, "%s's breaks %d", NAME[i], par[i][P_BREAKS]);
        CHECK(par[i][P_HATS] <= 55, "%s's hats %d", NAME[i], par[i][P_HATS]);
        CHECK(par[i][P_BMOVE] <= 50, "%s's bass move %d", NAME[i], par[i][P_BMOVE]);
        CHECK(par[i][P_NOTES] <= 3 && par[i][P_BLOOM] >= 40, "%s: notes %d, bloom %d", NAME[i], par[i][P_NOTES], par[i][P_BLOOM]);
        CHECK(par[i][P_DRUMS] <= 50, "%s's drums %d", NAME[i], par[i][P_DRUMS]);
        for (j = 0; j < i; j++)
            CHECK(par[i][P_ROOT] != par[j][P_ROOT], "%s and %s are in one key", NAME[i], NAME[j]);
    }
    /* the first is the defaults, all of it */
    for (p = 0; p < P_NPARAMS; p++)
        CHECK(par[LOAD_SCREEN][p] == en_param_info(p)->def, "Load Screen's par %d is not the default", p);
    {
        int d[P_NPARAMS];
        for (p = 0; p < P_NPARAMS; p++)
            d[p] = en_param_info(p)->def;
        CHECK(d[P_TEMPO] == 170 && d[P_ROOT] == 2 && d[P_MODE] == MODE_LYDIAN && d[P_STYLE] == EN_ST_LIQUID, "the defaults themselves moved");
    }
    /* the fastest, strictly */
    for (i = 0; i < EN_NPIECES; i++)
        if (i != SKYLINE)
            CHECK(par[SKYLINE][P_TEMPO] > par[i][P_TEMPO], "Skyline %d, %s %d", par[SKYLINE][P_TEMPO], NAME[i], par[i][P_TEMPO]);
    /* a minor mode: a minor third, no major one; its harmony the minor family's */
    CHECK((pl[AFTERGLOW] >> 3 & 1u) && !(pl[AFTERGLOW] >> 4 & 1u) && en_pool_family(pl[AFTERGLOW]) == EN_FAM_MINOR, "Afterglow is not minor: %03x", pl[AFTERGLOW]);
    CHECK(par[AFTERGLOW][P_MODE] == MODE_AEOLIAN, "Afterglow's mode %d", par[AFTERGLOW][P_MODE]);
    CHECK((pl[AQUA] >> 4 & 1u) && en_pool_family(pl[AQUA]) == EN_FAM_MAJOR, "Aqua is not sunny: %03x", pl[AQUA]);
    /* the knobs that say so */
    for (i = 0; i < EN_NPIECES; i++) {
        if (i != AURORA) {
            CHECK(par[AURORA][P_REVERB] > par[i][P_REVERB], "Aurora's reverb against %s's", NAME[i]);
            CHECK(par[AURORA][P_KEYS] > par[i][P_KEYS], "Aurora's pad against %s's", NAME[i]);
        }
        if (i != LAGOON) {
            CHECK(par[LAGOON][P_BASS] > par[i][P_BASS], "Lagoon's bass against %s's", NAME[i]);
            CHECK(par[LAGOON][P_BRIGHT] < par[i][P_BRIGHT], "Lagoon's brightness against %s's", NAME[i]);
        }
        if (i != SAVE_POINT) {
            CHECK(par[SAVE_POINT][P_DRUMS] < par[i][P_DRUMS], "Save Point's drums against %s's", NAME[i]);
            CHECK(par[SAVE_POINT][P_MOTIFS] < par[i][P_MOTIFS], "Save Point's melody against %s's", NAME[i]);
        }
    }
    /* out of range: the first */
    CHECK(!strcmp(en_piece_name(-1), NAME[0]) && !strcmp(en_piece_name(EN_NPIECES), NAME[0]), "a piece out of range");
}

/* ---- what they are, heard */
#define MINUTES 3.0f
static meas_t heard[EN_NPIECES], longer[EN_NPIECES];
static void listen_to_them(void)
{
    int i, j;
    static const char *const NM[EN_NPIECES] = {"LoadScreen", "Aqua", "Skyline", "Rainfall", "Aurora", "Lagoon", "SavePoint", "Afterglow"};
    /* (the seed of each the same: what differs is the recipe, and two seeds are tried at the end) */
    printf("%-11s %6s %6s %6s %6s %6s  %6s %5s %5s %6s %6s %5s\n", "3 minutes", "rms", "low", "high", "side", "peak", "keys/m", "mel/m", "bass/m", "drum/m", "hats/m", "bars");
    for (i = 0; i < EN_NPIECES; i++) {
        play_from_top(i, 1001u);
        measure(SECONDS(MINUTES * 60.0f), &heard[i]);
        printf("%-11s %6.1f %6.1f %6.1f %6.1f %6.3f  %6.1f %5.1f %6.1f %6.1f %6.1f %5u\n", NM[i], heard[i].rms, heard[i].low, heard[i].high, heard[i].side, heard[i].peak,
               heard[i].keys, heard[i].mel, heard[i].bass, heard[i].drums, heard[i].hats, heard[i].bars);
        CHECK(!heard[i].nans, "%s: NaN in %u blocks", NM[i], heard[i].nans);
        CHECK(heard[i].peak < 0.99, "%s: peak %f", NM[i], heard[i].peak);
    }
    /* long enough for a pass or two: what a piece's rhythm comes to */
    printf("%-11s %6s %6s %6s %6s %6s\n", "16 minutes", "rms", "keys/m", "bass/m", "drum/m", "breaks");
    for (i = 0; i < EN_NPIECES; i++) {
        play_from_top(i, 2002u);
        measure(SECONDS(16.0f * 60.0f), &longer[i]);
        printf("%-11s %6.1f %6.1f %6.1f %6.1f %6u\n", NM[i], longer[i].rms, longer[i].keys, longer[i].bass, longer[i].drums, longer[i].breaks);
        CHECK(longer[i].peak < 0.99, "%s: peak %f", NM[i], longer[i].peak);
    }

    /* Save Point: the quietest, and clearly; the fewest melody notes, the fewest drums */
    for (i = 0; i < EN_NPIECES; i++) {
        if (i == SAVE_POINT)
            continue;
        CHECK(heard[SAVE_POINT].rms < heard[i].rms - 3.0, "Save Point %.1f dB, %s %.1f dB", heard[SAVE_POINT].rms, NM[i], heard[i].rms);
        CHECK(longer[SAVE_POINT].rms < longer[i].rms - 3.0, "16 minutes: Save Point %.1f dB, %s %.1f dB", longer[SAVE_POINT].rms, NM[i], longer[i].rms);
    }
    /* Lagoon: the most of it under 150 Hz of all; and of those with a whole beat (Drift and Loading have hardly a hat in three minutes, so little on top
     * whatever their keys do), the least over 3 kHz: the warmest, as far as a meter can tell */
    for (i = 0; i < EN_NPIECES; i++) {
        if (i == LAGOON)
            continue;
        CHECK(heard[LAGOON].low > heard[i].low, "Lagoon's low %.1f, %s's %.1f", heard[LAGOON].low, NM[i], heard[i].low);
        if (i == LOAD_SCREEN || i == AQUA || i == SKYLINE || i == AFTERGLOW)
            CHECK(heard[LAGOON].high < heard[i].high, "Lagoon's high %.1f, %s's %.1f", heard[LAGOON].high, NM[i], heard[i].high);
    }
    /* Aurora: the widest. The sides are what the reverb and the pad's pan leave */
    for (i = 0; i < EN_NPIECES; i++)
        if (i != AURORA)
            CHECK(heard[AURORA].side > heard[i].side, "Aurora's sides %.1f, %s's %.1f", heard[AURORA].side, NM[i], heard[i].side);
    /* Aqua: over Load Screen, glossy: more on top */
    CHECK(heard[AQUA].high > heard[LOAD_SCREEN].high + 1.5, "Aqua's high %.1f, Load Screen's %.1f", heard[AQUA].high, heard[LOAD_SCREEN].high);
    /* Skyline: the most bars in the same time */
    for (i = 0; i < EN_NPIECES; i++)
        if (i != SKYLINE)
            CHECK(heard[SKYLINE].bars > heard[i].bars, "Skyline's %u bars, %s's %u", heard[SKYLINE].bars, NM[i], heard[i].bars);
    /* Rainfall: a rhythm there is, and sparse: under a third of Load Screen's hits over a long time */
    CHECK(longer[RAINFALL].drums > 0.0 && longer[RAINFALL].drums < longer[LOAD_SCREEN].drums / 3.0, "Rainfall's %.1f hits a minute, Load Screen's %.1f", longer[RAINFALL].drums,
          longer[LOAD_SCREEN].drums);
    CHECK(longer[SAVE_POINT].drums < longer[LOAD_SCREEN].drums / 3.0, "Save Point's %.1f hits a minute", longer[SAVE_POINT].drums);
    /* Afterglow's Jungle: a little more active than Liquid, no more */
    CHECK(longer[AFTERGLOW].drums < longer[LOAD_SCREEN].drums * 1.35, "Afterglow's %.1f hits a minute, Load Screen's %.1f", longer[AFTERGLOW].drums, longer[LOAD_SCREEN].drums);
    /* the breaks: none anywhere much over Liquid's own count, by the owner's ear */
    for (i = 0; i < EN_NPIECES; i++)
        CHECK(longer[i].breaks <= longer[LOAD_SCREEN].breaks + longer[LOAD_SCREEN].breaks / 2u + 2u, "%s has %u breaks in 16 minutes, Load Screen %u", NM[i], longer[i].breaks, longer[LOAD_SCREEN].breaks);
    /* the bass long, Rollers too; chords that flow: nowhere many more strikes than Load Screen's */
    for (i = 0; i < EN_NPIECES; i++) {
        CHECK(longer[i].bass <= longer[LOAD_SCREEN].bass * 1.25, "%s's %.1f bass notes a minute, Load Screen's %.1f", NM[i], longer[i].bass, longer[LOAD_SCREEN].bass);
        CHECK(longer[i].keys <= longer[LOAD_SCREEN].keys * 1.15, "%s's %.1f key notes a minute, Load Screen's %.1f", NM[i], longer[i].keys, longer[LOAD_SCREEN].keys);
    }
    /* no two alike: over all of what a meter has, a distance a person would notice (a dB and a half of level or tone, or a clear part of the rhythm) */
    for (i = 0; i < EN_NPIECES; i++)
        for (j = 0; j < i; j++) {
            const double d = fabs(heard[i].rms - heard[j].rms) / 1.5 + fabs(heard[i].low - heard[j].low) / 1.5 + fabs(heard[i].high - heard[j].high) / 1.5 +
                             fabs(heard[i].side - heard[j].side) / 1.5 + fabs(longer[i].drums - longer[j].drums) / (0.2 * (longer[i].drums + longer[j].drums) + 1.0);
            CHECK(d >= 2.0, "%s and %s measure alike: %.2f", NM[i], NM[j], d);
        }
}

/* ---- from one to the next, in the middle of the song */
static double block_jump(double *prev_l, double *prev_r, uint32_t k)
{
    double most = 0.0;
    uint32_t i;
    for (i = 0; i < k; i++) {
        const double a = (double)buf[2u * i] / 8388608.0, b = (double)buf[2u * i + 1u] / 8388608.0;
        const double da = fabs(a - *prev_l), db = fabs(b - *prev_r);
        if (da > most)
            most = da;
        if (db > most)
            most = db;
        *prev_l = a;
        *prev_r = b;
    }
    return most;
}
static double block_rms(uint32_t k)
{
    double e = 0.0;
    uint32_t i;
    for (i = 0; i < 2u * k; i++) {
        const double a = (double)buf[i] / 8388608.0;
        e += a * a;
    }
    return sqrt(e / (2.0 * k));
}

#define ROOT_OF(i, r) do { int16_t v_[P_NPARAMS]; uint16_t l_; en_piece((i), v_, &l_); (r) = v_[P_ROOT]; } while (0)

static void change_over(int from, int to, uint32_t seed)
{
    /* the render to the change: long enough that there is a whole band of everything sounding */
    double pl = 0.0, pr = 0.0, jump_before = 0.0, rms_before = 0.0, rms_least = 1.0;
    uint32_t i, played, step0, all0;
    int to_root, c0, hc0[EN_NPAD], v;
    int16_t to_par[P_NPARAMS];
    uint16_t to_pool;
    uint32_t blocks = 0u;
    en_piece(to, to_par, &to_pool);
    to_root = to_par[P_ROOT];
    play_from_top(from, seed);
    en_set(P_ENERGY, 100);                       /* (the fullest it gets, to have the most to cut) */
    en_trace_chord = 0;
    played = SECONDS(60.0f);
    for (i = 0; i < played; i += 256u) {
        en_render(buf, 256u, GAIN);
        if (i > played - SECONDS(20.0f)) {
            const double j = block_jump(&pl, &pr, 256u);
            jump_before = j > jump_before ? j : jump_before;
            rms_before += block_rms(256u);
            blocks++;
        } else {
            block_jump(&pl, &pr, 256u);
        }
    }
    rms_before /= (double)blocks;
    /* a chord with a bar and more to go */
    while (en_test_chord_left() < 24u) {
        en_render(buf, 256u, GAIN);
        block_jump(&pl, &pr, 256u);
    }
    nchord = 0;
    en_trace_chord = on_chord;
    counts_clear();
    all0 = all_off;
    step0 = en_test_song();
    c0 = en_chord_root;
    for (v = 0; v < EN_NPAD; v++)
        hc0[v] = en_chord[v];

    take_piece(to, seed ^ 0x51515u);

    /* the first block, where the levels change, and those after */
    {
        const uint32_t left0 = en_test_chord_left();
        uint32_t k;
        double jump_after = 0.0;
        for (k = 0; k < 4000u && !nchord; k++) {
            en_render(buf, 256u, GAIN);
            {
                const double j = block_jump(&pl, &pr, 256u);
                if (k < 1u)
                    jump_after = j;
            }
            /* nothing cut: a full band of sound goes on through it (a tenth of the level, in the worst 6 ms) */
            if (k < 200u) {
                const double r = block_rms(256u);
                rms_least = r < rms_least ? r : rms_least;
            }
            if (!nchord) {
                CHECK(en_chord_root == c0, "%s > %s: the chord's root moved before its time", en_piece_name(from), en_piece_name(to));
                for (v = 0; v < EN_NPAD; v++)
                    CHECK(en_chord[v] == hc0[v], "%s > %s: the hand moved before its time", en_piece_name(from), en_piece_name(to));
            }
        }
        /* the song goes on from there: same grid, not from the top, and the change is the chord's own end */
        CHECK(en_test_song() > step0, "%s > %s: the song went back", en_piece_name(from), en_piece_name(to));
        CHECK(nchord >= 1, "%s > %s: no chord after", en_piece_name(from), en_piece_name(to));
        if (nchord) {
            CHECK(chord[0].step == step0 + left0, "%s > %s: the first new chord on step %u, the old one was to end on %u", en_piece_name(from), en_piece_name(to), chord[0].step, step0 + left0);
            CHECK(!(chord[0].step & 15u), "%s > %s: the new chord is off the bar", en_piece_name(from), en_piece_name(to));
            CHECK(chord[0].tonic == to_root, "%s > %s: the next chord's key is %d, its key %d", en_piece_name(from), en_piece_name(to), chord[0].tonic, to_root);
            CHECK(chord[0].borrowed || !(chord[0].tones & ~(uint32_t)to_pool), "%s > %s: %s (%03x) is out of the mode (%03x)", en_piece_name(from), en_piece_name(to), chord[0].name, chord[0].tones,
                  to_pool);
        }
        /* a click in the block where the levels change would stand out over the song's own loudest edge (the bass alone: bass_tick) */
        printf("%-11s > %-11s jump %.4f, before %.4f; level %.4f, least after %.4f\n", en_piece_name(from), en_piece_name(to), jump_after, jump_before, rms_before, rms_least);
        CHECK(jump_after <= jump_before * 1.5 + 0.03, "%s > %s: a jump of %.4f in the first blocks, %.4f the most before", en_piece_name(from), en_piece_name(to), jump_after, jump_before);
        CHECK(rms_least > rms_before * 0.1, "%s > %s: the sound fell to %.4f from %.4f", en_piece_name(from), en_piece_name(to), rms_least, rms_before);
        /* no one told to stop: the hand stays down up to it; panic's all-off not said */
        CHECK(all_off == all0, "%s > %s: all notes off said", en_piece_name(from), en_piece_name(to));
    }
    /* and it stays in the new key, and plays on: a good while */
    {
        uint32_t k;
        int c;
        const uint32_t nans0 = en_test_nans;
        for (k = 0; k < SECONDS(10.0f); k += 256u)
            en_render(buf, 256u, GAIN);
        for (c = 0; c < nchord; c++)
            CHECK(chord[c].tonic == to_root, "%s > %s: chord %d's key %d", en_piece_name(from), en_piece_name(to), c, chord[c].tonic);
        CHECK(en_test_nans == nans0, "%s > %s: NaN after", en_piece_name(from), en_piece_name(to));
        CHECK(block_rms(256u) > 1e-4, "%s > %s: quiet after", en_piece_name(from), en_piece_name(to));
    }
    en_trace_chord = 0;
}

/* The bass has no filter after it, so a step in its level is a tick: heard by itself, the worst way, wherever its sine is at the time */
static void bass_tick(int from, int to)
{
    double most = 0.0;
    int tr, tried = 0;
    for (tr = 0; tr < 12; tr++) {
        double pl = 0.0, pr = 0.0;
        uint32_t k, s0;
        int l;
        en_init();
        take_piece(from, 300u + (uint32_t)tr);
        en_set(P_ENERGY, 100);
        en_play(1);
        for (l = 0; l < EN_NLAYERS; l++)
            if (l != EN_L_BASS)
                en_layer(l, 0);
        for (k = 0; k < SECONDS(65.0f + 3.1f * (float)tr); k += 256u)
            en_render(buf, 256u, GAIN);
        for (k = 0; k < 2000u && (en_bass_level < 0.1f || en_test_bass_down() < 1 || en_test_chord_left() < 4u); k++)
            en_render(buf, 256u, GAIN);
        block_jump(&pl, &pr, 256u);
        s0 = en_test_song();
        /* (keeping the layers off, this time: not as the app would) */
        {
            int16_t v[P_NPARAMS];
            uint16_t pool;
            en_piece(to, v, &pool);
            en_set(P_BASS, v[P_BASS]);
        }
        for (k = 0; k < 4u; k++) {
            en_render(buf, 256u, GAIN);
            {
                const double j = block_jump(&pl, &pr, 256u);
                if (en_test_song() == s0 && j > most)      /* (no step of the song in it: no note of its own) */
                    most = j;
            }
        }
        tried += en_test_song() == s0;
    }
    printf("the bass alone, %s > %s: its largest step %.4f (%d clean tries)\n", en_piece_name(from), en_piece_name(to), most, tried);
    CHECK(tried >= 3, "%s > %s: only %d tries with the bass alone", en_piece_name(from), en_piece_name(to), tried);
    CHECK(most < 0.02, "%s > %s: the bass ticks, %.4f", en_piece_name(from), en_piece_name(to), most);
}

static void changes(void)
{
    int i, j;
    bass_tick(LOAD_SCREEN, LAGOON);
    bass_tick(LAGOON, SAVE_POINT);
    bass_tick(LAGOON, LOAD_SCREEN);
    /* each to each */
    for (i = 0; i < EN_NPIECES; i++)
        change_over(i, (i + 1) % EN_NPIECES, 4000u + (uint32_t)i);
    for (i = 0; i < EN_NPIECES; i++)
        for (j = 0; j < EN_NPIECES; j++)
            if (i != j && (j + 8 - i) % 8 != 1)
                change_over(i, j, 5000u + (uint32_t)(i * 8 + j));
}

/* ---- a new seed: another song from there on; the same seed: the same */
static void chords_after(int to, uint32_t seed1, uint32_t seed2, char *out, size_t out_n)
{
    uint32_t k;
    int c;
    play_from_top(LOAD_SCREEN, seed1);
    for (k = 0; k < SECONDS(40.0f); k += 256u)
        en_render(buf, 256u, GAIN);
    nchord = 0;
    en_trace_chord = on_chord;
    take_piece(to, seed2);
    for (k = 0; k < SECONDS(90.0f); k += 256u)
        en_render(buf, 256u, GAIN);
    en_trace_chord = 0;
    out[0] = 0;
    for (c = 0; c < nchord; c++) {
        strncat(out, chord[c].name, out_n - strlen(out) - 1u);
        strncat(out, " ", out_n - strlen(out) - 1u);
    }
}
static void seeds(void)
{
    static char a[2048], b[2048], c[2048];
    chords_after(AQUA, 1u, 77u, a, sizeof a);
    chords_after(AQUA, 1u, 77u, b, sizeof b);
    chords_after(AQUA, 1u, 78u, c, sizeof c);
    CHECK(a[0] && !strcmp(a, b), "one seed, two songs: %s / %s", a, b);
    CHECK(strcmp(a, c), "two seeds, one song: %s", a);
    /* the engine has the seed it was given */
    en_init();
    en_seed(0x123456u);
    en_render(buf, 256u, GAIN);
    CHECK(en_test_seed() == 0x123456u, "the seed is %x", en_test_seed());
}

int main(void)
{
    recipes();
    listen_to_them();
    seeds();
    changes();
    if (fails) {
        printf("%d failures\n", fails);
        return 1;
    }
    printf("ok\n");
    return 0;
}
