/* SPDX-License-Identifier: MPL-2.0 */
/* The demo's song as a fixed script: what the owner approved by ear, and what the checks of the harmony, the piano and the
 * drums run over now the instrument arranges itself (en_test_script). 16 bars a harmonic cycle. */
#pragma once
#include "amb.h"

static const en_script_t DEMO_SCRIPT[] = {
    {0, 0, 0, 1, 0, 92},                         /* 0:00 pad and piano */
    {256, 1, 1, 4, 0, 95},                       /* 0:23 hats alone */
    {384, 2, 1, 2, 0, 97},                       /* 0:34 a light kick and snare */
    {512, 2, 2, 2, 100, 100},                    /* 0:45 the two-step, the bass */
    {1536, 1, 1, 2, 75, 96},                     /* 2:15 the breakdown: hats, chords, long roots */
    {1664, 4, 1, 2, 75, 96},                     /* 2:27 half time, for a moment */
    {1792, 3, 3, 1, 112, 114},                   /* 2:38 back, fuller, the second cycle: the arrival */
    {2304, 3, 2, 1, 112, 108},                   /* 3:23 the harmony starts home; the drums stay */
    {2624, 1, 1, 8, 55, 95},                     /* 3:51 out: the drums thin away */
    {2752, 0, 0, 4, 0, 90},                      /* 4:03 the last chord, alone */
};
#define DEMO_SCRIPT_N ((int)(sizeof DEMO_SCRIPT / sizeof DEMO_SCRIPT[0]))
