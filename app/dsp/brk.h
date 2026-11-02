/* SPDX-License-Identifier: MPL-2.0 */
/* The break: two stems of a kit (brk_data.h), played a slice a sixteenth. CORE keeps its skeleton where it is; TOP
 * and the fill at a phrase's end are what changes. Same threads as the engine: brk_step and brk_render in the audio
 * interrupt, nothing here is called from elsewhere. Float only, no libm, no allocation. */
#pragma once
#include <stdint.h>

#define BRK_NSTEP 32u
#define BRK_SLICE 2029u                  /* source samples a sixteenth */
#define BRK_NSAMPLES (BRK_NSTEP * BRK_SLICE)
#define BRK_RATE 22995.33f               /* so a sixteenth at 170 BPM is BRK_SLICE samples */
enum { BRK_CORE, BRK_TOP, BRK_NSTEM };

typedef struct {
    int16_t pred;
    uint8_t idx, rsv;
} brk_ima_t;

void brk_init(void);
void brk_set_kit(int kit);
void brk_set_tempo(float bpm);
void brk_set_ext(int ext);               /* an external clock: a wider tolerance (steps land on block edges) */
void brk_set_evolve(float e);            /* 0..1 */
void brk_set_busy(float b);              /* 0..1: TOP sounds on 40 .. 100 % of its steps */
void brk_levels(float core, float top, float ramp_samples);   /* call on a change, not continuously: each call
                                         restarts the ramp from the remaining distance */
void brk_step(uint32_t sixteenth);       /* one call a sixteenth, counted from the start */
/* The drums of a bar. The arranger says what it wants (a level, a style's numbers, an energy, and a break of so many
 * bars, how far in); brk_gen_bar composes it, once a bar; brk_pattern_step plays it a step at a time. Levels: 0 none, 1 hats,
 * 2 light, 3 fuller, 4 half time. A style's rhythm is only these numbers. */
typedef struct {
    uint8_t density;                     /* 0..100: how many kicks and hats, a break's hits; under 35 the whole style is half time */
    uint8_t synco;                       /* 0..100: odd sixteenths for the kicks, how far a break's snares move, its chopping */
    uint8_t break_rate;                  /* of 256: a phrase of the full rhythm that ends in a break (3/5 of it at the light) */
    uint8_t break_len;                   /* 0..100: 50 mostly one or two bars, 20 never four, 80 mostly four */
    uint8_t ghost;                       /* 0..100: ghosts a bar, and a break's ghost runs. 0 none at all */
} brk_rhythm_t;
typedef struct {
    char core[16], top[16], hat[16];     /* a letter of the banks (core K k S s c t, top g f e r), '.'; hat's h j l i o */
    uint8_t vcore[16], vtop[16], vhat[16];   /* how hard, in percent */
    uint8_t late[16];                    /* top's small lateness, ms. K, S and the rest of core stay on the grid; a hat's is in its letter */
    uint8_t is_break;
    uint8_t rest;                        /* a break's bar with a big gap in it: how many steps */
} brk_bar_t;
/* The rhythm generator: one bar a call, in order. A pure function of its state and these: the same calls give the same bars. A bar out
 * of order (a start, a jump) or another seed starts a new groove. A break is what break_bars and break_in ask for, if it ends with
 * its phrase. Limits: the most kicks, and snares plus ghosts, of a plain bar; the most hits of a break's bar. */
#define BRK_PHRASE 8u
typedef struct {
    uint16_t kick[2], ghost[2], hat[2];  /* the two bars, a bit a step */
    uint8_t accent, open;                /* the off-beats that lean, four bits a bar; bit 0: bar 2's sixteenth 11 opens */
} brk_groove_t;
typedef struct {
    brk_groove_t base, cur;              /* what it is, and what it is this time round */
    uint32_t seed, next, start, gno;     /* the bar expected; this groove's first bar, its number */
    uint16_t life;                       /* bars */
    uint8_t on, level, density, ghostn, synco, half, tail, brk_hits;
} brk_gen_t;
void brk_gen_start(brk_gen_t *g);
void brk_gen_bar(brk_gen_t *g, const brk_rhythm_t *r, int level, int energy, int evolve, uint32_t seed, uint32_t bar, int break_bars, uint32_t break_in, brk_bar_t *out);
void brk_gen_limits(const brk_rhythm_t *r, int level, int energy, int *kicks, int *snares_ghosts, int *break_hits);
extern brk_bar_t brk_bar;                /* the bar being played */
uint32_t brk_hash(uint32_t seed, uint32_t what, uint32_t a);
/* one call a sixteenth, instead of brk_step, playing brk_bar. flags: BRK_DROP the kit holds its breath, BRK_SWELL the
 * rising air starts here; seed: the air's strength, a pure function of it and the step. Returns the kick's strength, 0 none,
 * what the melodic bus ducks under. */
enum { BRK_DROP = 1, BRK_SWELL = 2 };
float brk_pattern_step(uint32_t step, int flags, uint32_t seed);
extern volatile char brk_hit_core, brk_hit_top;   /* the sound this step chose on each stem: its pattern letter, else '.' */
extern volatile char brk_hat_hit;                 /* the hat this step asks of the engine: h j l i closed, o open, '.' none */
extern volatile float brk_hat_vel;                /* and how hard */
extern volatile uint32_t brk_kicks, brk_snares;
void brk_stop(void);                     /* what sounds fades out; the next step starts cleanly */
void brk_render(float *out, uint32_t n); /* ADDS mono */
void brk_render_stem(int stem, float *out, uint32_t n);   /* ADDS one stem, mono */

/* for the screen and the tests */
extern volatile uint32_t brk_hits;       /* CORE's kicks and snares played */
extern volatile int8_t brk_st_core, brk_st_top;   /* this step's slices; -1 none */
extern volatile uint32_t brk_jumps;      /* crossfaded moves */
extern volatile uint32_t brk_decoded;    /* source samples decoded */
void brk_decode_range(int kit, int stem, uint32_t start, uint32_t n, int16_t *out);   /* start: a slice's */
uint32_t brk_stem_hash(int kit, int stem);
uint32_t brk_stem_expected(int kit, int stem);
