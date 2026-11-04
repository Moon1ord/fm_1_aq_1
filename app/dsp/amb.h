/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal */
/* AQ-1: an arranger (en_arr_*) and chords composed one at a time (en_gen_*) play an electric piano over a dark pad, a melody (en_mel_*),
 * a sub and drum patterns, through AMB-1's space: an Echoplex and a tape loop after Discreet Music's chain, then Dattorro's
 * plate.
 *
 * What the buttons ask. Growing (en_play 1): all of it. Resting (en_play 0): the grid stops; drums, bass and motifs let go and ring out,
 * the hand and the pad stay on the chord; growing again goes on from exactly there. Held (en_hold): no new chord and no new section,
 * and everything else plays on over the chord. en_next: the next section, en_break: a break, each on the next bar.
 * en_layer: a layer on or off, as a gain: nothing that composes or plays knows of it. en_arrange: Auto, the arranger leads; Hands, the player does.
 *
 * Threads: en_render() runs in the audio interrupt. Everything else posts commands, which the render
 * drains at the start of a block; the state below marked (UI) is written by the render and only read
 * elsewhere. Float only, no libm: fastmath.h. */
#pragma once
#include <stdint.h>
#include "brk.h"

#define EN_SR 44100.0f
#define EN_NLOOP 4                     /* the rings of motif notes on the screen */
#define EN_LOOP_MAX 29
#define EN_NPAD 4                      /* pad voices */

/* modes: pools relative to the root (bit i: root + i semitones) */
enum { MODE_IONIAN, MODE_DORIAN, MODE_PHRYGIAN, MODE_LYDIAN, MODE_MIXOLYDIAN, MODE_AEOLIAN, MODE_PENT_MAJ,
       MODE_PENT_MIN, EN_NMODES };
extern const char *const EN_MODE_NAME[EN_NMODES];
extern const uint16_t EN_MODE_POOL[EN_NMODES];
extern const char *const EN_NOTE_NAME[12];
extern const uint8_t EN_LOOP_LEN[EN_NLOOP];

/* parameters, a page's four knobs at a time: value ranges and defaults in en_param_info() */
enum {
    P_TEMPO, P_ENERGY, P_EVOLVE, P_STYLE,             /* Home */
    P_KEYS, P_BLOOM, P_BRIGHT, P_NOTES,               /* Keys: the level (the pad follows it), the bloom, the brightness, lone notes */
    P_MOTIFS, P_MRATE, P_MTONE, P_MOCT,               /* Motifs: the level, how often, the tone, the octave */
    P_BASS, P_BMOVE, P_GLIDE, P_BASSOCT,              /* Bass: the level, which figures, the glide, the octave */
    P_DRUMS, P_BREAKS, P_HATS, P_KIT,                 /* Drums: the level, how often a break, the hats' level, the kit */
    P_ECHO, P_ETIME, P_REVERB, P_SIZE,                /* Space */
    P_ROOT, P_TUNE, P_MIDI,                           /* Setup (its fourth knob, the lights, is the panel's: not the engine's) */
    P_MODE,                                           /* the ALGORITHM encoder */
    P_KEYSND, P_PADSND, P_BASSSND, P_MELSND,          /* the sounds: SEL + a knob (the first of each is the sound the owner approved; the melody's first is Auto, then each timbre + 1) */
    P_MELMODE, P_ARPPAT,                              /* the melody: Phrases or Arp, and Arp's pattern: SEL + KNOB 2, SEL + KNOB 3 on its page */
    P_NPARAMS
};
typedef struct {
    const char *name;
    int16_t lo, hi, def;
} en_param_t;
const en_param_t *en_param_info(int i);
void en_param_text(int i, int v, char *buf);   /* the value as the screen shows it (10 bytes) */

