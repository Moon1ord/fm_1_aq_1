/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal */
/* The buttons and what MIDI says about them. A ledger keeps every note-on and note-off, channel by channel, and after every block it is held
 * against what the engine says is down: a note left hanging or a note-off gone missing shows at once, in any of the five layers. On top of
 * it: the rest, the hold, the next section, the break asked for, the panic and its restore, the shed, MIDI Start, MIDI out off; what the out
 * queue may and may not drop; and that none of it costs the sound anything. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "amb.h"
#include "midi_gate.h"

static int fails;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); fails++; } } while (0)
#define GAIN 2430u

/* ---- the ledger */
#define CH_KEYS 0
#define CH_MOTIFS 1
#define CH_BASS 2
#define CH_PAD 3
#define CH_DRUMS 9
static const int LAYER[5] = {CH_KEYS, CH_MOTIFS, CH_BASS, CH_PAD, CH_DRUMS};
static struct {
    int down[16][128];                           /* said to be down, by channel and note */
    int n[16];                                   /* so many */
    int peak[16];
    uint32_t ons[16], offs[16], all, starts, stops, conts, clocks, sys, cc123;
    uint32_t note_seen[128];                     /* channel 10's: which notes */
    uint32_t burst, burst_most;                  /* messages in one block */
    uint8_t vel_lo[128], vel_hi[128];
} led;
static uint64_t frames;                          /* samples rendered, at the start of the block */
static int ledger_on = 1;                        /* the bound: the most each channel may have down */
static int most_keys = 12;

/* out through the gate and a queue that may stall, if the test asks: what the receiver gets is what counts */
static midi_gate_t gate;
static int q_on;
static uint32_t q_room = 64u, q_dropped_offs, q_dropped;

static uint64_t step_frame;                      /* where the last step started */

/* hooks for one test at a time */
static void (*on_note)(int ch, int note, int vel);

static void led_note_on(int c, int n, int v)
{
    led.down[c][n]++;
    led.n[c]++;
    led.ons[c]++;
    if (led.n[c] > led.peak[c])
        led.peak[c] = led.n[c];
    if (c == CH_DRUMS) {
        if (!led.note_seen[n] || v < led.vel_lo[n])
            led.vel_lo[n] = (uint8_t)v;
        if (v > led.vel_hi[n])
            led.vel_hi[n] = (uint8_t)v;
        led.note_seen[n]++;
    }
    if (on_note)
        on_note(c, n, v);
}
static void led_note_off(int c, int n)
{
    if (led.down[c][n] > 0) {
        led.down[c][n]--;
        led.n[c]--;
    }
    led.offs[c]++;
}
void en_midi_out(uint32_t st, uint32_t d1, uint32_t d2)
{
    const int c = (int)(st & 15u), k = (int)(st & 0xF0u);
    if (q_on) {
        const int off = k == 0x80 || (k == 0xB0 && d1 == 123u);
        if (!midi_gate(&gate, st, d1, d2, q_room))
            return;
        if (!q_room) {
            q_dropped++;
            q_dropped_offs += (uint32_t)off;
            return;
        }
        q_room--;
    }
    led.all++;
    led.burst++;
    if (st >= 0xF0u) {
        led.sys++;
        led.clocks += st == 0xF8u;
        if (st == 0xF8u && led.clocks % 6u == 1u)
            step_frame = frames;
        led.starts += st == 0xFAu;
        led.conts += st == 0xFBu;
        led.stops += st == 0xFCu;
    } else if (k == 0x90 && d2) {
        CHECK(d1 < 128u && d2 < 128u, "a note-on out of range: %u %u", d1, d2);
        led_note_on(c, (int)d1, (int)d2);
    } else if (k == 0x80 || k == 0x90) {
        led_note_off(c, (int)d1);
    } else if (k == 0xB0 && d1 == 123u) {
        memset(led.down[c], 0, sizeof led.down[c]);
        led.n[c] = 0;
        led.cc123++;
    }
}

/* the ledger against the engine's own idea, after a block */
static uint32_t led_bad;
static void led_check(const char *where)
{
    int bad = 0;
    if (!ledger_on)
        return;
    bad |= led.n[CH_KEYS] != en_test_keys_down();
    bad |= led.n[CH_MOTIFS] != en_test_bells_on();
    bad |= led.n[CH_BASS] != en_test_bass_down();
    bad |= led.n[CH_PAD] != en_test_pad_sent();
    bad |= led.n[CH_KEYS] > most_keys || led.n[CH_MOTIFS] > 4 || led.n[CH_BASS] > 1 || led.n[CH_PAD] > EN_NPAD || led.n[CH_DRUMS] > 8;
    if (bad && led_bad++ < 6u)
        printf("FAIL %s: MIDI says keys %d motifs %d bass %d pad %d drums %d; the engine keys %d motifs %d bass %d pad %d\n", where, led.n[CH_KEYS], led.n[CH_MOTIFS], led.n[CH_BASS],
               led.n[CH_PAD], led.n[CH_DRUMS], en_test_keys_down(), en_test_bells_on(), en_test_bass_down(), en_test_pad_sent());
    fails += bad;
}

/* ---- rendering */
static int32_t buf[2048];
static uint32_t ahash;
static double e_sum;
static uint32_t e_n;
static float a_peak;
static int block_len = 256;
static const char *where_now = "?";
static void block(void)
{
    int i;
    led.burst = 0u;
    en_render(buf, (uint32_t)block_len, GAIN);
    frames += (uint64_t)block_len;
    if (led.burst > led.burst_most)
        led.burst_most = led.burst;
    for (i = 0; i < 2 * block_len; i++) {
        const float x = (float)buf[i] * (1.0f / 8388608.0f);
        ahash = (ahash ^ (uint32_t)buf[i]) * 16777619u;
        e_sum += (double)(x * x);
        a_peak = fabsf(x) > a_peak ? fabsf(x) : a_peak;
    }
    e_n += 2u * (uint32_t)block_len;
    led_check(where_now);
}
static void secs(float s)
{
    int n = (int)(s * 44100.0f) / block_len;
    while (n-- > 0)
        block();
}
static float rms_of(float s)
{
    e_sum = 0.0;
    e_n = 0u;
    secs(s);
    return e_n ? (float)sqrt(e_sum / (double)e_n) : 0.0f;
}
static void drain_only(void)                     /* the commands taken, not a step of grid: resting */
{
    static int32_t b[64];
    en_render(b, 16, GAIN);
}

static void start(uint32_t seed, int style, const char *where)
{
    en_trace = 0;
    en_trace_chord = 0;
    en_test_pad_off = 0;
    on_note = 0;
    en_init();
    memset(&led, 0, sizeof led);
    memset(&gate, 0, sizeof gate);
    q_on = 0;
    ledger_on = 1;
    most_keys = 12;
    frames = 0u;
    ahash = 2166136261u;
    a_peak = 0.0f;
    block_len = 256;
    where_now = where;
    en_seed(seed);
    en_set(P_STYLE, style);
    drain_only();
}
static void go(void)
{
    en_play(1);
    block();
}
/* on to the drums' level, by the grid alone, then a bar */
static void to_level(int level)
{
    int i;
    for (i = 0; i < 4000 && en_drum_level != (uint8_t)level; i++)
        en_test_ticks(16u);
    CHECK(en_drum_level == (uint8_t)level, "level %d never came", level);
}
static void to_step(uint32_t step_of_bar)        /* on to this step of a bar */
{
    while (en_test_song() % 16u != step_of_bar)
        en_test_ticks(1u);
}
static void to_bar_pos(uint32_t pos)             /* on to this bar of the phrase, its first step */
{
    to_step(0u);
    while (en_test_song() / 16u % EN_ARR_PHRASE != pos)
        en_test_ticks(16u);
}

/* ---- traces */
#define TR_N 8192
static struct {
    uint32_t step, sixteenth;
    int section, level, brk;
} tr_b[TR_N];
static int ntr_b;
static struct {
    uint32_t step;
    en_gchord_t g;
} tr_c[TR_N];
static int ntr_c;
static void on_bar(uint32_t step, int section, int level, int character, int brk)
{
    (void)character;
    if (ntr_b < TR_N) {
        tr_b[ntr_b].step = step;
        tr_b[ntr_b].sixteenth = en_bars;
        tr_b[ntr_b].section = section;
        tr_b[ntr_b].level = level;
        tr_b[ntr_b].brk = brk;
        ntr_b++;
    }
}
static void on_chord(uint32_t step, const en_gchord_t *g, int character)
{
    (void)character;
    if (ntr_c < TR_N) {
        tr_c[ntr_c].step = step;
        tr_c[ntr_c].g = *g;
        ntr_c++;
    }
}
static void trace_on(void)
{
    ntr_b = ntr_c = 0;
    en_trace = on_bar;
    en_trace_chord = on_chord;
}

