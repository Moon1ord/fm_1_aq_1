/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal */
/* On the branch demo-loading the composition is replaced: a fixed four-minute song ("the demo's song" in amb.c), an
 * electric piano over a dark pad, two motifs, a sub and drum patterns; Swell is the piano's level. What follows is
 * AMB-1's design, of which the space (echo, plate) and the parameters remain.
 *
 * The instrument: a generative ambient garden. Four layers draw on one pool of notes (a scale:
 * the root plus up to eleven more pitch classes) and one chord that drifts slowly through it:
 *
 *   PAD     four breathing voices: each swells in, hangs and fades on its own cycle (unequal lengths,
 *           as the loops of Music for Airports), taking its note of the chord as it starts, so the
 *           chord forms and dissolves. Two colours, by piece: a filtered Rhodes (FM, blooming brighter
 *           as it swells) or Voices (saws through two formants, as "2/1"); then a soft low-pass and an
 *           ensemble chorus. The chord moves every DRIFT (20 s .. 5 min): voices on shared
 *           notes hold, the others breathe in anew.
 *   BELLS   soft two-operator FM bells played by four tape loops of unequal length (13, 17, 23, 29
 *           quarter notes), each holding a short phrase that shifts against the others, each its own
 *           shade of bell. EVOLVE changes a note now and then; DENSITY sets how many notes a loop holds.
 *   BASS    a sine and its sub-octave on the chord's root (now and then its fifth), gliding.
 *   BREAK   a drum break played a slice a sixteenth, in two stems (core and top) of a kit: see brk.h.
 *           BUSY sets how much of the top sounds; EVOLVE how often it moves.
 *
 * Over it all, the ARC: a slow tide, minutes long, between sparse (fewer bells, a darker vowel, a quieter
 * break, more reverb) and full; MOTION sets how far it goes. And rests: with EVOLVE, the
 * bass a chord, a bell loop a pass.
 * Then, after Discreet Music's chain, an Echoplex (160 ms, fed back) and a tape loop (to ~5.6 s, two
 * heads, each repeat darker), and Dattorro's plate (from FoMni), fed generously: the space is most of
 * what is heard.
 * FREEZE holds the chord and lets the plate ring on forever.
 *
 * Threads: en_render() runs in the audio interrupt. Everything else posts commands, which the render
 * drains at the start of a block; the state below marked (UI) is written by the render and only read
 * elsewhere. Float only, no libm: fastmath.h. */
#pragma once
#include <stdint.h>
#include "brk.h"

#define EN_SR 44100.0f
#define EN_NLOOP 4                     /* bell tape loops */
#define EN_LOOP_MAX 29
#define EN_NPAD 4                      /* pad voices */

/* modes: pools relative to the root (bit i: root + i semitones) */
enum { MODE_IONIAN, MODE_DORIAN, MODE_PHRYGIAN, MODE_LYDIAN, MODE_MIXOLYDIAN, MODE_AEOLIAN, MODE_PENT_MAJ,
       MODE_PENT_MIN, EN_NMODES };
extern const char *const EN_MODE_NAME[EN_NMODES];
extern const uint16_t EN_MODE_POOL[EN_NMODES];
extern const char *const EN_NOTE_NAME[12];
extern const uint8_t EN_LOOP_LEN[EN_NLOOP];