/* commands (main loop -> render) */
void en_init(void);
void en_set(int param, int value);
void en_pool(uint32_t pool);                   /* the scale, relative to the root (bit 0 always set) */
void en_mask(uint32_t mask);                   /* the notes chords may stand on, relative to the root: from the next chord */
void en_play(int on);                          /* it grows / rests */
void en_next(void);                            /* the next section, on the next bar (resting: on the first bar it grows again) */
void en_break(void);                           /* a break on the next bar, if there are drums then: it ends with a phrase */
void en_hold(int on);                          /* the chord stays, the section stays, the rest plays on */
void en_seed(uint32_t s);                      /* a new seed (24 bits are used): every choice from now on follows it */
void en_panic(void);                           /* everything quiet, now, and it stays so until en_restore */
void en_restore(void);                         /* it grows, from where it was; the first time, from the top */
void en_regrow(void);                          /* a new piece: its key; the song goes on */
/* The sounds. Nothing that composes knows of them and MIDI is the same whichever is chosen, and whenever. Keys and pad: on what is sounding too, at once and softly, and in full from the next chord; bass: over
 * about a tenth of a second, on the note sounding; melody: from its next note. en_mel_hear: the hand has chosen a timbre: if no phrase is under way and the melody is on, one quiet note in it, not on MIDI, once the turning has stopped */
void en_mel_hear(void);
/* The layers, on and off. Off is a gain and nothing else: what composes goes on composing and MIDI goes on, like a level of 0, and the room rings out
 * what it had. Keys (with the pad) and melody go over about a second. Bass, kick and snare (with the ghosts, the rim, the tom and the kick's duck) and hats: off at once, in the block it is asked in and in a few ms
 * (bass 15, the others 5); on at the next sixteenth, in as few, in whatever the bar is playing by then (the bass in the note it has, or in its next); at once while resting, and before it has played at all. Going to the other mode's set (en_arrange), on the next bar line, as a change of arrangement.
 * A panic and a new piece leave them; en_init has the start.
 * en_layer_press is the hand's switch: on what is off, off what is playing, and it answers 1 for on. In Auto a layer the arranger has absent (en_layers_absent) counts
 * as off, and pressed, the next bar is the first place it plays (a jump of sections is a jump of chords and rhythm: on the bar, as EDIT); in Hands the melody, pressed on, has a phrase at its next chance */
enum { EN_L_KEYS, EN_L_MEL, EN_L_BASS, EN_L_DRUMS, EN_L_HATS, EN_NLAYERS };
void en_layer(int layer, int on);
int en_layer_press(int layer);
extern volatile uint8_t en_layers_absent;      /* Auto: bits of EN_L_*: the next bar's arrangement does not have it. 0 in Hands */
/* Auto: the arranger leads, what is off stays off over it. Hands: the player's: what plays is the switches', Energy the drums' density and the harmony's
 * character (en_hands_part), no section or break comes by itself, EDIT the next section, and it stays; SELECT + EDIT a break of the Breaks knob's length (en_hands_break).
 * Each mode keeps its own set of layers: Auto starts all on, Hands with keys and pad. Back to Auto it goes on from where it is. en_init: Auto; the app says which */
enum { EN_AUTO, EN_HANDS };
void en_arrange(int mode);
/* The volume's duck, for a flash write: the whole output, after everything, down to nothing in 150 ms or back in 300 ms. Nothing under it knows: it plays on.
 * en_ducked: at zero, and has been for a whole DMA ring of samples, so a write now cuts nothing (a panic ends a duck; silence is at zero already) */
void en_duck(int on);
/* The commands' queue: en_room, how many of the values' commands fit now (en_set, en_pool, en_mask, en_arrange, en_seed, en_regrow, en_sys, en_clock): at most EN_ROOM_MAX.
 * en_dropped: 1 once if one of them found no room since it was last asked. The app, which has them all in the project, says them again. PLAY and the other things only a hand
 * asks for have places of their own, and en_duck is no command */
#define EN_ROOM_MAX 56u
uint32_t en_room(void);
int en_dropped(void);
extern volatile uint8_t en_ducked;
/* The system's voice: one FM bell of its own, its notes from the chord as it stands when each is struck, rising; into the mix and the reverb, never to MIDI.
 * A blip does not break into the boot's or the save's notes; nothing sings in a panic's silence */
enum { EN_SYS_BLIP, EN_SYS_BOOT, EN_SYS_SAVE };
void en_sys(int kind);
extern volatile uint8_t en_sys_note;           /* the note it struck last (MIDI) */
extern volatile uint32_t en_sys_notes;         /* and how many since en_init */
/* MIDI clock in (as FoMni): ticks drive the 1/16 grid (6 a step) while they keep coming. Stop rests, Continue grows on. Start grows
 * from the top of a bar and a phrase, keeping the section and the chord (the very first time: the song's top) */
enum { EN_CLK_TICK, EN_CLK_START, EN_CLK_CONTINUE, EN_CLK_STOP };
void en_clock(int msg);