/* ---- every style, minutes of it: the ledger holds, all five layers speak, in the notes they should */
static void t_ledger(void)
{
    int st;
    uint32_t burst = 0u;
    for (st = 0; st < EN_NSTYLES; st++) {
        int c;
        start(1001u + (uint32_t)st, st, "plain");
        en_set(P_MRATE, 100);
        go();
        secs(300.0f);
        en_set(P_STYLE, (st + 1) % EN_NSTYLES);
        en_set(P_MOCT, 1);
        secs(60.0f);
        for (c = 0; c < 5; c++) {
            if (st == EN_ST_DRIFT && LAYER[c] == CH_DRUMS && led.ons[CH_DRUMS] < 1u)
                continue;
            CHECK(led.ons[LAYER[c]] > 0u, "%s: nothing on channel %d", EN_STYLE_NAME[st], LAYER[c] + 1);
        }
        CHECK(led.peak[CH_DRUMS] <= 3, "%s: %d drum notes down at once", EN_STYLE_NAME[st], led.peak[CH_DRUMS]);
        CHECK(led.peak[CH_BASS] == 1, "%s: the bass has %d notes down", EN_STYLE_NAME[st], led.peak[CH_BASS]);
        CHECK(led.peak[CH_PAD] <= EN_NPAD && led.peak[CH_PAD] >= 3, "%s: the pad has %d notes down", EN_STYLE_NAME[st], led.peak[CH_PAD]);
        if (led.burst_most > burst)
            burst = led.burst_most;
        printf("  %-8s 6 min: on keys %u motifs %u bass %u pad %u drums %u; most down %d %d %d %d %d; most in a block %u\n", EN_STYLE_NAME[st], led.ons[0], led.ons[1], led.ons[2], led.ons[3], led.ons[9],
               led.peak[0], led.peak[1], led.peak[2], led.peak[3], led.peak[9], led.burst_most);
    }
    CHECK(burst <= 48u, "%u messages in a block: the out queue holds 64", burst);
}

/* channel 10: only the notes it has, at velocities that make sense; a ghost softer than a snare; let go after the gate, whatever the block size */
#define NN 4096
static uint64_t on_at[128];
static uint32_t gate_bad, late_most_hat, late_most_kick;
static void drum_timing(int ch, int note, int vel)
{
    (void)vel;
    if (ch != CH_DRUMS)
        return;
    on_at[note] = frames;
    {
        const uint32_t late = (uint32_t)(frames - step_frame);
        if (note == 42 || note == 46)
            late_most_hat = late > late_most_hat ? late : late_most_hat;
        if (note == 36)
            late_most_kick = late > late_most_kick ? late : late_most_kick;
    }
}
static void t_drums(void)
{
    static const int OK[7] = {36, 38, 37, 45, 42, 46, 0};
    int st, n, i;
    /* a whole style's worth of notes: the mapping */
    for (st = 0; st < EN_NSTYLES; st++) {
        if (st == EN_ST_DRIFT)
            continue;
        start(70u + (uint32_t)st, st, "drums");
        en_set(P_ENERGY, 100);
        go();
        secs(400.0f);
        for (n = 0; n < 128; n++) {
            int ok = 0;
            for (i = 0; OK[i]; i++)
                ok |= OK[i] == n;
            CHECK(ok || !led.note_seen[n], "%s: drum note %d", EN_STYLE_NAME[st], n);
        }
        CHECK(led.note_seen[36] && led.note_seen[38] && led.note_seen[42], "%s: kick %u snare %u hat %u", EN_STYLE_NAME[st], led.note_seen[36], led.note_seen[38], led.note_seen[42]);
        CHECK(led.vel_hi[36] >= 90 && led.vel_hi[38] >= 90, "%s: the loudest kick %d, snare %d", EN_STYLE_NAME[st], led.vel_hi[36], led.vel_hi[38]);
        CHECK(led.vel_lo[38] < led.vel_hi[38] / 2 + 10, "%s: snares %d..%d: nothing soft", EN_STYLE_NAME[st], led.vel_lo[38], led.vel_hi[38]);
        if (st == EN_ST_ROLLER || st == EN_ST_JUNGLE)
            CHECK(led.vel_lo[38] < 50, "%s: the softest of 38 is %d: ghosts", EN_STYLE_NAME[st], led.vel_lo[38]);
    }

    /* with its sound: fine blocks, hats late by their swing, kicks not; each let go, none left */
    start(11u, EN_ST_LIQUID, "drums timing");
    block_len = 32;
    late_most_hat = late_most_kick = gate_bad = 0u;
    on_note = drum_timing;
    go();
    for (i = 0; i < (int)(60.0f * 44100.0f / 32.0f); i++) {
        block();
        for (n = 0; n < 128; n++)
            if (led.down[CH_DRUMS][n] && frames - on_at[n] > 1764u + 64u)
                gate_bad++;
    }
    CHECK(!gate_bad, "a drum note down longer than its gate: %u blocks", gate_bad);
    printf("  drums' lateness on the step: hats up to %u samples, kicks up to %u\n", late_most_hat, late_most_kick);
    CHECK(late_most_hat >= 130u && late_most_hat < 480u, "the hats go out %u samples after the step", late_most_hat);
    CHECK(late_most_kick <= 64u, "a kick goes out %u samples after the step", late_most_kick);

    /* a layer at 0 still sends */
    start(12u, EN_ST_LIQUID, "drums at 0");
    en_set(P_DRUMS, 0);
    en_set(P_KEYS, 0);
    en_set(P_BASS, 0);
    en_set(P_MOTIFS, 0);
    go();
    secs(200.0f);
    CHECK(a_peak == 0.0f || a_peak < 0.001f, "levels 0: peak %f", (double)a_peak);
    CHECK(led.ons[CH_DRUMS] > 100u && led.ons[CH_PAD] > 10u && led.ons[CH_KEYS] > 10u && led.ons[CH_BASS] > 10u, "levels 0: %u %u %u", led.ons[CH_DRUMS], led.ons[CH_PAD], led.ons[CH_KEYS]);
}

/* the pad's channel: the notes that sound, those a chord keeps staying down */
static uint32_t pad_restrike;
static int pad_was[128];
static void t_pad(void)
{
    int i;
    start(3u, EN_ST_LIQUID, "pad");
    trace_on();
    go();
    for (i = 0; i < 40000; i++) {
        const int nc = ntr_c;
        const uint32_t on0 = led.ons[CH_PAD];
        int n;
        memcpy(pad_was, led.down[CH_PAD], sizeof pad_was);
        block();
        if (ntr_c != nc) {
            for (n = 0; n < 4; n++)
                CHECK(led.down[CH_PAD][tr_c[ntr_c - 1].g.pad_v[n]] >= 1, "a pad note is not down at its chord");
            for (n = 0; n < 128; n++)
                if (pad_was[n] && led.down[CH_PAD][n] == pad_was[n] && led.ons[CH_PAD] != on0) {
                    /* (stayed down, not struck: its count did not move. How many new: what moved) */
                }
            CHECK(led.ons[CH_PAD] - on0 <= 4u, "%u pad note-ons on a change", led.ons[CH_PAD] - on0);
        } else if (led.ons[CH_PAD] != on0) {
            pad_restrike++;
        }
    }
    CHECK(!pad_restrike, "pad notes struck away from a change: %u", pad_restrike);
    CHECK(ntr_c > 10, "%d chords", ntr_c);
}

/* MIDI must not alter the sound or the choices */
static void t_midi_costs_nothing(void)
{
    int st;
    for (st = 0; st < EN_NSTYLES; st += 2) {
        uint32_t a, b;
        start(424242u, st, "midi on");
        drain_only();
        go();
        secs(150.0f);
        a = ahash;
        start(424242u, st, "midi off");
        en_set(P_MIDI, 0);
        drain_only();
        ledger_on = 0;
        go();
        secs(150.0f);
        b = ahash;
        CHECK(a == b, "%s: MIDI on %08x, off %08x: MIDI moved the sound", EN_STYLE_NAME[st], a, b);
        CHECK(led.all == 5u && led.cc123 == 5u, "MIDI out off: %u messages besides its all-notes-off", led.all - led.cc123);
    }
}

/* MIDI out off: everything down let go, while it could be said; then nothing, in any way */
static void t_midi_off(void)
{
    int sd;
    for (sd = 0; sd < 12; sd++) {
        uint32_t all;
        int c;
        start(500u + (uint32_t)sd, sd % EN_NSTYLES, "midi off");
        en_set(P_MRATE, 100);
        go();
        secs(90.0f + 7.0f * (float)sd);
        CHECK(led.n[CH_KEYS] + led.n[CH_PAD] > 0, "nothing down before MIDI off");
        en_set(P_MIDI, 0);
        ledger_on = 0;
        drain_only();
        for (c = 0; c < 5; c++)
            CHECK(led.n[LAYER[c]] == 0, "MIDI off: channel %d has %d down", LAYER[c] + 1, led.n[LAYER[c]]);
        all = led.all;
        secs(20.0f);
        en_play(0);
        secs(1.0f);
        en_play(1);
        en_next();
        en_panic();
        secs(1.0f);
        en_restore();
        secs(2.0f);
        CHECK(led.all == all, "MIDI off, %u more messages", led.all - all);
        /* on again: nothing hangs from then on: what it says is a chord at the next change, and the ledger only gains what it can lose */
        en_set(P_MIDI, 1);
        secs(30.0f);
        CHECK(led.n[CH_KEYS] <= 12 && led.n[CH_PAD] <= EN_NPAD && led.n[CH_BASS] <= 1, "on again: %d %d %d", led.n[CH_KEYS], led.n[CH_PAD], led.n[CH_BASS]);
    }
}

