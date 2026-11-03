/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal */
/* The instrument on the host: the pure parts (scales, chords, voice leading, names) and the
 * render (sound, no NaN, bounded, the cycle, freeze holds). */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include "amb.h"

static int fails;
#define CHECK(c, ...)                                                                                              \
    do {                                                                                                           \
        if (!(c)) {                                                                                                \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);                                                            \
            printf(__VA_ARGS__);                                                                                   \
            printf("\n");                                                                                          \
            fails++;                                                                                               \
        }                                                                                                          \
    } while (0)

static uint32_t midi_count;
void en_midi_out(uint32_t st, uint32_t d1, uint32_t d2)
{
    (void)st;
    (void)d1;
    (void)d2;
    midi_count++;
}

static int32_t buf[2 * 256];
static float peak, sum2;
static int bad;
static void run(float secs)
{
    int blocks = (int)(secs * 44100.0f / 256.0f), b, j;
    for (b = 0; b < blocks; b++) {
        en_render(buf, 256, 2048);
        for (j = 0; j < 512; j++) {
            float x = (float)buf[j] / 8388608.0f;
            if (x != x)
                bad++;
            if (fabsf(x) > peak)
                peak = fabsf(x);
            sum2 += x * x;
        }
    }
}

int main(void)
{
    int pcs[4], n, i;
    char name[16];

    /* the settings: the tempo's range, the recipes in it */
    {
        int16_t par[P_NPARAMS];
        uint16_t pl;
        int pc, p;
        CHECK(en_param_info(P_TEMPO)->lo == 150 && en_param_info(P_TEMPO)->hi == 180, "tempo range");
        for (pc = 0; pc < EN_NPIECES; pc++) {
            en_piece(pc, par, &pl);
            for (p = 0; p < P_NPARAMS; p++)
                CHECK(par[p] >= en_param_info(p)->lo && par[p] <= en_param_info(p)->hi, "piece %d par %d = %d", pc, p, par[p]);
        }
        en_piece(0, par, &pl);
        for (p = 0; p < P_NPARAMS; p++)
            CHECK(par[p] == en_param_info(p)->def, "the first piece is the defaults: par %d", p);
        CHECK(!strcmp(en_piece_name(0), "Aqua") && par[P_TEMPO] == 170 && par[P_ROOT] == 2 && par[P_MODE] == MODE_LYDIAN, "Aqua");
        en_piece(7, par, &pl);
        CHECK(!strcmp(en_piece_name(7), "Afterglow") && par[P_MODE] == MODE_DORIAN, "Afterglow");
    }

    /* scales and chords */
    {
        int st[12];
        CHECK(en_scale(EN_MODE_POOL[MODE_DORIAN], st) == 7 && st[2] == 3 && st[6] == 10, "Dorian steps");
        CHECK(en_scale(EN_MODE_POOL[MODE_PENT_MIN], st) == 5, "pentatonic minor has 5");
        CHECK(en_scale(0, st) == 1, "the root is always in");
    }
    n = en_chord_tones(EN_MODE_POOL[MODE_IONIAN], 0, 0, pcs);
    CHECK(n == 4 && pcs[0] == 0 && pcs[1] == 4 && pcs[2] == 7 && pcs[3] == 11, "C Ionian I = Cmaj7");
    CHECK(!strcmp(en_chord_name(pcs, n, name), "CM7"), "name %s", name);
    n = en_chord_tones(EN_MODE_POOL[MODE_DORIAN], 2, 0, pcs);
    CHECK(!strcmp(en_chord_name(pcs, n, name), "Dm7"), "D Dorian i: %s", name);
    n = en_chord_tones(EN_MODE_POOL[MODE_DORIAN], 2, 3, pcs);
    CHECK(!strcmp(en_chord_name(pcs, n, name), "G7"), "D Dorian IV: %s", name);
    n = en_chord_tones(EN_MODE_POOL[MODE_PENT_MAJ], 0, 0, pcs);
    CHECK(n == 3, "pentatonic chords have three tones (%d)", n);
    n = en_chord_tones(1u | 1u << 4 | 1u << 7 | 1u << 9, 0, 0, pcs);   /* four notes: no doubled tones */
    CHECK(n == 3 && pcs[0] != pcs[1] && pcs[1] != pcs[2] && pcs[0] != pcs[2], "4-note pool: distinct");
    n = en_chord_tones(1u | 1u << 4 | 1u << 7, 0, 1, pcs);
    CHECK(n == 3, "3-note pool: all three (%d)", n);

    /* voice leading: small moves, in range, no unisons */
    {
        int from[4] = {50, 57, 62, 66}, to[4], cm7[4] = {0, 4, 7, 11}, f7[4] = {5, 9, 0, 4}, tot = 0;
        en_voice_lead(from, cm7, 4, to, 48, 77);
        for (i = 0; i < 4; i++) {
            CHECK(to[i] >= 48 && to[i] <= 77, "range %d", to[i]);
            tot += abs(to[i] - from[i]);
        }
        CHECK(tot <= 8, "Cmaj7 from D-ish: moved %d", tot);
        memcpy(from, to, sizeof from);
        en_voice_lead(from, f7, 4, to, 48, 77);
        tot = 0;
        for (i = 0; i < 4; i++)
            tot += abs(to[i] - from[i]);
        CHECK(tot <= 6, "CM7 -> FM7 moved %d", tot);
        CHECK(to[0] != to[1] && to[1] != to[2] && to[2] != to[3] && to[0] != to[3], "no unisons");
    }

    /* texts */
    {
        char t[12];
        en_param_text(P_ETIME, 3, t);
        CHECK(!strcmp(t, "1/4."), "etime: %s", t);   /* (demo-loading) the default place is the dotted quarter */
        en_param_text(P_MOCT, 1, t);
        CHECK(!strcmp(t, "+1"), "motif octave: %s", t);
    }

    /* the render: silent at rest, sound when growing, never NaN, bounded */
    en_init();
    run(1.0f);
    CHECK(peak == 0.0f, "silent before play (%f)", (double)peak);
    en_play(1);
    peak = sum2 = 0.0f;
    run(20.0f);
    CHECK(!bad, "NaN in the output");
    CHECK(peak > 0.05f && peak < 0.99f, "peak %f", (double)peak);   /* (no reason for the numbers but -26 dB is hardly a sound, and full scale is the limiter's) */
    printf("  20 s playing: peak %.3f, rms %.4f, bars %u\n", (double)peak,
           (double)sqrtf(sum2 / (20.0f * 44100.0f * 2.0f)), en_bars / 16u);
    CHECK(en_bars / 16u >= 5u, "the grid ran (%u sixteenths)", en_bars);
    CHECK(en_pad_level > 0.5f, "the pad swelled (%f)", (double)en_pad_level);

    /* the chords move, on and on: each change another root, none longer than four bars */
    {
        uint8_t was;
        int k, changes = 0;
        en_init();
        en_play(1);
        run(0.1f);
        was = en_chord_root;
        for (k = 0; k < 120; k++) {              /* a minute, in half seconds */
            run(0.5f);
            if (en_chord_root != was) {
                changes++;
                was = en_chord_root;
            }
        }
        CHECK(changes >= 10, "the chords: %d changes in a minute", changes);
    }

    /* held: the chord stays, the drums go on */
    {
        int held = en_chord[0] + 128 * en_chord[1], k;
        uint32_t h0 = en_hits;
        en_hold(1);
        for (k = 0; k < 60; k++) {
            run(1.0f);
            CHECK(en_chord[0] + 128 * en_chord[1] == held, "the chord moved while held");
        }
        CHECK(en_hits > h0, "the drums stopped while held");
        en_hold(0);
    }

    /* (demo-loading) a new key and the pool no longer move the voices at once: the next chord takes the new key, and has
     * its own notes whatever the scale. Those two checks do not apply. */
    run(0.1f);                                   /* (the un-freeze arrives) */

    /* the break alone: it sounds, and with its level at 0 it is silent after the block's tail */
    {
        en_init();
        en_set(P_MOTIFS, 0);
        en_set(P_BASS, 0);
        en_set(P_KEYS, 0);
        en_set(P_ECHO, 0);
        en_set(P_REVERB, 0);
        en_play(1);
        run(140.0f);                             /* to the drums */
        peak = 0.0f;
        run(10.0f);
        printf("  break alone: peak %.3f\n", (double)peak);
        CHECK(peak > 0.05f, "the break alone is silent (%f)", (double)peak);
        en_set(P_DRUMS, 0);
        run(0.5f);
        peak = 0.0f;
        run(2.0f);
        printf("  break at 0: peak %g\n", (double)peak);
        CHECK(peak == 0.0f, "the break at 0 still sounds (%g)", (double)peak);
    }

    /* shed, then a new piece: the bells still ring (the limit's restoring is not visible from outside) */
    {
        float gl = 0.0f;
        int k;
        en_init();
        en_play(1);
        en_shed();
        en_regrow();
        bad = 0;
        for (k = 0; k < 100; k++) {              /* the first motif comes at 38 s */
            int l;
            run(0.5f);
            for (l = 0; l < EN_NLOOP; l++)
                gl = en_loop_glow[l] > gl ? en_loop_glow[l] : gl;
        }
        CHECK(!bad && gl > 0.1f, "after a shed and a new piece: NaN %d, bell glow %f", bad, (double)gl);
    }

    /* every setting at its extremes: still bounded */
    {
        int p;
        for (p = 0; p < P_NPARAMS; p++)
            en_set(p, en_param_info(p)->hi);
        en_set(P_MIDI, 1);
        peak = 0.0f;
        run(30.0f);
        CHECK(!bad && peak < 1.0f, "all at max: peak %f", (double)peak);
        for (p = 0; p < P_NPARAMS; p++)
            en_set(p, en_param_info(p)->lo);
        en_set(P_TEMPO, 30);
        run(10.0f);
        CHECK(!bad, "all at min: NaN");
    }

    /* (demo-loading) Bell tone no longer shapes the motifs (each has its fixed timbre) and they are too sparse for this
     * test's 20 s: the Bell tone and Bell oct checks do not apply */
    en_init();
    en_play(1);
    run(20.0f);

    /* a rest: the drums, the bass and the motifs stop, the chord and the pad do not (the rest of it: ctl_test) */
    for (i = 0; i < P_NPARAMS; i++)
        en_set(i, en_param_info(i)->def);
    run(120.0f);                                 /* the bass and the drums have come in */
    en_play(0);
    run(40.0f);
    {
        const uint32_t h0 = en_hits;
        peak = 0.0f;
        run(1.0f);
        CHECK(peak > 0.01f, "silent in a rest (%f)", (double)peak);
        CHECK(en_hits == h0 && en_bass_level == 0.0f, "drums or bass in a rest");
    }
    CHECK(midi_count > 50u, "MIDI out (%u)", midi_count);

    printf(fails ? "amb: %d FAILED\n" : "amb: ok\n", fails);
    return fails != 0;
}