/* the render (audio ISR): n stereo frames, 24-bit in int32; gain Q12 (the MASTER pot) */
void en_render(int32_t *out_lr, uint32_t n, uint32_t gain_q12);
void en_shed(void);   /* the audio interrupt's own: six key voices until the next piece, the others let go softly */

/* MIDI out, from the render: the app sends it (USB); status, data 1, data 2. Channel 1 the keys, 2 the motifs, 3 the bass, 4 the pad's
 * notes, 10 the drums (36 kick, 38 snare and, softer, ghosts, 37 rim, 45 tom, 42 closed hat, 46 open hat); clock. A layer at level 0
 * still sends: that is how it is played from elsewhere */
void en_midi_out(uint32_t st, uint32_t d1, uint32_t d2);

/* state for the screen (UI) */
extern volatile uint8_t en_playing, en_held, en_ext;
extern volatile uint8_t en_mute_keys, en_mute_mel, en_mute_bass, en_mute_drums, en_mute_hats;   /* 1: asked off (the sound follows: en_layer) */
extern volatile uint8_t en_mode;               /* EN_AUTO, EN_HANDS */
extern volatile uint8_t en_chord[EN_NPAD];     /* the chord's notes (MIDI), the piano's hand: with the root, its name */
extern volatile uint8_t en_chord_root;         /* the chord's root (not always its bass), MIDI pitch class 0..11 */
extern volatile uint8_t en_chord_n;            /* its distinct tones, with the root: 2 .. 5 */
extern volatile uint8_t en_chord_bars;         /* its length: 1, 2 or 4 */
extern volatile float en_drift;                /* 0..1: how far along this chord, to the next */
extern volatile float en_pad_level, en_bass_level;
extern volatile uint8_t en_loop_pos[EN_NLOOP];
extern volatile int8_t en_loop_note[EN_NLOOP][EN_LOOP_MAX];   /* -1 rest, else the slot's offset */
extern volatile float en_loop_glow[EN_NLOOP];  /* 0..1: a bell just rang from this loop */
extern volatile uint32_t en_bars;              /* sixteenths played since start (for tests) */
extern volatile uint32_t en_hits;              /* the break's kicks and snares played: the screen counts them */
extern volatile float en_break_level;          /* the break's level, for the meter */

/* the pure parts, for the tests and the screen */
int en_scale(uint32_t pool, int *steps);       /* the pool's pitch classes (relative), ascending; count */
/* the chord on scale degree deg: its tones (MIDI pitch classes, absolute), 3 or 4 of them; count */
int en_chord_tones(uint32_t pool, int root, int deg, int *pcs);
/* voice leading: new pad notes for the tones, each voice moving as little as possible, all in lo..hi */
void en_voice_lead(const int *from, const int *pcs, int ntones, int *to, int lo, int hi);
const char *en_chord_name(const int *pcs, int n, char *buf);   /* "Em7", "Dsus4", "C" ... */

#define EN_HARM_SEED 7u                         /* the seed until en_seed says otherwise */

/* The arranger: endless passes through the sections, each a whole number of phrases of bars, each section a part of what
 * is asked of the drums and the harmony. Nothing here knows how the harmony is made or which pattern a drum level is: it
 * says the section, the drums' level, a character for the harmony and, for a bar, whether a break is wanted and how long.
 * Pure: state, seed, style, energy in; the next section and its length out. See "the arranger" in amb.c. */