/* the out queue: a stall of any length, the busiest settings: a note-off is never lost, and what the receiver has is what the engine has */
static void t_queue(void)
{
    int sd;
    uint32_t held_back = 0u;
    for (sd = 0; sd < 6; sd++) {
        int i;
        start(40u + (uint32_t)sd, EN_ST_ROLLER, "queue");
        ledger_on = 0;                           /* (note-ons may not get through: the receiver's books, below) */
        q_on = 1;
        q_room = 64u;
        q_dropped = q_dropped_offs = 0u;
        en_set(P_ENERGY, 100);
        en_set(P_MRATE, 100);
        go();
        for (i = 0; i < 80000; i++) {
            /* the host takes 16 a millisecond, then, now and then, nothing for a while */
            const int stall = i % 2000 > 1400 + sd * 100;
            if (!stall && q_room < 64u) {
                q_room += 90u;                   /* 5.8 ms of it */
                q_room = q_room > 64u ? 64u : q_room;
            }
            if (!(i % 500))
                en_set(P_TEMPO, 150 + i / 500 % 30);
            block();
        }
        CHECK(!q_dropped_offs, "%u note-offs lost", q_dropped_offs);
        CHECK(!q_dropped, "%u messages lost at the queue: the gate should have held them", q_dropped);
        held_back += gate.owed;
        {
            /* a full stop with the queue as low as the gate lets it be: all of it can still be let go */
            en_panic();
            drain_only();
            CHECK(!q_dropped_offs, "the panic's note-offs lost: %u", q_dropped_offs);
            CHECK(led.n[CH_KEYS] + led.n[CH_MOTIFS] + led.n[CH_BASS] + led.n[CH_PAD] + led.n[CH_DRUMS] == 0, "after the panic the receiver has notes down");
        }
    }
    (void)held_back;
    /* the rule itself: room is kept for what is owed */
    {
        midi_gate_t g;
        int k;
        memset(&g, 0, sizeof g);
        for (k = 0; k < 10; k++)
            CHECK(midi_gate(&g, 0x90u, 40u + (uint32_t)k, 90u, 64u), "note-on %d with room", k);
        CHECK(g.owed == 10u, "owed %u", g.owed);
        CHECK(!midi_gate(&g, 0x90u, 60u, 90u, 16u), "a note-on with only the room owed");
        CHECK(midi_gate(&g, 0x90u, 60u, 90u, 17u), "a note-on with room over");
        CHECK(!midi_gate(&g, 0xF8u, 0u, 0u, 3u), "a clock into the reserve");
        CHECK(midi_gate(&g, 0x80u, 40u, 0u, 0u), "a note-off held back");
        CHECK(g.owed == 10u, "owed %u", g.owed);
        CHECK(!midi_gate(&g, 0x80u, 40u, 0u, 3u), "a note-off not owed, into the reserve");
        CHECK(g.owed == 10u, "owed %u", g.owed);
        CHECK(midi_gate(&g, 0xB0u, 123u, 0u, 0u) && !g.owed, "all notes off: owed %u", g.owed);
    }
}

/* ---- PLAY */
static int chord_key(void) { return en_chord[0] | en_chord[1] << 7 | en_chord[2] << 14 | en_chord[3] << 21; }

static void t_rest(void)
{
    static const float AT[6] = {100.0f, 133.3f, 151.7f, 170.1f, 190.9f, 217.6f};
    int k;
    for (k = 0; k < 6; k++) {
        uint32_t hits, song_at, on_keys, on_bells, on_bass, on_drums, on_pad, bars0;
        int ck, keys_down;
        int sec, lv;
        start(90u + (uint32_t)k, k % 4, "rest");
        en_set(P_MRATE, 100);
        go();
        secs(AT[k]);
        hits = en_hits;
        CHECK(hits > 0u || k == 0, "no drums before the rest");
        ck = chord_key();
        sec = en_section;
        lv = en_drum_level;
        CHECK(led.starts == 1u, "a start: %u", led.starts);
        en_play(0);
        drain_only();
        where_now = "in a rest";
        song_at = en_test_song();
        keys_down = led.n[CH_KEYS];
        bars0 = en_bars;
        CHECK(!en_playing, "still playing");
        CHECK(led.stops == 1u, "MIDI Stop out: %u", led.stops);
        CHECK(led.n[CH_BASS] == 0, "the bass's note is still down");
        CHECK(led.cc123 == 0u, "an all-notes-off in a rest: the chord is still down");
        on_keys = led.ons[CH_KEYS];
        on_bells = led.ons[CH_MOTIFS];
        on_bass = led.ons[CH_BASS];
        on_drums = led.ons[CH_DRUMS];
        on_pad = led.ons[CH_PAD];
        secs(30.0f);
        CHECK(led.ons[CH_KEYS] == on_keys && led.ons[CH_MOTIFS] == on_bells && led.ons[CH_BASS] == on_bass && led.ons[CH_PAD] == on_pad, "notes in a rest: keys %u motifs %u bass %u pad %u",
              led.ons[CH_KEYS] - on_keys, led.ons[CH_MOTIFS] - on_bells, led.ons[CH_BASS] - on_bass, led.ons[CH_PAD] - on_pad);
        CHECK(led.ons[CH_DRUMS] == on_drums, "drums in a rest: %u", led.ons[CH_DRUMS] - on_drums);
        CHECK(led.n[CH_MOTIFS] == 0, "motifs still down after 30 s");
        CHECK(led.n[CH_DRUMS] == 0, "drums still down after 30 s");
        CHECK(led.n[CH_BASS] == 0 && en_bass_level == 0.0f, "the bass after 30 s: %f", (double)en_bass_level);
        CHECK(led.n[CH_KEYS] <= keys_down && led.n[CH_KEYS] >= 3, "the hand after 30 s: %d of %d", led.n[CH_KEYS], keys_down);
        CHECK(led.n[CH_PAD] >= 3, "the pad's notes after 30 s: %d", led.n[CH_PAD]);
        CHECK(en_hits == hits || en_hits == hits, "drums in a rest: %u", en_hits - hits);
        CHECK(chord_key() == ck, "a new chord in a rest");
        CHECK(en_test_song() == song_at && en_bars == bars0, "the grid moved in a rest");
        CHECK(en_section == sec && en_drum_level == lv, "the section moved in a rest");
        CHECK(en_pad_level > 0.5f, "the pad in a rest: %f", (double)en_pad_level);
        /* and growing again: no chord but the one due, MIDI Continue, the same place */
        en_play(1);
        block();
        CHECK(en_playing && led.conts == 1u && led.starts == 1u, "Continue %u, Start %u", led.conts, led.starts);
        CHECK(en_test_song() >= song_at && en_test_song() <= song_at + 2u, "it went on from step %u at %u", song_at, en_test_song());
    }
}

/* a rest in the middle changes nothing that is composed: every section and chord, on the same step */
static void t_rest_same_song(void)
{
    static struct {
        uint32_t step;
        int v[3];
    } a[TR_N];
    static uint8_t ch[TR_N][8];
    static uint32_t ch_step[TR_N];
    int na, nc, i;
    start(77u, EN_ST_LIQUID, "plain song");
    trace_on();
    go();
    secs(300.0f);
    na = ntr_b;
    nc = ntr_c;
    for (i = 0; i < na; i++) {
        a[i].step = tr_b[i].step;
        a[i].v[0] = tr_b[i].section;
        a[i].v[1] = tr_b[i].level;
        a[i].v[2] = tr_b[i].brk;
    }
    for (i = 0; i < nc; i++) {
        ch_step[i] = tr_c[i].step;
        memcpy(ch[i], tr_c[i].g.keys_v, 4);
        memcpy(ch[i] + 4, tr_c[i].g.pad_v, 4);
    }
    start(77u, EN_ST_LIQUID, "rested song");
    trace_on();
    go();
    for (i = 0; i < 9; i++) {
        secs(23.3f + (float)i * 3.17f);
        en_play(0);
        drain_only();
        secs(2.0f + (float)i);
        en_play(1);
        block();
    }
    while (en_test_song() < a[na - 1].step)
        block();
    CHECK(ntr_b >= na && ntr_c >= nc, "bars %d of %d, chords %d of %d", ntr_b, na, ntr_c, nc);
    for (i = 0; i < na && i < ntr_b; i++)
        if (a[i].step != tr_b[i].step || a[i].v[0] != tr_b[i].section || a[i].v[1] != tr_b[i].level || a[i].v[2] != tr_b[i].brk) {
            CHECK(0, "bar %d after rests: step %u/%u section %d/%d level %d/%d break %d/%d", i, a[i].step, tr_b[i].step, a[i].v[0], tr_b[i].section, a[i].v[1], tr_b[i].level, a[i].v[2], tr_b[i].brk);
            break;
        }
    for (i = 0; i < nc && i < ntr_c; i++)
        if (ch_step[i] != tr_c[i].step || memcmp(ch[i], tr_c[i].g.keys_v, 4) || memcmp(ch[i] + 4, tr_c[i].g.pad_v, 4)) {
            CHECK(0, "chord %d after rests: step %u/%u", i, ch_step[i], tr_c[i].step);
            break;
        }
}

/* how it sounds across a rest and out of it: no hole, no attack. The hand and the pad alone, dry: the loudness, 50 ms at a time */
static void level_walk(float s, float *lo, float *jump)
{
    int i, n = (int)(s / 0.05f);
    float was = rms_of(0.05f);
    *lo = was;
    *jump = 1.0f;
    for (i = 0; i < n; i++) {
        const float r = rms_of(0.05f), up = r / (was > 1e-6f ? was : 1e-6f);
        *lo = r < *lo ? r : *lo;
        *jump = up > *jump ? up : *jump;
        was = r;
    }
}
static void t_rest_sound(void)
{
    int k;
    for (k = 0; k < 4; k++) {
        float lo, up;
        const uint32_t on0 = 0u;
        (void)on0;
        start(600u + (uint32_t)k, EN_ST_LIQUID, "rest sound");
        en_set(P_DRUMS, 0);
        en_set(P_MOTIFS, 0);
        en_set(P_BASS, 0);
        en_set(P_ECHO, 0);
        en_set(P_REVERB, 0);
        en_set(P_NOTES, 0);
        go();
        secs(40.0f + 1.31f * (float)k);
        {
            const float before = rms_of(0.2f);
            const uint32_t chords = en_ep_chords, notes = en_ep_notes;
            en_play(0);
            level_walk(20.0f, &lo, &up);
            printf("  a rest: before %.4f, the lowest 50 ms %.4f, the biggest rise in 50 ms %.2f times\n", (double)before, (double)lo, (double)up);
            CHECK(lo > 0.25f * before, "a hole in the rest: %f from %f", (double)lo, (double)before);
            CHECK(up < 1.35f, "an attack in the rest: %.2f times in 50 ms", (double)up);
            CHECK(en_ep_chords == chords && en_ep_notes == notes, "struck in a rest: %u chords, %u notes", en_ep_chords - chords, en_ep_notes - notes);
            /* out of it, to the next chord: nothing struck but what is due */
            en_play(1);
            {
                const uint32_t left = en_test_chord_left();
                int i;
                for (i = 0; i < 3 && left > 16u; i++)
                    block();
                if (left > 16u)
                    CHECK(en_ep_notes == notes, "struck on the way out of a rest: %u", en_ep_notes - notes);
            }
        }
    }
    /* out of a rest the bass does not wait for its figure */
    start(620u, EN_ST_LIQUID, "bass back");
    go();
    to_level(3);
    for (k = 0; k < 24; k++) {
        secs(0.61f + 0.137f * (float)k);
        en_play(0);
        secs(0.3f);
        {
            const uint32_t on = led.ons[CH_BASS];
            en_play(1);
            secs(0.1f);                          /* (within its step) */
            CHECK(led.ons[CH_BASS] > on, "time %d: the bass stays out after a rest", k);
        }
    }
}

