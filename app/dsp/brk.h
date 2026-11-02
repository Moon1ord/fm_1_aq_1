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
void brk_stop(void);                     /* what sounds fades out; the next step starts cleanly */
void brk_render(float *out, uint32_t n); /* ADDS mono */

/* for the screen and the tests */
extern volatile uint32_t brk_hits;       /* CORE's kicks and snares played */
extern volatile int8_t brk_st_core, brk_st_top;   /* this step's slices; -1 none */
extern volatile uint32_t brk_jumps;      /* crossfaded moves */
extern volatile uint32_t brk_decoded;    /* source samples decoded */
void brk_decode_range(int kit, int stem, uint32_t start, uint32_t n, int16_t *out);   /* start: a slice's */
uint32_t brk_stem_hash(int kit, int stem);
uint32_t brk_stem_expected(int kit, int stem);