enum { EN_SEC_INTRO, EN_SEC_HATS, EN_SEC_LIGHT, EN_SEC_DOWN, EN_SEC_FULL, EN_SEC_OUT, EN_NSECTIONS };
enum { EN_ST_LIQUID, EN_ST_ROLLER, EN_ST_JUNGLE, EN_ST_HALF, EN_ST_DRIFT, EN_ST_LOADING, EN_NSTYLES };
#define EN_ARR_PHRASE 8u                        /* bars: sections and break placement go by it */
extern const char *const EN_SECTION_NAME[EN_NSECTIONS];
extern const char *const EN_STYLE_NAME[EN_NSTYLES];
typedef struct {
    uint32_t pass;                              /* passes completed */
    uint16_t bars;                              /* this section's length */
    uint8_t section;
} en_arr_t;
typedef struct {
    uint8_t section, level;                     /* the drums: 0 none, 1 hats, 2 light, 3 full, 4 half time */
    uint8_t character;                          /* the harmony: 0 alone, 1 drifting, 2 grown, 3 the peak */
    uint8_t ramp;                               /* bars the drums take to arrive */
    uint8_t bass, touch;                        /* the bass's level and how firmly the piano plays, % */
} en_part_t;
void en_arr_first(en_arr_t *a, uint32_t seed, int style, int energy);
void en_arr_next(en_arr_t *a, uint32_t seed, int style, int energy);
void en_arr_range(int style, int section, int energy, int *lo_bars, int *hi_bars);
void en_arr_part(const en_arr_t *a, uint32_t bar_in, int style, int energy, en_part_t *out);   /* a bar of the section */
void en_hands_part(int style, int energy, int section, en_part_t *out);   /* Hands: what this Energy asks, in this section, whatever the bar */
int en_hands_break(int breaks);                /* Hands: the length of a break called: 1, 2, 4 bars */
/* a break wanted at this bar of the song: its length in bars, 0 none, and *in how many of them are already gone. breaks:
 * the knob, 50 the style's own rate. It always ends with its phrase */
int en_arr_break(uint32_t bar, int level, int style, int breaks, uint32_t seed, uint32_t *in);
/* a break asked for on this bar: its length, and how many bars of the phrase go first so that it starts here and ends with it */
int en_arr_break_now(uint32_t bar, int style, uint32_t seed, uint32_t *skip);
const brk_rhythm_t *en_style_rhythm(int style);
extern volatile uint8_t en_section, en_style;   /* for the screen: what is playing */
extern volatile uint8_t en_drum_level, en_break_bars;   /* the level now; the length of the break now, 0 none */
extern volatile uint8_t en_drum_next;           /* the next bar's level: 2 and up have a kick, so en_break can be had */
#if defined EN_HOST || defined PLAT_HOST
void en_test_ticks(uint32_t n);                 /* n sixteenths at once, no time passing: hours in a test */
void en_test_jump(uint32_t step);               /* the song's counter, as if it had played this far */
extern volatile uint32_t en_test_nans;          /* blocks with a NaN on the way to the limiter */
extern volatile uint32_t en_hat_hits;           /* hats struck */
extern int en_test_pad_off;                     /* the pad out of the mix: to hear the piano by itself */
extern float en_test_peak_in;                   /* the most that has gone into the limiter (1: its knee, about) */
uint32_t en_test_degrees(void);                 /* the degrees the engine has been told */
uint32_t en_test_pool(void);                    /* the scale and a parameter, likewise */
int en_test_par(int p);
uint32_t en_test_song(void);                    /* the next step of the song */
uint32_t en_test_seed(void);                    /* the seed all of it follows */
uint32_t en_test_chord_left(void);              /* steps to the next chord */
uint32_t en_test_section_bar(void);             /* this section's bar; how many it has */
uint32_t en_test_section_bars(void);
int en_test_silent(void);                       /* since en_panic */
float en_test_mute_gain(int layer);             /* a layer's gain as it is now: 0 off .. 1 on */
float en_test_bass_song(void);                  /* the arrangement's bass, where it is going: 0 absent */
float en_test_duck(void);                       /* the duck the melodic bus last had, the more of its two ends */
int en_test_bass_down(void);                    /* notes the engine thinks are down, by layer: what MIDI should say */
int en_test_bells_on(void);                     /* the melody's notes MIDI has been told are down: one at most */
int en_test_bell_voices(void);                  /* its voices sounding, tails too */
extern uint32_t en_test_bell_echoes;            /* its notes that have gone to the echo */
int en_test_bell_limit(void);                   /* how many it may have */
float en_test_bell_stolen(void);                /* the loudest a sounding voice was when it was taken: its level, 0..1 */
void en_test_bell_stolen_clear(void);           /* from here on */
void en_test_bell_note(int timbre, int note, float vel);   /* one note in a timbre, at once, out of the echo: to measure it */
void en_test_key_note(int note, float vel);     /* a key held, at once: to measure it */
void en_test_bass_note(int note, uint32_t steps);   /* a note of the bass, at once, at this MIDI note */
extern float en_test_keys_gain;                 /* the keys alone, against the pad: 1 as ever, 0 out of the mix: to hear the pad */
extern int en_test_bell_struck;                 /* the timbre of the melody's last note, -1 none */
extern uint32_t en_test_mel_heard;              /* the notes en_mel_hear has played */
float en_test_sound_now(int which);             /* where it has got to: 0 the pad's level, 1 the keys' bus */
int en_test_bell_keys(void);                    /* the melody's keys down, whatever MIDI has been told */
int en_test_bell_off_chord(void);               /* those of them that the chord sounding does not have */
uint32_t en_test_bell_case(int which);          /* a chosen timbre, against the one it stands for: 0 its sound was over before the note's MIDI, 1 the MIDI before its sound */
void en_test_mel_hear(int timbre);              /* a statement at the next chance in this timbre, -1 back to its own: to audition them */
extern uint32_t en_mel_ops;                     /* the generator's steps of work, all of them: what bounds its cost */
int en_test_pad_sent(void);
int en_test_keys_down(void);
int en_test_keys_on(void);                      /* voices sounding or still fading */
int en_test_keys_limit(void);                   /* how many it may have */
int en_test_keys_down_past(int from);           /* lone notes' keys down on voices from this one on, sounding */
float en_test_keys_low(void);                   /* the quietest key held */
extern uint32_t en_test_gens;                   /* chords composed for the engine */
float en_test_bass_pitch(void);                 /* the bass, cycles a sample: where it is and where it is going */
float en_test_bass_target(void);
/* each bar as it starts: step, section, level, character, break's bars */
extern void (*en_trace)(uint32_t step, int section, int level, int character, int break_bars);
#endif
enum { EN_FAM_MAJOR, EN_FAM_MINOR, EN_FAM_SPARSE };
typedef struct {                               /* a chord in a pool: pitch classes in semitones above the tonic */
    uint8_t root, bass, col;                   /* col: its passing colour */
    uint8_t family, borrowed, tonic;           /* borrowed: some of its tones may lie outside the pool; tonic: the key it is in */
    uint16_t tones;                            /* bass, hand and pad, bit i */
    int8_t keys[4], pad[4];
} en_hchord_t;
/* The harmony: chords composed one at a time, on any degree of the pool. See "the chords, one at a time" in amb.c.
 * en_gen_next is pure: state, pool, key, seed, Evolve, the arranger's character and the degrees in; the state and the next chord out. Every
 * choice hangs on the seed and the state, so calling it again from a copy of the state with another pool gives the chord
 * that pool would have had: how a prepared chord is remade. */