/* ---- REC */
static void t_hold(void)
{
    int st;
    for (st = 0; st < EN_NSTYLES; st++) {
        uint32_t hits, song0, notes, on_bass, remain;
        int sec, lv, ck, nc, nb, i, bad_level = 0, bad_sec = 0, kicks_after;
        const int want = st == EN_ST_DRIFT ? 4 : 2;
        start(200u + (uint32_t)st, st, "hold");
        en_set(P_ENERGY, st == EN_ST_DRIFT ? 34 : 100);
        go();
        to_level(want);
        secs(2.0f);
        trace_on();
        en_hold(1);
        block();
        CHECK(en_held, "not held");
        sec = en_section;
        lv = en_drum_level;
        ck = chord_key();
        hits = en_hits;
        song0 = en_test_song();
        notes = en_bass_notes;
        on_bass = led.ons[CH_BASS];
        nc = ntr_c;
        nb = ntr_b;
        for (i = 0; i < 4000; i++) {
            block();
            bad_sec += en_section != sec;
            bad_level += en_drum_level != lv;
            if (i == 2000 && en_held)
                en_set(P_ROOT, 4);               /* a new key waits */
        }
        CHECK(ntr_c == nc, "%s: %d chords while held", EN_STYLE_NAME[st], ntr_c - nc);
        CHECK(!bad_sec && !bad_level, "%s: the section moved %d, the level %d", EN_STYLE_NAME[st], bad_sec, bad_level);
        CHECK(chord_key() == ck, "%s: the chord moved", EN_STYLE_NAME[st]);
        CHECK(en_hits > hits + 10u, "%s: the drums stopped: %u", EN_STYLE_NAME[st], en_hits - hits);
        CHECK(en_bass_notes > notes + 3u && led.ons[CH_BASS] > on_bass + 3u, "%s: the bass stopped: %u", EN_STYLE_NAME[st], en_bass_notes - notes);
        CHECK(en_test_song() > song0 + 200u, "%s: the grid stopped", EN_STYLE_NAME[st]);
        CHECK(ntr_b > nb, "%s: no bars", EN_STYLE_NAME[st]);
        kicks_after = (int)brk_kicks;
        /* let go: a chord within four bars; the section's time as it was */
        remain = en_test_section_bars() - en_test_section_bar();
        en_hold(0);
        block();
        {
            const uint32_t s1 = en_test_song();
            const int was_c = ntr_c;
            uint32_t left = en_test_chord_left();
            while (ntr_c == was_c && en_test_song() < s1 + 4u * 16u + 2u)
                block();
            CHECK(ntr_c > was_c, "%s: no chord after letting go (it had %u steps left)", EN_STYLE_NAME[st], left);
            CHECK(tr_c[ntr_c - 1].g.ch.tonic == 4, "%s: the key that waited is not taken", EN_STYLE_NAME[st]);
            CHECK(en_test_song() >= s1 && en_test_song() <= s1 + left + 2u, "%s: the chord came at %u, it had %u left", EN_STYLE_NAME[st], en_test_song() - s1, left);
            while (en_section == sec && en_test_song() < s1 + remain * 16u + 320u)
                block();
            CHECK(en_section != sec, "%s: the section never ended after letting go", EN_STYLE_NAME[st]);
            {
                const uint32_t took = (en_test_song() - s1) / 16u;
                CHECK(took + 1u >= remain && took <= remain + 1u, "%s: the section went on %u bars, it had %u left", EN_STYLE_NAME[st], took, remain);
            }
        }
        (void)kicks_after;
    }
}

/* the bass over a held chord: tones of the chord, figures not a stuck note, never the leading tone, however it was held */
static uint32_t bass_outside, bass_seen, bass_kinds;
static uint32_t held_tones;
static int held_key;
static void bass_over(int ch, int note, int vel)
{
    (void)vel;
    if (ch != CH_BASS)
        return;
    bass_seen++;
    bass_kinds |= 1u << (note % 12);
    if (!(held_tones >> ((note - held_key + 120) % 12) & 1u))
        bass_outside++;
}
static void t_hold_bass(void)
{
    int sd;
    uint32_t leads_elsewhere = 0u;
    for (sd = 0; sd < 40; sd++) {
        int i;
        start(300u + (uint32_t)sd, sd % 3, "hold bass");
        trace_on();
        go();
        to_level(3);
        /* just where the leading tone is due or sounding: at every place near a chord's end */
        while (en_test_chord_left() > 1u + (uint32_t)sd % 12u)
            en_test_ticks(1u);
        en_hold(1);
        block();
        block();
        held_tones = tr_c[ntr_c - 1].g.ch.tones;
        held_key = tr_c[ntr_c - 1].g.ch.tonic + 0;
        bass_outside = bass_seen = bass_kinds = 0u;
        on_note = bass_over;
        /* (what was asked before the hold may still arrive for a step or two) */
        for (i = 0; i < 80; i++)
            block();
        bass_outside = bass_seen = bass_kinds = 0u;
        for (i = 0; i < 3000; i++)
            block();
        leads_elsewhere += bass_outside;
        CHECK(bass_seen > 20u, "seed %d: %u bass notes", sd, bass_seen);
        CHECK(!bass_outside, "seed %d: %u of %u bass notes outside the held chord", sd, bass_outside, bass_seen);
        CHECK(bass_kinds & (bass_kinds - 1u) || sd % 3 == 3, "seed %d: one note only, a stuck bass", sd);
    }
    (void)leads_elsewhere;
}

/* held for ever: present, no denormals, no runaway voices */
static void hold_long(int rest, float minutes)
{
    int i, m;
    float r0 = 0.0f;
    start(44u, EN_ST_LIQUID, rest ? "long rest" : "long hold");
    en_set(P_DRUMS, 0);
    en_set(P_BASS, 0);
    en_set(P_MOTIFS, 0);
    en_set(P_ECHO, 0);
    en_set(P_REVERB, 0);
    en_set(P_NOTES, rest ? 3 : 0);
    en_test_pad_off = 1;
    go();
    secs(30.0f);
    if (rest)
        en_play(0);
    else
        en_hold(1);
    secs(40.0f);
    for (m = 0; m < (int)minutes; m++) {
        const float r = rms_of(1.0f);
        if (!m)
            r0 = r;
        CHECK(r > 0.012f, "%s, minute %d: the piano is at %f", where_now, m, (double)r);
        CHECK(r < 1.3f * r0 && r > 0.75f * r0, "%s, minute %d: the piano moved to %f from %f", where_now, m, (double)r, (double)r0);
        CHECK(fabsf(en_test_keys_low() - 0.18f) < 1e-4f, "%s, minute %d: a held key is at %g", where_now, m, (double)en_test_keys_low());
        CHECK(en_test_keys_on() >= 3 && en_test_keys_on() <= 6, "%s, minute %d: %d voices", where_now, m, en_test_keys_on());
        for (i = 0; i < 59; i++)
            secs(1.0f);
    }
    printf("  %s: after %.0f minutes the piano alone is at %.4f, %d voices, the quietest key at %.3f\n", where_now, (double)minutes, (double)rms_of(1.0f), en_test_keys_on(), (double)en_test_keys_low());
}