/* parameters: value ranges in en_param_info() */
enum {
    P_PAD, P_BELLS, P_BASS, P_BREAK,                  /* layer levels */
    P_DENSITY, P_EVOLVE, P_DRIFT, P_BUSY,             /* motion */
    P_ECHO, P_ETIME, P_REVERB, P_SIZE,                /* space */
    P_BRIGHT, P_SWELL, P_BTONE, P_MOTION,             /* tone */
    P_TUNE, P_MIDI, P_TEMPO, P_ROOT, P_MODE, P_BOCT,  /* setup, panel encoders, bell octave */
    P_PADTONE,                                        /* the pad's colour, set by the piece: Rhodes, Voices */
    P_BASSOCT, P_GLIDE, P_FIFTH,                      /* bass: its octave, its glide, how often it leans on the fifth */
    P_WATER, P_KIT,                                   /* the water's level; the break's kit: Glass, Moss */
    P_TIDE,                                           /* how much of the tide is calm: set by the piece, no knob */
    P_ENERGY, P_STYLE, P_BREAKS,                      /* the arranger: how far it may rise, its style, how often a break */
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
void en_play(int on);                          /* the garden grows / rests (the tails ring on) */
void en_next(void);                            /* the next chord: on the next bar (now while resting) */
void en_freeze(int on);
void en_seed(uint32_t s);                      /* a new seed (24 bits are used): every choice from now on follows it */
void en_panic(void);                           /* everything quiet, now */
void en_regrow(void);                          /* a new piece: fresh loops, the home chord */
/* MIDI clock in (as FoMni): ticks drive the 1/16 grid (6 a step) while they keep coming */
enum { EN_CLK_TICK, EN_CLK_START, EN_CLK_CONTINUE, EN_CLK_STOP };
void en_clock(int msg);

/* the render (audio ISR): n stereo frames, 24-bit in int32; gain Q12 (the MASTER pot) */
void en_render(int32_t *out_lr, uint32_t n, uint32_t gain_q12);
void en_shed(void);   /* the audio interrupt's own: four bell voices until the next piece */

/* MIDI out, from the render: the app sends it (USB); status, data 1, data 2 */
void en_midi_out(uint32_t st, uint32_t d1, uint32_t d2);

/* state for the screen (UI) */
extern volatile uint8_t en_playing, en_frozen, en_ext;
extern volatile uint8_t en_chord[EN_NPAD];     /* the chord's notes (MIDI), the piano's hand: with the root, its name */
extern volatile uint8_t en_chord_root;         /* the chord's root (not always its bass), MIDI pitch class 0..11 */
extern volatile uint8_t en_chord_n;            /* its distinct tones, with the root: 2 .. 5 */
extern volatile float en_drift;                /* 0..1: how far to the next chord */
extern volatile float en_pad_level, en_bass_level;
extern volatile uint8_t en_loop_pos[EN_NLOOP];
extern volatile int8_t en_loop_note[EN_NLOOP][EN_LOOP_MAX];   /* -1 rest, else the slot's offset */
extern volatile float en_loop_glow[EN_NLOOP];  /* 0..1: a bell just rang from this loop */
extern volatile uint32_t en_bars;              /* sixteenths played since start (for tests) */
extern volatile float en_arc;                  /* the arc, -1 sparse .. +1 full (times Motion) */
extern volatile uint32_t en_hits;              /* the break's kicks and snares played: the screen counts them */
extern volatile float en_break_level;          /* the break's level, for the meter */
extern volatile uint8_t en_pad_note[EN_NPAD];  /* the note each voice sings now (MIDI) */
extern volatile float en_pad_amp[EN_NPAD];     /* and how loud */

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
/* a break wanted at this bar of the song: its length in bars, 0 none, and *in how many of them are already gone. breaks:
 * the knob, 50 the style's own rate. It always ends with its phrase */
int en_arr_break(uint32_t bar, int level, int style, int breaks, uint32_t seed, uint32_t *in);
const brk_rhythm_t *en_style_rhythm(int style);
extern volatile uint8_t en_section, en_style;   /* for the screen: what is playing */
extern volatile uint8_t en_drum_level, en_break_bars;   /* the level now; the length of the break now, 0 none */
#if defined EN_HOST || defined PLAT_HOST
typedef struct {                                /* a fixed song in place of the arranger */
    uint16_t step;
    uint8_t level, character, ramp, bass, touch;
} en_script_t;
void en_test_script(const en_script_t *s, int n);   /* after en_init; 0 the arranger again */
void en_test_ticks(uint32_t n);                 /* n sixteenths at once, no time passing: hours in a test */
void en_test_jump(uint32_t step);               /* the song's counter, as if it had played this far */
/* each bar as it starts: step, section, level, character, break's bars */
extern void (*en_trace)(uint32_t step, int section, int level, int character, int break_bars);
#endif
/* the demo's harmony: a small vocabulary of functions (ids), a walk over it, and what each is in a pool; pure and testable.
 * See "the demo's song" in amb.c. en_harm_name, _bass, _colour, _tones, en_bass_at and en_motif_note: as in Ionian */
typedef struct {
    uint8_t chord[4];
    uint8_t section, age, exiting, since, next_sub, borrowed;
    uint8_t away, nudge;                       /* cycles since it was last all home; a move is due whatever the level */
} en_harm_t;
enum { EN_FAM_MAJOR, EN_FAM_MINOR, EN_FAM_SPARSE };
typedef struct {                               /* a chord in a pool: pitch classes in semitones above the tonic */
    uint8_t root, bass, col;                   /* col: its passing colour */
    uint8_t family, borrowed, tonic;           /* borrowed: some of its tones may lie outside the pool; tonic: en_harm_walk's root */
    uint16_t tones;                            /* bass, hand and pad, bit i */
    int8_t keys[4], pad[4];
} en_hchord_t;
/* The harmony: chords composed one at a time, on any degree of the pool. See "the chords, one at a time" in amb.c.
 * en_gen_next is pure: state, pool, key, seed, Evolve and the arranger's character in; the state and the next chord out. Every
 * choice hangs on the seed and the state, so calling it again from a copy of the state with another pool gives the chord
 * that pool would have had: how a prepared chord is remade. */
#define EN_GEN_WINDOW 96                       /* chords: at Evolve 100 no three chords in a row come again within this */
typedef struct {
    uint8_t root, shape, bars;                 /* root: semitones above the tonic */
} en_gid_t;
typedef struct {
    uint32_t n, pool;                          /* chords so far; the pool of the last */
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
int en_pool_family(uint32_t pool);
void en_gen_start(en_gen_t *st);
uint32_t en_gen_roots(uint32_t pool);          /* the roots it may use, bit i: i above the tonic; the tonic only if nothing else */
void en_gen_next(en_gen_t *st, uint32_t pool, int key, uint32_t seed, int evolve, int character, en_gchord_t *out);
int en_chord_make(uint32_t pool, int root, int shape, en_hchord_t *out);   /* a colour on a root; 0 none. What a shape can be */
const char *en_gchord_name(const en_gchord_t *g, char *buf);   /* as a score: with its bass, if that is not its root */
#define EN_SHAPE_BORROWED 18                   /* the shape of a chord from outside the pool */
void en_harm_resolve(uint32_t pool, int id, en_hchord_t *out);
/* the pure function: state, pool, root, cycle, seed, level in; next state and the four chords out */
void en_harm_walk(en_harm_t *st, uint32_t pool, int root, uint32_t cycle, uint32_t seed, int level, en_hchord_t *out);
const char *en_hchord_name(const en_hchord_t *c, char *buf);   /* as the screen names it */
int en_pool_nearest(uint32_t pool, int root, int note);        /* the pool's note nearest a MIDI note */
void en_home_voicing(uint32_t pool, int root, uint8_t keys[4][4], uint8_t pad[4][4]);
void en_voice_cycle(const en_harm_t *was, const en_harm_t *now, const en_hchord_t *ch, int anew, int root,
                    const uint8_t home_k[4][4], const uint8_t home_p[4][4], uint8_t keys[4][4], uint8_t pad[4][4]);
int en_bass_ch(int id, const en_hchord_t *ch, const en_hchord_t *nx, uint32_t pool, uint32_t cycle, uint32_t cs, int level,
               uint32_t seed, int *len);
int en_motif_note_ch(int m, int n, const en_hchord_t *ch);
void en_harm_step(en_harm_t *st, uint32_t cycle, uint32_t seed, int level);   /* from the last cycle's state; cycle 0 starts it */
void en_harm_new_pass(en_harm_t *st);          /* a new pass: another place on the way, a borrowed chord allowed again */
int en_harm_count(void);
const char *en_harm_name(int id);
int en_harm_bass(int id);                      /* the bass's pitch class */
int en_harm_plain(int id);                     /* the family's plain chord */
int en_harm_sub(int id);                       /* the relative along the graph, -1 none */
int en_harm_home(int slot);
int en_harm_section(int slot);
uint32_t en_harm_tones(int id);                /* all its pitch classes, bit i */
int en_harm_colour(int id);                    /* its passing tone, semitones above the tonic */
int en_motif_step(int m, int n);               /* motif 0 call, 1 answer; note 0..3: its step in the cycle */
int en_motif_note(int m, int n, const uint8_t *chords);   /* its note over a cycle's chords, semitones */
#define EN_ECHO_STEPS 6                        /* the echo's default place, in sixteenths */
extern volatile uint32_t en_ep_notes, en_ep_chords, en_ep_singles, en_ep_steals;
extern volatile uint8_t en_ep_peak;
#define EN_HOME_CYCLES 24                      /* it goes home after this long away, whatever the arranger asks */
enum { EN_BASS_LIGHT = 2, EN_BASS_FULL = 3, EN_BASS_ROLL = 5, EN_BASS_LONG = 6 };   /* the figures en_bass_* draw from */
int en_bass_at(int id, int next_id, uint32_t cycle, uint32_t cs, int level, uint32_t seed, int *len);   /* level: EN_BASS_*: pure */
extern volatile uint32_t en_bass_notes;
extern volatile uint8_t en_hat_peak;           /* the hat voices sounding at once: at most two */
extern volatile uint8_t en_cycle_now[4];       /* the chords playing now */

/* the pieces: recipes for every parameter (Tune and MIDI out included, but they are the player's) */
#define EN_NPIECES 8
const char *en_piece_name(int i);
void en_piece(int i, int16_t *par, uint16_t *pool);