#define EN_GEN_WINDOW 96                       /* chords: at Evolve 100 no three chords in a row come again within this */
typedef struct {
    uint8_t root, shape, bars;                 /* root: semitones above the tonic */
} en_gid_t;
typedef struct {
    uint32_t n, pool;                          /* chords so far; the pool of the last */
    uint16_t mask;                             /* the degrees in force for the last */
    en_gid_t phrase[4];                        /* the memory: the phrase being played */
    uint8_t tmpl, ph_i, composing, valid, repeats, ph_char, was_char, var_kind, var_slot;
    uint8_t prev_root, prev2, prev3, prev_bass, away, since_t, since_b, key, seen_i;
    uint16_t prev_tones, id1, id2;
    uint8_t keys[4], pad[4];                   /* the last voicing */
    uint16_t seen[EN_GEN_WINDOW];              /* every three chords in a row, hashed */
} en_gen_t;
typedef struct {
    en_hchord_t ch;
    uint8_t keys_v[4], pad_v[4];               /* its voicing, MIDI notes */
    uint8_t shape, bars;                       /* its colour; its length, 1, 2 or 4 */
} en_gchord_t;
enum { SH_7, SH_9, SH_ADD9, SH_69, SH_11, SH_7SUS, SH_9SUS, SH_13SUS, SH_7_3, SH_ADD9_3, SH_7_5, SH_S2, SH_S4, SH_S2_7, SH_S4_7,
       SH_S2_6, SH_S2_5, SH_S4_5, SH_BORROW };   /* a chord's colours: _3, _5 over that tone; S open chords */
#if defined EN_HOST || defined PLAT_HOST
extern void (*en_trace_chord)(uint32_t step, const en_gchord_t *chord, int character);   /* each chord as it starts */
#endif
int en_pool_family(uint32_t pool);
void en_gen_start(en_gen_t *st);
uint32_t en_gen_roots(uint32_t pool);          /* the roots it may use, bit i: i above the tonic; the tonic only if nothing else */
/* The degrees. mask: which notes a chord may stand on or have in its bass, bit i: i above the tonic, whatever the key; a note
 * not in the pool has no say, its bit waits for it. Fewer than two roots left by pool and mask together (the tonic, which is
 * never in the bass, does not count): the mask is not used at all, every degree of the pool is */