/* ---- EDIT */
static void t_next(void)
{
    int st;
    for (st = 0; st < EN_NSTYLES; st++) {
        int k;
        start(400u + (uint32_t)st, st, "next");
        en_set(P_ENERGY, 100);
        trace_on();
        go();
        secs(1.0f);
        for (k = 0; k < 14; k++) {
            const int nb = ntr_b, sec = en_section;
            const uint32_t six = en_bars;
            int i, c0 = ntr_c, b;
            const float wait = 0.37f + 0.211f * (float)k;
            secs(wait);
            {
                const uint32_t six0 = en_bars, bar_of = tr_b[ntr_b - 1].sixteenth % 16u;
                const int nb0 = ntr_b;
                int want = sec;
                (void)six;
                (void)nb;
                want = en_section;
                c0 = ntr_c;
                en_next();
                for (i = 0; i < 400 && ntr_b == nb0; i++)
                    block();
                b = ntr_b - 1;
                CHECK(ntr_b == nb0 + 1, "the next bar did not come");
                CHECK(tr_b[b].section != want, "%s: on the next bar still section %d", EN_STYLE_NAME[st], want);
                CHECK(tr_b[b].step % (16u * EN_ARR_PHRASE) == 0u, "%s: a section begins at bar %u of a phrase", EN_STYLE_NAME[st], tr_b[b].step / 16u % EN_ARR_PHRASE);
                CHECK(tr_b[b].sixteenth % 16u == bar_of, "%s: the bar lines moved: %u from %u", EN_STYLE_NAME[st], tr_b[b].sixteenth % 16u, bar_of);
                CHECK(tr_b[b].sixteenth - six0 <= 16u, "%s: %u steps from the press to the bar", EN_STYLE_NAME[st], tr_b[b].sixteenth - six0);
                CHECK(ntr_c > c0 && tr_c[ntr_c - 1].step == tr_b[b].step, "%s: no new chord with the section", EN_STYLE_NAME[st]);
                CHECK(tr_b[b].section == (want + 1) % EN_NSECTIONS || (want == EN_SEC_DOWN && tr_b[b].section == EN_SEC_OUT), "%s: section %d after %d", EN_STYLE_NAME[st], tr_b[b].section, want);
            }
        }
    }
    /* the first chord after a section begins lasts as a phrase wants; a phrase of eight bars after it, and a section's length as ever */
    {
        int i, sec;
        start(410u, EN_ST_LIQUID, "next, then");
        trace_on();
        go();
        secs(5.3f);
        en_next();
        secs(2.0f);
        sec = en_section;
        CHECK(sec == EN_SEC_HATS, "section %d", sec);
        {
            const uint32_t bars = en_test_section_bars(), at = en_test_section_bar();
            const int n = ntr_b;
            for (i = 0; i < 40000 && en_section == sec; i++)
                block();
            CHECK(en_section != sec, "the section never ended");
            CHECK((uint32_t)(ntr_b - n) >= bars - at - 1u && (uint32_t)(ntr_b - n) <= bars - at + 1u, "%d bars, it had %u left", ntr_b - n, bars - at);
            CHECK(tr_b[ntr_b - 1].step % (16u * EN_ARR_PHRASE) == 0u, "the section after it starts off the phrase");
        }
    }
    /* held: it still acts; the section moves, the chord stays */
    {
        int k;
        start(420u, EN_ST_LIQUID, "next held");
        trace_on();
        go();
        secs(3.0f);
        en_hold(1);
        block();
        for (k = 0; k < 4; k++) {
            const int sec = en_section, ck = chord_key(), nc = ntr_c;
            en_next();
            secs(1.6f);
            CHECK(en_section == (sec + 1) % EN_NSECTIONS, "held: section %d after %d", en_section, sec);
            CHECK(chord_key() == ck && ntr_c == nc, "held: EDIT moved the chord");
            secs(30.0f);
            CHECK(en_section == (sec + 1) % EN_NSECTIONS, "held: section %d, on its own", en_section);
        }
    }
    /* resting: it waits for growing, and is not done twice */
    {
        int sec;
        start(430u, EN_ST_LIQUID, "next resting");
        go();
        secs(6.0f);
        en_play(0);
        secs(0.5f);
        sec = en_section;
        en_next();
        en_next();
        secs(10.0f);
        CHECK(en_section == sec, "a section moved in a rest");
        en_play(1);
        secs(1.6f);
        CHECK(en_section == sec + 1, "section %d, after %d", en_section, sec);
        secs(1.6f);
        CHECK(en_section == sec + 1, "a second one, section %d", en_section);
    }
}

/* ---- SELECT + EDIT */
static void t_break_pure(void)
{
    uint32_t bar;
    int st;
    for (st = 0; st < EN_NSTYLES; st++)
        for (bar = 0u; bar < 800u; bar++) {
            uint32_t skip = 99u;
            const int len = en_arr_break_now(bar, st, 7u + bar * 3u, &skip);
            const uint32_t pos = bar % EN_ARR_PHRASE;
            CHECK(len >= 1 && len <= 4, "a break of %d bars", len);
            CHECK(pos + skip + (uint32_t)len == EN_ARR_PHRASE, "bar %u: skip %u, length %d: it does not end the phrase", pos, skip, len);
            CHECK(pos < EN_ARR_PHRASE - 4u || skip == 0u, "bar %u: it could run from here, and skips %u", pos, skip);
        }
}

static uint32_t no_drums_yet, breaks_tried;
static void break_case(int style, int level, uint32_t pos, int held, int breaks, uint32_t seed)
{
    int i, b, nb0, first;
    uint32_t six0, bar_of, end;
    start(seed, style, "break");
    en_set(P_ENERGY, 100);
    en_set(P_BREAKS, breaks);
    trace_on();
    go();
    to_level(level);
    to_bar_pos(pos);
    en_test_ticks(3u + seed % 8u);
    if (held)
        en_hold(1);
    block();
    while (ntr_b < 1)
        block();
    nb0 = ntr_b;
    bar_of = tr_b[nb0 - 1].sixteenth % 16u;
    if (en_drum_next < 2u) {
        no_drums_yet++;
        return;                                  /* (the next bar has none: the test below) */
    }
    breaks_tried++;
    six0 = en_bars;
    en_break();
    for (i = 0; i < 400 && ntr_b == nb0; i++)
        block();
    first = ntr_b - 1;
    CHECK(tr_b[first].brk > 0, "style %d level %d bar %u held %d: no break on the next bar (Breaks %d)", style, level, pos, held, breaks);
    CHECK(tr_b[first].sixteenth - six0 <= 16u, "%u steps to the break", tr_b[first].sixteenth - six0);
    CHECK(tr_b[first].sixteenth % 16u == bar_of, "the bar lines moved");
    CHECK(brk_bar.is_break, "style %d level %d bar %u: the bar is not a break (level now %d, its brk %d, song bar %u)", style, level, pos, tr_b[first].level, tr_b[first].brk, tr_b[first].step / 16u % 8u);
    /* to its phrase's end, every bar of it */
    end = (tr_b[first].step / (16u * EN_ARR_PHRASE) + 1u) * 16u * EN_ARR_PHRASE;
    while (en_test_song() < end)
        block();
    for (b = first; b < ntr_b; b++) {
        if (tr_b[b].step >= end) {
            CHECK(tr_b[b].step == end && !tr_b[b].brk, "after the break: step %u, break %d", tr_b[b].step, tr_b[b].brk);
            break;
        }
        CHECK(tr_b[b].brk > 0, "style %d: bar %u of the break is plain", style, tr_b[b].step / 16u % EN_ARR_PHRASE);
        CHECK(tr_b[b].brk <= 4 && tr_b[b].step + (uint32_t)0 < end, "a break of %d", tr_b[b].brk);
    }
    CHECK((end - tr_b[first].step) / 16u <= 4u, "a break of %u bars", (end - tr_b[first].step) / 16u);
    /* the next phrase: exactly on the grid, on a kick */
    while (en_test_song() <= end)
        block();
    {
        int k = ntr_b - 1;
        CHECK(tr_b[k].step == end, "the bar after the break starts at %u, not %u", tr_b[k].step, end);
        CHECK(tr_b[k].level >= 2, "the phrase after the break has level %d", tr_b[k].level);
        CHECK(tr_b[k].sixteenth % 16u == bar_of, "the next phrase is off the bars");
        CHECK(brk_bar.core[0] == 'K', "the next phrase starts on '%c'", brk_bar.core[0]);
        CHECK(!brk_bar.is_break, "the next phrase is a break");
        if (!held) {
            int c, found = 0;
            for (c = 0; c < ntr_c; c++)
                found |= tr_c[c].step == end;
            CHECK(found, "style %d bar %u: no new chord where the next phrase starts: a chord has outlasted its phrase", style, pos);
        }
    }
}

static void t_break(void)
{
    uint32_t pos;
    int st;
    for (st = 0; st < EN_NSTYLES; st++) {
        if (st == EN_ST_DRIFT)
            continue;
        for (pos = 0u; pos < EN_ARR_PHRASE; pos++) {
            break_case(st, 3, pos, 0, 50, 1000u + pos);
            break_case(st, 2, pos, 0, 0, 1100u + pos);
            break_case(st, 3, pos, 1, 50, 1200u + pos);
        }
    }
    for (pos = 0u; pos < EN_ARR_PHRASE; pos++)
        break_case(EN_ST_LIQUID, 4, pos, 0, 50, 1300u + pos);    /* half time: drums there are */
    printf("  breaks asked for: %u, on every bar of a phrase; %u with no drums on the next bar\n", breaks_tried, no_drums_yet);
    CHECK(breaks_tried >= 100u, "only %u breaks were tried", breaks_tried);

    /* no drums: no break, nothing moves */
    {
        static const int LV[2] = {0, 1};
        int l;
        for (l = 0; l < 2; l++) {
            uint32_t song0;
            int i, n;
            start(1400u + (uint32_t)l, EN_ST_LIQUID, "break, no drums");
            trace_on();
            go();
            secs(0.1f);
            if (l) {
                en_next();
                secs(2.0f);
            }
            CHECK(en_drum_level == (uint8_t)LV[l], "level %d, wanted %d", en_drum_level, LV[l]);
            CHECK(en_drum_next < 2u, "the next bar has level %d", en_drum_next);
            song0 = en_test_song();
            en_break();
            n = ntr_b;
            for (i = 0; i < 400; i++)
                block();
            CHECK(ntr_b > n, "no bars");
            for (i = n; i < ntr_b; i++)
                CHECK(!tr_b[i].brk, "a break with no drums");
            CHECK((en_test_song() - song0) / 16u <= 3u + (uint32_t)(ntr_b - n), "the grid jumped without drums");
        }
        /* resting: nothing either, and nothing kept for later */
        start(1410u, EN_ST_LIQUID, "break resting");
        en_set(P_ENERGY, 100);
        trace_on();
        go();
        to_level(3);
        block();
        en_play(0);
        drain_only();
        en_break();
        secs(3.0f);
        {
            const int n = ntr_b;
            int i;
            en_play(1);
            secs(1.5f);
            for (i = n; i < ntr_b; i++)
                CHECK(!tr_b[i].brk || en_section != EN_SEC_FULL || tr_b[i].step % (16u * EN_ARR_PHRASE) >= 4u * 16u, "a break asked for in a rest came after it");
        }
    }

    /* the end of a section, when what follows has no kick: the rule holds, this section has a phrase more */
    {
        int sd, seen = 0;
        for (sd = 0; sd < 30; sd++) {
            uint32_t end;
            int i, sec;
            start(1500u + (uint32_t)sd, EN_ST_LIQUID, "break at the end");
            en_set(P_ENERGY, 100);
            trace_on();
            go();
            to_level(3);
            sec = en_section;
            while (en_section == sec && en_test_section_bar() + EN_ARR_PHRASE + 1u < en_test_section_bars())
                en_test_ticks(16u);
            to_bar_pos(2u);
            block();
            {
                en_break();
                for (i = 0; i < 400 && !en_break_bars; i++)
                    block();
                CHECK(en_break_bars, "seed %d: no break", sd);
                end = (en_test_song() / (16u * EN_ARR_PHRASE) + 1u) * 16u * EN_ARR_PHRASE;
                while (en_test_song() <= end)
                    block();
                CHECK(en_drum_level >= 2u && brk_bar.core[0] == 'K', "seed %d: the break leads to level %d, on '%c'", sd, en_drum_level, brk_bar.core[0]);
                seen++;
            }
        }
        CHECK(seen == 30, "%d", seen);
    }
}