#define EN_MASK_ALL 0xFFFu
#define EN_GEN_GONE 0xFFu                      /* a place in the phrase's memory whose chord went with the mask */
uint32_t en_gen_roots_in(uint32_t pool, uint32_t mask);   /* of the roots, those it leaves: 0 for none */
int en_mask_usable(uint32_t pool, uint32_t mask);         /* does it leave two? */
uint32_t en_mask_effective(uint32_t pool, uint32_t mask); /* what is in force: mask, or all */
void en_gen_next(en_gen_t *st, uint32_t pool, int key, uint32_t seed, int evolve, int character, uint32_t mask, en_gchord_t *out);
int en_chord_make(uint32_t pool, int root, int shape, en_hchord_t *out);   /* a colour on a root; 0 none. What a shape can be */
const char *en_gchord_name(const en_gchord_t *g, char *buf);   /* as a score: with its bass, if that is not its root */
const char *en_hchord_name(const en_hchord_t *c, char *buf);   /* as the screen names it */
int en_pool_nearest(uint32_t pool, int root, int note);        /* the pool's note nearest a MIDI note */
int en_bass_ch(const en_hchord_t *ch, const en_hchord_t *nx, uint32_t pool, uint32_t count, uint32_t cs, uint32_t steps, int level,
               uint32_t seed, int *len);
/* The melody: phrases composed one at a time. See "the melody" in amb.c. en_mel_next is pure: state, pool, key, the chords it can
 * see (the one sounding, what is left of it, and those coming), the section, the knobs and the seed in; the state and one phrase of
 * notes, lengths, velocities and a timbre out. Called on a bar, one bar before the phrase's first strong beat. */
#define EN_MEL_NOTES 7                         /* a phrase's most */
#define EN_MEL_CHORDS 3                        /* chords it looks at: the one sounding and two to come */
#define EN_MEL_MEM 4                           /* motifs remembered */
#define EN_MEL_SEEN 128                        /* motifs it knows it has had: at Evolve 100 none comes again within these */
#define EN_MEL_LEAD 16u                        /* steps from the call to the phrase's first bar line; a pickup starts in them */
#define EN_MOTIF_NOTES 4
#define EN_MEL_RANGE 19                        /* semitones, its whole register: an octave and a half */
#define EN_MEL_LO(key) (72 + (key))            /* the key's tonic an octave over middle C: the hand stays under it */
#define EN_MEL_TOP 98                          /* the highest, however high the key */
/* Arp, the melody's other mode: the notes of the chord sounding, one after another with no rests, in the melody's voices and on its channel, through a register of two octaves. All of it pure; amb.c plays it.
 * en_arp_steps: Rate's division, in sixteenths: to 33 a quarter (4), to 66 an eighth (2), above a sixteenth (1). en_arp_next: from where it is, the next note in this pattern of these pitch classes (absolute, bit i);
 * h only for Random. en_arp_timbre: what Auto has it sound in: of what Tone allows, those that speak at a sixteenth, by a hash that moves slowly. */