/* a break entered in the middle owes nothing to the last one */
static void t_break_middle(void)
{
    const brk_rhythm_t *r = en_style_rhythm(EN_ST_JUNGLE);
    uint32_t sd;
    int differ_old = 0;
    for (sd = 1u; sd <= 200u; sd++) {
        static brk_gen_t a, b;
        static brk_bar_t ba, bb;
        uint32_t bar;
        brk_gen_start(&a);
        brk_gen_start(&b);
        for (bar = 0u; bar < 16u; bar++) {
            /* a: a whole break in the first phrase's end; b: none. Both then enter one at its second bar */
            const int brk_a = bar >= 6u && bar < 8u;
            if (bar == 15u) {
                brk_gen_bar(&a, r, 3, 70, 50, sd, bar, 2, 1u, &ba);
                brk_gen_bar(&b, r, 3, 70, 50, sd, bar, 2, 1u, &bb);
            } else {
                brk_gen_bar(&a, r, 3, 70, 50, sd, bar, brk_a ? 2 : 0, brk_a ? bar - 6u : 0u, &ba);
                brk_gen_bar(&b, r, 3, 70, 50, sd, bar, 0, 0u, &bb);
            }
        }
        CHECK(!memcmp(&ba, &bb, sizeof ba), "seed %u: a break entered in the middle remembers the last one", sd);
        differ_old += a.brk_hits != b.brk_hits;
    }
    (void)differ_old;
}

/* ---- MIDI clock in */
static void clocks(int n)
{
    int i;
    for (i = 0; i < n; i++) {
        en_clock(EN_CLK_TICK);
        block();
    }
}
static void t_clock(void)
{
    /* Stop rests, Continue grows on from there */
    {
        uint32_t song0;
        int ck;
        start(800u, EN_ST_LIQUID, "clock");
        en_clock(EN_CLK_START);
        block();
        CHECK(en_playing, "Start: not growing");
        clocks(600);
        song0 = en_test_song();
        ck = chord_key();
        en_clock(EN_CLK_STOP);
        block();
        CHECK(!en_playing, "Stop: still growing");
        secs(0.4f);
        CHECK(en_test_song() == song0, "the grid moved after Stop");
        CHECK(chord_key() == ck, "the chord moved after Stop");
        CHECK(led.n[CH_BASS] == 0, "the bass is down after Stop");
        en_clock(EN_CLK_CONTINUE);
        block();
        CHECK(en_playing && en_test_song() == song0, "Continue: playing %d, step %u from %u", en_playing, en_test_song(), song0);
        CHECK(!led.starts && !led.conts && !led.stops, "following a clock, it says transport: %u %u %u", led.starts, led.conts, led.stops);
    }
    /* Start, later: the top of a bar and of a phrase, with the same section and the same chord, nothing struck */
    {
        int k;
        start(810u, EN_ST_LIQUID, "start while playing");
        en_set(P_ENERGY, 100);
        trace_on();
        go();
        to_level(3);
        for (k = 0; k < 8; k++) {
            int ck, sec, nc;
            uint32_t notes, pos_in;
            secs(3.1f + 0.43f * (float)k);
            ck = chord_key();
            sec = en_section;
            nc = ntr_c;
            notes = en_ep_chords;
            pos_in = en_test_section_bar();
            en_clock(EN_CLK_START);
            en_clock(EN_CLK_TICK);
            block();
            CHECK(en_test_song() % (16u * EN_ARR_PHRASE) <= 1u, "Start: step %u of the phrase", en_test_song() % (16u * EN_ARR_PHRASE));
            CHECK(en_section == sec, "Start lost the section: %d from %d", en_section, sec);
            CHECK(chord_key() == ck && ntr_c == nc && en_ep_chords == notes, "Start lost the chord");
            CHECK(en_test_section_bar() <= pos_in, "Start moved on in the section: bar %u from %u", en_test_section_bar(), pos_in);
            CHECK(en_test_section_bar() % EN_ARR_PHRASE == 0u, "Start: bar %u of the section", en_test_section_bar());
            CHECK(en_bars <= 1u, "Start: %u sixteenths", en_bars);
            /* (let the clock lapse, the grid runs on by itself) */
        }
        /* from a rest as well */
        en_play(0);
        secs(1.0f);
        {
            const int ck = chord_key(), sec = en_section;
            en_clock(EN_CLK_START);
            block();
            CHECK(en_playing && chord_key() == ck && en_section == sec, "Start out of a rest: playing %d", en_playing);
        }
        secs(2.0f);
    }
    /* the first Start of all: the song's top */
    {
        start(820u, EN_ST_LIQUID, "first start");
        en_clock(EN_CLK_START);
        block();
        CHECK(en_section == EN_SEC_INTRO && en_test_song() <= 1u, "the first Start: step %u", en_test_song());
    }
}

/* ---- panic and restore */
static float block_peak(void)
{
    int i;
    float p = 0.0f;
    block();
    for (i = 0; i < 2 * block_len; i++) {
        const float x = fabsf((float)buf[i]);
        p = x > p ? x : p;
    }
    return p;
}
static void t_panic(void)
{
    static const float AT[4] = {5.0f, 60.0f, 110.0f, 170.0f};
    int k;
    for (k = 0; k < 8; k++) {
        int i, c;
        uint32_t all, song0;
        const int resting = k & 1;
        start(900u + (uint32_t)k, k % EN_NSTYLES, "panic");
        en_set(P_MRATE, 100);
        go();
        secs(AT[k / 2]);
        if (resting) {
            en_play(0);
            secs(1.0f);
        }
        en_play(0);                              /* (as app_panic) */
        en_panic();
        {
            const float p = block_peak();
            CHECK(p == 0.0f, "the block after a panic: %f", (double)p);
        }
        for (c = 0; c < 5; c++)
            CHECK(led.n[LAYER[c]] == 0, "panic: channel %d has %d down", LAYER[c] + 1, led.n[LAYER[c]]);
        CHECK(en_test_keys_on() == 0 && en_test_bells_on() == 0 && !en_test_bass_down(), "panic: voices still on");
        all = led.all;
        song0 = en_test_song();
        for (i = 0; i < 800; i++) {              /* 4.6 s: long past when the pad used to come back */
            const float p = block_peak();
            if (p != 0.0f) {
                CHECK(0, "panic: sound after %d blocks: %f", i, (double)p);
                break;
            }
            if (i == 100) {                      /* whatever is asked meanwhile */
                en_play(1);
                en_next();
                en_break();
            }
            if (i == 200) {
                en_clock(EN_CLK_START);
                en_clock(EN_CLK_TICK);
                en_clock(EN_CLK_CONTINUE);
            }
        }
        CHECK(en_pad_level == 0.0f, "panic: the pad's level %f", (double)en_pad_level);
        CHECK(!en_playing, "panic: it plays");
        CHECK(led.all == all, "panic: %u more messages", led.all - all);
        CHECK(en_test_song() == song0, "panic: the grid moved");
        /* restored: it grows, from where it was, nothing of the old room, softly */
        en_restore();
        {
            int gone = 0;
            float first = 0.0f;
            for (i = 0; i < 800 && !first; i++)
                first = block_peak();
            gone = i;
            CHECK(en_playing, "restored: not growing");
            CHECK(gone < 800, "restored: silent");
            CHECK(first < 8388608.0f * 0.2f, "restored: it comes back at %f", (double)(first / 8388608.0f));
        }
        CHECK(en_test_song() >= song0 && en_test_song() < song0 + 200u, "restored: step %u from %u", en_test_song(), song0);
        CHECK(led.n[CH_PAD] >= 3, "restored: the pad's notes %d", led.n[CH_PAD]);
        secs(0.2f);
        CHECK(led.n[CH_KEYS] >= 3, "restored: no hand on the first step: %d keys", led.n[CH_KEYS]);
        secs(0.3f);
        CHECK(en_pad_level > 0.0f && en_pad_level < 0.4f, "restored: half a second on the pad is at %f: not from silence", (double)en_pad_level);
        secs(8.0f);
        CHECK(en_pad_level > 0.5f, "restored: the pad %f", (double)en_pad_level);
        CHECK(en_test_keys_on() >= 3 && led.n[CH_KEYS] >= 3, "restored: the hand: %d %d", en_test_keys_on(), led.n[CH_KEYS]);
        secs(4.0f);
        CHECK(!led.stops || led.conts + led.starts >= 1u, "restored: MIDI not told");
    }
    /* before the first step: the first restore is the start */
    start(950u, EN_ST_LIQUID, "first restore");
    en_restore();
    block();
    CHECK(en_playing && led.starts == 1u && !led.conts, "the first en_restore: playing %d, Start %u, Continue %u", en_playing, led.starts, led.conts);
    /* a room cleared: what rang before does not ring after */
    {
        float quiet_p = 0.0f;
        int i;
        start(960u, EN_ST_LIQUID, "room");
        en_set(P_ECHO, 100);
        en_set(P_REVERB, 100);
        en_set(P_SIZE, 100);
        go();
        secs(60.0f);
        en_set(P_KEYS, 0);
        en_set(P_MOTIFS, 0);
        en_set(P_BASS, 0);
        en_set(P_DRUMS, 0);
        en_panic();
        block();
        en_restore();                            /* at once: the room is cleared first all the same */
        for (i = 0; i < 400; i++) {
            const float p = block_peak();
            quiet_p = p > quiet_p ? p : quiet_p;
        }
        CHECK(quiet_p == 0.0f, "the old room came back: %f", (double)(quiet_p / 8388608.0f));
    }
}

/* ---- the shed */
static void t_shed(void)
{
    int sd;
    for (sd = 0; sd < 16; sd++) {
        int i, before;
        float was_l, biggest = 0.0f;
        start(1600u + (uint32_t)sd, sd % 3, "shed");
        en_set(P_DRUMS, 0);
        en_set(P_ECHO, 0);
        en_set(P_REVERB, 0);
        en_test_pad_off = 1;
        en_set(P_BASS, 0);
        go();
        /* a moment of many voices: a chord just changed, what it let go still ringing, a lone note or two */
        secs(30.0f + (float)sd);
        {
            int most = 0;
            for (i = 0; i < 1000 && most < 7; i++) {
                block();
                most = en_test_keys_on();
            }
        }
        for (i = 0; i < 20; i++)                 /* (what is yet to start has started) */
            block();
        before = en_test_keys_on();
        was_l = (float)buf[2 * block_len - 2] * (1.0f / 8388608.0f);
        en_shed();
        led_check("just shed");
        CHECK(en_test_keys_limit() == 6, "a shed: the limit is %d", en_test_keys_limit());
        CHECK(en_test_keys_on() == before, "a shed cut voices dead: %d of %d", en_test_keys_on(), before);
        for (i = 0; i < 16; i++) {
            int j;
            block();
            for (j = 0; j < block_len; j++) {
                const float x = (float)buf[2 * j] * (1.0f / 8388608.0f), d = fabsf(x - was_l);
                biggest = d > biggest ? d : biggest;
                was_l = x;
            }
        }
        CHECK(biggest < 0.09f, "a shed clicks: a step of %f", (double)biggest);
        secs(0.5f);
        CHECK(en_test_keys_on() <= 6, "shed: %d voices after half a second (%d before)", en_test_keys_on(), before);
        secs(0.1f);
        {
            /* the hand is still there, whole */
            CHECK(en_test_keys_down() >= 3, "shed: the hand lost: %d keys down", en_test_keys_down());
        }
        most_keys = 6;
        secs(60.0f);
        CHECK(en_ep_peak <= 12, "%d", en_ep_peak);
        most_keys = 12;
        {
            int on = en_test_keys_on();
            CHECK(on <= 6, "shed, a minute on: %d voices", on);
        }
        /* a new piece: all twelve again, and nothing that had gone comes back */
        {
            const int on = en_test_keys_on();
            en_regrow();
            block();
            CHECK(en_test_keys_limit() == 12, "a new piece: the limit is %d", en_test_keys_limit());
            CHECK(en_test_keys_on() <= on, "a new piece brought voices back: %d from %d", en_test_keys_on(), on);
            CHECK(led.n[CH_KEYS] == en_test_keys_down(), "a new piece: MIDI %d, voices %d", led.n[CH_KEYS], en_test_keys_down());
        }
        secs(20.0f);
    }
    /* a lone note's key down past the limit: it comes up, MIDI told, and its voice lets go */
    {
        int sd2, found = 0;
        for (sd2 = 0; sd2 < 12 && !found; sd2++) {
            int i;
            start(1800u + (uint32_t)sd2, EN_ST_LIQUID, "shed, a lone note");
            en_set(P_MRATE, 0);
            go();
            for (i = 0; i < 40000 && !en_test_keys_down_past(6); i++)
                block();
            if (en_test_keys_down_past(6)) {
                found = 1;
                en_shed();
                led_check("a lone note shed");
                CHECK(en_test_keys_down_past(6) == 0, "shed: a key still down past the limit");
            }
        }
        CHECK(found, "never a key down past six voices");
    }
    /* many voices at once, for sure: a shed lets the ones past six go, with their note-offs */
    {
        uint32_t offs;
        start(1700u, EN_ST_LIQUID, "shed of a crowd");
        en_set(P_BLOOM, 100);
        en_set(P_NOTES, 3);
        go();
        {
            int i, most = 0;
            for (i = 0; i < 60000 && most < 9; i++) {
                block();
                most = en_test_keys_on();
            }
            CHECK(most >= 9, "never %d voices", most);
        }
        offs = led.offs[CH_KEYS];
        en_shed();
        (void)offs;
        led_check("crowd shed");
        secs(0.5f);
        CHECK(en_test_keys_on() <= 6, "a crowd shed: %d voices", en_test_keys_on());
    }
}

/* ---- the review's list: what no test looked at */

/* the style moved in the middle of a section: the bar playing stays what it is, the next is the new style's */
static void t_style_mid(void)
{
    uint32_t sd;
    int differ = 0;
    for (sd = 1u; sd <= 40u; sd++) {
        static brk_bar_t cur_a, nxt_a, nxt_b;
        int i;
        start(sd, EN_ST_LIQUID, "style");
        en_set(P_ENERGY, 100);
        drain_only();
        en_test_ticks(1u);
        to_level(3);
        to_step(5u);
        cur_a = brk_bar;
        en_test_ticks(12u);
        nxt_a = brk_bar;
        start(sd, EN_ST_LIQUID, "style");
        en_set(P_ENERGY, 100);
        drain_only();
        en_test_ticks(1u);
        to_level(3);
        to_step(5u);
        en_set(P_STYLE, EN_ST_JUNGLE);
        drain_only();
        CHECK(en_style == EN_ST_JUNGLE, "the screen's style");
        for (i = 0; i < 11; i++)
            en_test_ticks(1u);
        CHECK(!memcmp(&cur_a, &brk_bar, sizeof cur_a), "seed %u: the style moved the bar under way", sd);
        en_test_ticks(1u);
        nxt_b = brk_bar;
        differ += memcmp(&nxt_a, &nxt_b, sizeof nxt_a) != 0;
    }
    CHECK(differ >= 30, "the next bar is the new style's in only %d of 40", differ);
}

/* a note the next chord also has is not played again */
static int on_block[128];
static void note_on_block(int ch, int note, int vel)
{
    (void)vel;
    if (ch == CH_KEYS)
        on_block[note]++;
}
static void t_common(void)
{
    uint32_t sd;
    uint32_t kept = 0u;
    for (sd = 1u; sd <= 6u; sd++) {
        int i;
        start(sd * 31u, (int)sd % 3, "common");
        trace_on();
        on_note = note_on_block;
        go();
        for (i = 0; i < 30000; i++) {
            const int nc = ntr_c;
            memset(on_block, 0, sizeof on_block);
            block();
            if (ntr_c == nc + 1 && nc > 0) {
                const en_gchord_t *a = &tr_c[nc - 1].g, *b = &tr_c[nc].g;
                int x, y;
                for (x = 0; x < 4; x++)
                    for (y = 0; y < 4; y++)
                        if (a->keys_v[x] == b->keys_v[y]) {
                            kept++;
                            CHECK(!on_block[b->keys_v[y]], "seed %u chord %d: note %d, kept, was struck again", sd, nc, b->keys_v[y]);
                        }
            }
        }
    }
    CHECK(kept > 200u, "only %u notes kept", kept);
}

/* at the keys all the time, by key and by mode: the bass's notes are of the chord playing or of the scale it was made in, and into a new key
 * no leading tone */
static uint32_t lead_bad;
static uint32_t lead_pool, lead_now;             /* the scale of the chord playing; the scale as it is now */
static int lead_at_chord;
static void lead_note(int ch, int note, int vel)
{
    (void)vel;
    if (ch != CH_BASS || !ntr_c)
        return;
    if (ntr_c != lead_at_chord) {
        lead_at_chord = ntr_c;
        lead_pool = lead_now;
    }
    {
        const en_hchord_t *c = &tr_c[ntr_c - 1].g.ch;
        const int pc = (note - c->tonic + 120) % 12;
        if (!(c->tones >> pc & 1u) && !(lead_pool >> pc & 1u))
            lead_bad++;
    }
}
static void t_bass_lead(void)
{
    static const uint32_t POOLS[4] = {0xAD5u, 0x5ADu, 0x6ADu, 0xAB5u};
    uint32_t sd;
    for (sd = 1u; sd <= 12u; sd++) {
        int i, seen;
        uint32_t pool_now = POOLS[0];
        int key_now = 2;
        start(sd * 53u, EN_ST_LIQUID, "lead");
        en_set(P_ENERGY, 100);
        trace_on();
        en_pool(pool_now);
        go();
        to_level(3);
        seen = ntr_c;
        lead_bad = 0u;
        on_note = lead_note;
        lead_at_chord = ntr_c;
        lead_pool = lead_now = pool_now;
        for (i = 0; i < 30000; i++) {
            if (en_test_chord_left() <= 12u && !(i % 97)) {
                if (sd & 1u) {
                    pool_now = POOLS[i / 97 % 4];
                    lead_now = pool_now;
                    en_pool(pool_now);
                } else {
                    key_now = (key_now + 5) % 12;
                    en_set(P_ROOT, key_now);
                }
            }
            block();
        }
        seen = ntr_c - seen;
        CHECK(seen > 30, "%d chords", seen);
        CHECK(!lead_bad, "seed %u: %u bass notes outside the chord and its scale", sd, lead_bad);
    }
}