enum { EN_MEL_PHRASES, EN_MEL_ARP };
enum { EN_ARP_UP, EN_ARP_DOWN, EN_ARP_UPDOWN, EN_ARP_RANDOM, EN_ARP_NPATTERNS };
typedef struct {
    uint8_t last;                              /* the last note, 0 none yet */
    int8_t dir;                                /* up-down: the way it is going */
} en_arp_t;
int en_arp_steps(int rate);
void en_arp_range(int key, int *lo, int *hi);
void en_arp_start(en_arp_t *a);
int en_arp_next(en_arp_t *a, uint32_t tones, int key, int pattern, uint32_t h);
int en_arp_timbre(int tone, uint32_t h);
int en_mel_fold(int n);                        /* a note MIDI has: over 127, down by octaves, so it stays in tune */
enum { EN_TB_FLUTE, EN_TB_VOCAL, EN_TB_VIBES, EN_TB_MARIMBA, EN_TB_BOX, EN_TB_GLASS, EN_NTIMBRES };   /* softest to brightest */
extern const char *const EN_TIMBRE_NAME[EN_NTIMBRES];
enum { EN_MEL_STATEMENT, EN_MEL_ANSWER, EN_MEL_RETURN, EN_MEL_CLOSE };
typedef struct {
    uint8_t n, pick, span;                     /* pick: steps before its bar line; span: its length from the bar line */
    uint8_t on[EN_MOTIF_NOTES], len[EN_MOTIF_NOTES];   /* from the pickup */
    int8_t deg[EN_MOTIF_NOTES];                /* its contour: scale steps from the first note */
    uint16_t sig;
} en_motif_t;
typedef struct {
    uint32_t n, periods;                       /* phrases so far; periods so far */
    en_motif_t mem[EN_MEL_MEM];                /* mem[0] the theme: it stays longest */
    en_motif_t now;                            /* this period's */
    uint16_t seen[EN_MEL_SEEN];
    uint8_t mem_n, mem_i, seen_i, seen_n, role, roles, recalled;
    uint8_t timbre, timbre_left, last;         /* last: the last note played, 0 none */
    uint8_t anchor, seq, cad;                  /* the statement's first note (a place in the scale) and its form */
    int8_t dev;
} en_mel_t;
typedef struct {
    uint16_t tones;                            /* absolute pitch classes, bit i */
    uint8_t root;                              /* absolute */
    uint16_t steps;                            /* the first: steps left from the call; the others: their lengths */
} en_mel_chord_t;
typedef struct {
    uint32_t pool, seed;                       /* pool relative to the key */
    int key, section, evolve, rate, tone, echo_steps, nchords;
    int hands;                                 /* the player's (Hands): over the recipes' Rates the rests shrink towards none, the section's calm counting for less and less. 0: Auto's, as approved */
    en_mel_chord_t chord[EN_MEL_CHORDS];
} en_mel_in_t;
typedef struct {
    uint16_t step, len;                        /* from the call */
    uint8_t note, vel;                         /* MIDI, without the Octave knob; 1..100 */
    uint8_t echo;                              /* its first two echoes sit in the chords they land on: only then it goes to the echo */
} en_mnote_t;
typedef struct {
    uint8_t n, timbre, role, recalled, rest;   /* rest: bars of silence after it, at least */
    uint16_t end, sig;                         /* end: the step it has all ended by; sig: the motif it is made of */
    en_mnote_t note[EN_MEL_NOTES];
} en_phrase_t;
void en_mel_start(en_mel_t *st);
void en_mel_next(en_mel_t *st, const en_mel_in_t *in, en_phrase_t *out);
/* a timbre at a Tone: two operators, the modulator's ratio to the carrier (whole numbers: 1, 2, 3); its index in radians falling to a
 * floor, how long it takes; attack, the decay's 60 dB, the release after the key, s; vibrato (of the pitch, the same for both operators); level */
typedef struct {
    float ratio, index, floor_idx, idec_s, att_s, t60_s, rel_s, vib, level;
} en_mel_voice_t;
void en_mel_voice(int timbre, int tone, en_mel_voice_t *v);
#if defined EN_HOST || defined PLAT_HOST
extern void (*en_trace_mel)(uint32_t step, const en_phrase_t *p);   /* each phrase as it is planned */
extern uint32_t en_test_mel_refits;             /* phrases that had to be moved: a chord that was not the one foreseen */
#endif
#define EN_ECHO_STEPS 6                        /* the echo's default place, in sixteenths */
extern volatile uint32_t en_ep_notes, en_ep_chords, en_ep_singles, en_ep_steals;
extern volatile uint8_t en_ep_peak;
enum { EN_BASS_LIGHT = 2, EN_BASS_FULL = 3, EN_BASS_ROLL = 5, EN_BASS_LONG = 6 };   /* the figures en_bass_* draw from */
extern volatile uint32_t en_bass_notes;
/* the figures the Move knob asks for: 50 the style's own (set), 0 the long roots, 100 the rolling ones. Between, a figure at a
 * time, as h falls */
int en_bass_move(int set, int move, uint32_t h);
extern volatile uint8_t en_hat_peak;           /* the hat voices sounding at once: at most two */

/* the pieces: recipes for every parameter (Tune and MIDI out included, but they are the player's) */
#define EN_NPIECES 8
const char *en_piece_name(int i);
void en_piece(int i, int16_t *par, uint16_t *pool);