/* a mode changed in a rest, just before the chord was due: the next chord is a new one, and of that mode */
static void t_mode_in_rest(void)
{
    uint32_t sd;
    for (sd = 1u; sd <= 30u; sd++) {
        static const uint32_t DOR = 0x6ADu;
        int nc;
        start(sd * 17u, EN_ST_LIQUID, "mode in a rest");
        trace_on();
        go();
        en_test_ticks(200u + sd * 7u);
        while (en_test_chord_left() > 7u)
            en_test_ticks(1u);
        block();
        en_play(0);
        drain_only();
        nc = ntr_c;
        {
            const int ck = chord_key();
            en_pool(DOR);
            en_set(P_MODE, 1);
            secs(0.5f);
            CHECK(ntr_c == nc && chord_key() == ck, "seed %u: a new chord in a rest", sd);
        }
        en_play(1);
        while (ntr_c < nc + 3)
            block();
        {
            const en_gchord_t *a = &tr_c[nc - 1].g, *b = &tr_c[nc].g, *c = &tr_c[nc + 1].g;
            CHECK(memcmp(a->keys_v, b->keys_v, 4) || a->ch.root != b->ch.root, "seed %u: the same chord twice over the rest", sd);
            CHECK(memcmp(c->keys_v, b->keys_v, 4) || c->ch.root != b->ch.root, "seed %u: the same chord twice after the rest", sd);
            CHECK(!b->ch.borrowed ? (uint32_t)(b->ch.tones & ~DOR) == 0u : 1, "seed %u: tones %03x, the new scale's not", sd, b->ch.tones);
        }
    }
}

/* whatever en_set is given, it comes out as the nearest value it has; a parameter there is not does nothing */
static void t_out_of_range(void)
{
    static const struct {
        int p, v, same;
    } T[] = {
        {P_KEYS, 100000, 100}, {P_KEYS, 101, 100}, {P_KEYS, -1, 0}, {P_KEYS, -70000, 0}, {P_BASSOCT, 99, 1}, {P_MOCT, -99, -2}, {P_TEMPO, 1, 150}, {P_TEMPO, 40000, 180}, {P_STYLE, 77, EN_NSTYLES - 1},
        {P_ETIME, 100, 5}, {P_NOTES, -3, 0}, {P_MODE, 40, EN_NMODES - 1}, {P_ROOT, 12, 11}, {P_MIDI, 5, 1}, {P_KIT, 2, 1},
    };
    unsigned i;
    for (i = 0; i < sizeof T / sizeof T[0]; i++) {
        uint32_t a, b;
        start(21u, EN_ST_LIQUID, "range");
        en_set(T[i].p, T[i].v);
        go();
        secs(6.0f);
        a = ahash;
        start(21u, EN_ST_LIQUID, "range");
        en_set(T[i].p, T[i].same);
        go();
        secs(6.0f);
        b = ahash;
        CHECK(a == b, "en_set(%s, %d) is not en_set(%s, %d)", en_param_info(T[i].p)->name, T[i].v, en_param_info(T[i].p)->name, T[i].same);
    }
    {
        uint32_t a, b;
        start(22u, EN_ST_LIQUID, "range");
        go();
        secs(4.0f);
        a = ahash;
        start(22u, EN_ST_LIQUID, "range");
        en_set(-1, 50);
        en_set(P_NPARAMS, 50);
        en_set(P_NPARAMS + 256, 50);
        en_set(256 + P_KEYS, 0);
        go();
        secs(4.0f);
        b = ahash;
        CHECK(a == b, "a parameter that does not exist did something");
    }
}

/* ten changes in a drain compose one chord */
static void t_once(void)
{
    uint32_t g0;
    int i;
    start(23u, EN_ST_LIQUID, "once");
    g0 = en_test_gens;
    for (i = 0; i < 10; i++)
        en_pool(i & 1 ? 0xAD5u : 0x6ADu);
    drain_only();
    CHECK(en_test_gens - g0 == 1u, "%u chords composed for ten commands", en_test_gens - g0);
    g0 = en_test_gens;
    for (i = 0; i < 5; i++)
        en_set(P_ROOT, i);
    drain_only();
    CHECK(en_test_gens - g0 == 1u, "%u chords composed for five keys", en_test_gens - g0);
    /* and the last word is what counts: the same as if said once */
    {
        char a[16], b[16];
        en_gchord_t g;
        en_gen_t st;
        en_pool(0x6ADu);
        en_set(P_ROOT, 7);
        drain_only();
        en_gen_start(&st);
        en_gen_next(&st, 0x6ADu, 7, 23u, 50, 0, EN_MASK_ALL, &g);
        (void)a;
        (void)b;
        CHECK(en_chord_root == (uint8_t)((7 + g.ch.root) % 12), "the chord at the top of the song: root %d", en_chord_root);
    }
    /* in a rest, none at all: the next chord takes it */
    go();
    secs(3.0f);
    en_play(0);
    drain_only();
    g0 = en_test_gens;
    for (i = 0; i < 10; i++)
        en_pool(i & 1 ? 0xAD5u : 0x6ADu);
    drain_only();
    CHECK(en_test_gens == g0, "%u chords composed in a rest", en_test_gens - g0);
}

/* en_voice_lead against what it was: every permutation worked out the long way */
static int nearest_ref(int from, int pc, int lo, int hi)
{
    int n = from + ((pc - from) % 12 + 12) % 12;
    if (n - from > 6)
        n -= 12;
    while (n < lo)
        n += 12;
    while (n > hi)
        n -= 12;
    return n;
}
static void voice_lead_ref(const int *from, const int *pcs, int nt, int *to, int lo, int hi)
{
    int t[4], a, b, c, d, v, best = 1 << 30;
    for (v = 0; v < 4; v++)
        t[v] = pcs[v < nt ? v : 0];
    for (a = 0; a < 4; a++)
        for (b = 0; b < 4; b++) {
            if (b == a)
                continue;
            for (c = 0; c < 4; c++) {
                if (c == a || c == b)
                    continue;
                d = 6 - a - b - c;
                {
                    const int p[4] = {a, b, c, d};
                    int n[4], cost = 0, x, y;
                    for (v = 0; v < 4; v++) {
                        n[v] = nearest_ref(from[v], t[p[v]], lo, hi);
                        cost += n[v] > from[v] ? n[v] - from[v] : from[v] - n[v];
                    }
                    for (x = 0; x < 4; x++)
                        for (y = x + 1; y < 4; y++)
                            if (n[x] == n[y])
                                cost += 40;
                    if (cost < best) {
                        best = cost;
                        for (v = 0; v < 4; v++)
                            to[v] = n[v];
                    }
                }
            }
        }
}
static void t_voice_lead(void)
{
    uint32_t x = 12345u;
    int i;
    for (i = 0; i < 200000; i++) {
        int from[4], pcs[4], a[4], b[4], v, nt;
        const int lo = 40 + (int)(x % 20u), hi = lo + 19;
        for (v = 0; v < 4; v++) {
            x ^= x << 13;
            x ^= x >> 17;
            x ^= x << 5;
            from[v] = lo - 6 + (int)(x >> 8) % 32;
            pcs[v] = (int)(x >> 20) % 12;
        }
        nt = 3 + (int)(x >> 3 & 1u);
        en_voice_lead(from, pcs, nt, a, lo, hi);
        voice_lead_ref(from, pcs, nt, b, lo, hi);
        if (memcmp(a, b, sizeof a)) {
            CHECK(0, "voice lead %d: %d %d %d %d, by the long way %d %d %d %d", i, a[0], a[1], a[2], a[3], b[0], b[1], b[2], b[3]);
            break;
        }
    }
}

int main(int ac, char **av)
{
    const int quick = ac > 1 && !strcmp(av[1], "quick");      /* ctl_test quick: no long holds. ctl_test long M: only those, M minutes each */
    const float long_min = ac > 2 ? (float)atof(av[2]) : 12.0f;
    if (ac > 1 && !strcmp(av[1], "long")) {
        hold_long(0, long_min);
        hold_long(1, long_min);
        printf(fails ? "ctl: %d FAILED\n" : "ctl: ok\n", fails);
        return fails != 0;
    }
#define T(f) do { printf("%s\n", #f); f(); } while (0)
    T(t_voice_lead);
    T(t_once);
    T(t_out_of_range);
    T(t_ledger);
    T(t_drums);
    T(t_pad);
    T(t_midi_costs_nothing);
    T(t_midi_off);
    T(t_queue);
    T(t_rest);
    T(t_rest_same_song);
    T(t_rest_sound);
    T(t_hold);
    T(t_hold_bass);
    T(t_next);
    T(t_break_pure);
    T(t_break);
    T(t_break_middle);
    T(t_clock);
    T(t_panic);
    T(t_shed);
    T(t_style_mid);
    T(t_common);
    T(t_bass_lead);
    T(t_mode_in_rest);
    if (!quick) {
        hold_long(0, long_min);
        hold_long(1, long_min);
    }
    printf(fails ? "ctl: %d FAILED\n" : "ctl: ok\n", fails);
    return fails != 0;
}
