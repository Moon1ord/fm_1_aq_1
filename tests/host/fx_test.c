/* SPDX-License-Identifier: MPL-2.0 */
/* The keys' and the melody's effects (P_KFX ..): Off is what it was, to the bit, pinned as renders of the commit before; each effect is measurably itself, in time, within its level, and bounded at every end; what the instrument composes is on the lines it says, and
 * follows the seed and nothing else; nothing clicks, whatever is changed and whenever, and a rest, a new piece, a panic or a save's duck do no more than they do without it. MIDI never knows. The panel: tests/scenarios/fx.amb */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "amb.h"

static int fails;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); fails++; } } while (0)
#define GAIN 2430u
#define BLOCK 256u
#define FS 44100.0f
#define SECS(s) ((uint32_t)((float)(s) * FS / (float)BLOCK))
#define BAR_STEPS 16u
#define PHRASE_STEPS (16u * EN_ARR_PHRASE)
#define LEVEL_DB 1.5f                            /* the owner's: each effect at any setting, within this of Off on the layer */

static uint32_t mh, mn, mblock;
void en_midi_out(uint32_t st, uint32_t d1, uint32_t d2)
{
    mh = (mh ^ (st << 16 | d1 << 8 | d2)) * 16777619u;
    mh = (mh ^ mblock) * 16777619u;
    mn++;
}

static int32_t blk[2u * BLOCK];
static uint32_t ah;
static void render(uint32_t blocks)
{
    uint32_t b, j;
    for (b = 0; b < blocks; b++) {
        mblock++;
        en_render(blk, BLOCK, GAIN);
        for (j = 0; j < 2u * BLOCK; j++)
            ah = (ah ^ (uint32_t)blk[j]) * 16777619u;
    }
}
#define CAP_MAX (44100u * 2u * 100u)
static float *cap;
static uint32_t ncap;
static void capture(uint32_t blocks)
{
    uint32_t b, j;
    for (b = 0; b < blocks; b++) {
        mblock++;
        en_render(blk, BLOCK, GAIN);
        for (j = 0; j < 2u * BLOCK; j++) {
            ah = (ah ^ (uint32_t)blk[j]) * 16777619u;
            if (ncap < CAP_MAX)
                cap[ncap++] = (float)blk[j] * (1.0f / 8300000.0f);
        }
    }
}
static void counts_clear(void)
{
    mh = ah = 2166136261u;
    mn = mblock = 0u;
    ncap = 0u;
}
static void start(uint32_t seed, int mode, int style)
{
    en_init();
    en_seed(seed);
    en_arrange(mode);
    en_set(P_STYLE, style);
    en_test_pad_off = 0;
    en_test_keys_gain = 1.0f;
    en_test_fx_dry = 1.0f;
    counts_clear();
}
static void layers(int keys, int mel, int bass, int drums, int hats)
{
    en_layer(EN_L_KEYS, keys);
    en_layer(EN_L_MEL, mel);
    en_layer(EN_L_BASS, bass);
    en_layer(EN_L_DRUMS, drums);
    en_layer(EN_L_HATS, hats);
}
static int fx_par(int layer) { return layer == EN_L_KEYS ? P_KFX : P_MFX; }
static void fx_set(int layer, int fx, int motion, int depth)
{
    const int p = fx_par(layer);
    en_set(p, fx);
    en_set(p + 1, motion);
    en_set(p + 2, depth);
}

/* ---- what is in the tables */
static void t_table(void)
{
    int i, p;
    static const int PAR[10] = {P_KFX, P_KMOTION, P_KDEPTH, P_KECHO, P_KREV, P_MFX, P_MMOTION, P_MDEPTH, P_MECHO, P_MREV};
    static const int DEF[10] = {EN_FX_OFF, 50, 50, 100, 100, EN_FX_OFF, 50, 50, 100, 100};
    static const int HI[10] = {EN_NFX - 1, 100, 100, 200, 200, EN_NFX - 1, 100, 100, 200, 200};
    char t[16];
    for (i = 0; i < 10; i++) {
        const en_param_t *q = en_param_info(PAR[i]);
        CHECK(q->def == DEF[i] && q->lo == 0 && q->hi == HI[i], "param %d: %d %d %d", PAR[i], q->lo, q->hi, q->def);
    }
    for (i = 0; i < EN_NPIECES; i++) {           /* a recipe: Off and neutral */
        int16_t v[P_NPARAMS];
        uint16_t pool;
        en_piece(i, v, &pool);
        for (p = 0; p < 10; p++)
            CHECK(v[PAR[p]] == DEF[p], "%s: par %d is %d", en_piece_name(i), PAR[p], v[PAR[p]]);
    }
    for (i = 0; i < EN_NFX; i++) {
        en_param_text(P_KFX, i, t);
        CHECK(!strcmp(t, EN_FX_NAME[i]), "%s", t);
        en_param_text(P_MFX, i, t);
        CHECK(!strcmp(t, EN_FX_NAME[i]), "%s", t);
    }
    CHECK(!strcmp(EN_FX_NAME[EN_FX_CHORUS], "Chorus") && !strcmp(EN_FX_NAME[EN_FX_AUTOPAN], "Autopan") && !strcmp(EN_FX_NAME[EN_FX_TREMOLO], "Tremolo") && !strcmp(EN_FX_NAME[EN_FX_FILTER], "Filter"), "the names");
    printf("  a piece: %d commands, the queue %u\n", P_NPARAMS + 6, EN_ROOM_MAX);
    CHECK(P_NPARAMS + 6 == 52, "a piece's commands: %d", P_NPARAMS + 6);
    CHECK(P_NPARAMS + 6 <= (int)EN_ROOM_MAX, "a piece no longer fits");
    CHECK(EN_ROOM_MAX == 56u, "the queue as it was");
}

/* ---- Off is what it was: renders of 75431cf, sixty seconds each. Motion and Depth at any value do nothing at Off, nor do sends at 100 said again */
static const struct {
    int mode, style, mel;
    uint32_t audio, midi, n;
} PIN[] = {
    {0, 0, 3, 0x9cb8b925u, 0x8c4263d8u, 5101u},
    {0, 2, 4, 0x226b125cu, 0x0bc27902u, 4822u},
    {0, 5, 1, 0x4f78f349u, 0x48d78526u, 4383u},
    {1, 0, 2, 0xfce9c1b0u, 0xfe65891bu, 5639u},
    {1, 1, 3, 0x64f5cdbau, 0xd5797925u, 6668u},
    {1, 4, 1, 0x3d5f9c4fu, 0xe336c257u, 4924u},
};
static void pin_start(uint32_t r)
{
    en_test_pad_off = 0;
    en_test_keys_gain = 1.0f;
    en_init();
    en_seed(4000u + r);
    en_arrange(PIN[r].mode);
    en_set(P_STYLE, PIN[r].style);
    en_set(P_MELMODE, PIN[r].mel == 3 ? EN_MEL_ARP : EN_MEL_PHRASES);
    layers(1, 1, 1, 1, 1);
    en_play(1);
    counts_clear();
}
static void t_off_pinned(void)
{
    uint32_t r;
    for (r = 0u; r < sizeof PIN / sizeof PIN[0]; r++) {
        pin_start(r);
        render(44100u * 60u / 256u);
        CHECK(ah == PIN[r].audio, "run %u: the sound moved: %08x", r, ah);
        CHECK(mh == PIN[r].midi && mn == PIN[r].n, "run %u: MIDI moved: %08x %u", r, mh, mn);
        pin_start(r);                            /* the same, with the Off layers told everything but an effect, on the way */
        render(10u);
        en_set(P_KMOTION, 100);
        en_set(P_KDEPTH, 3);
        en_set(P_MMOTION, 0);
        en_set(P_MDEPTH, 100);
        en_set(P_KECHO, 100);
        en_set(P_MREV, 100);
        en_set(P_KFX, EN_FX_OFF);
        render(44100u * 60u / 256u - 10u);
        CHECK(ah == PIN[r].audio, "run %u, Off with Motion and Depth moved: the sound moved: %08x", r, ah);
        CHECK(en_test_fx_q(EN_L_KEYS, EN_FXQ_BUS) == 0.0f && en_test_fx_q(EN_L_MEL, EN_FXQ_BUS) == 0.0f, "run %u: Off goes by no bus", r);
    }
}

/* ---- MIDI: whatever the effects, and whenever they change. And the voices do not know: nothing of the bass, the drums or the pad moves, whatever the keys' and the melody's buses do */
static void t_midi_never(void)
{
    uint32_t r, fx;
    for (r = 0u; r < sizeof PIN / sizeof PIN[0]; r++)
        for (fx = 1u; fx < EN_NFX; fx++) {
            uint32_t t;
            pin_start(r);
            fx_set(EN_L_KEYS, (int)fx, 70, 80);
            fx_set(EN_L_MEL, (int)(fx % (EN_NFX - 1u) + 1u), 100, 100);
            en_set(P_KECHO, 150);
            en_set(P_MREV, 30);
            for (t = 0u; t < 44100u * 60u / 256u; t++) {
                if (t % 1500u == 700u)
                    en_set(P_KFX, (int)(t / 1500u % EN_NFX));
                render(1u);
            }
            CHECK(mh == PIN[r].midi && mn == PIN[r].n, "run %u %s: MIDI moved: %08x %u", r, EN_FX_NAME[fx], mh, mn);
        }
}
static uint32_t others_alone(int keys_fx, int mel_fx, int pad_only)
{
    start(4242u, EN_HANDS, EN_ST_LIQUID);
    layers(1, 1, 1, 1, 1);
    if (pad_only) {
        en_test_keys_gain = 0.0f;                /* the keys' voices out, the pad in */
        en_set(P_BASS, 0);
        en_set(P_DRUMS, 0);
    } else {
        en_set(P_KEYS, 0);                       /* what is left: the bass and the drums */
    }
    en_set(P_MOTIFS, 0);
    fx_set(EN_L_KEYS, keys_fx, 100, 100);
    fx_set(EN_L_MEL, mel_fx, 100, 100);
    en_play(1);
    render(SECS(30));
    return ah;
}
static void t_others_untouched(void)
{
    const uint32_t off = others_alone(EN_FX_OFF, EN_FX_OFF, 0), pad = others_alone(EN_FX_OFF, EN_FX_OFF, 1);
    int fx;
    for (fx = 1; fx < EN_NFX; fx++) {
        CHECK(others_alone(fx, fx, 0) == off, "%s moved the bass or the drums", EN_FX_NAME[fx]);
        CHECK(others_alone(fx, fx, 1) == pad, "%s moved the pad", EN_FX_NAME[fx]);
    }
}

/* ---- what the instrument composes: the pure part */
static void compose_at(int fx, int layer, int motion, int depth, uint32_t seed, uint32_t step, int section, int chord_bars, int mel_on, uint32_t mel_count, en_fx_plan_t *o)
{
    en_fx_in_t in;
    in.fx = fx;
    in.layer = layer;
    in.motion = motion;
    in.depth = depth;
    in.seed = seed;
    in.step = step;
    in.section = section;
    in.chord_bars = chord_bars;
    in.mel_on = mel_on;
    in.mel_count = mel_count;
    en_fx_compose(&in, o);
}
static int plan_same(const en_fx_plan_t *a, const en_fx_plan_t *b)
{
    return a->cycles == b->cycles && a->dir == b->dir && a->depth == b->depth && a->shape == b->shape && a->hz == b->hz && a->open == b->open;
}
static void t_compose_still(void)
{
    int fx, l, sec;
    uint32_t seed, step;
    for (fx = 1; fx < EN_NFX; fx++)
        for (l = 0; l < 2; l++) {
            en_fx_plan_t first;
            compose_at(fx, l, 0, 60, 1u, 0u, 0, 1, 0, 0u, &first);
            for (seed = 1u; seed < 40u; seed += 3u)
                for (sec = 0; sec < EN_NSECTIONS; sec++)
                    for (step = 0u; step < 40u * PHRASE_STEPS; step += 37u) {
                        en_fx_plan_t p;
                        compose_at(fx, l, 0, 60, seed, step, sec, 1 + (int)(step % 4u), (int)(step & 1u), step, &p);
                        CHECK(plan_same(&p, &first), "%s at Motion 0 moved", EN_FX_NAME[fx]);
                    }
            CHECK(first.dir == 1 && first.shape == 0.0f, "%s: its still setting", EN_FX_NAME[fx]);
            if (fx == EN_FX_AUTOPAN)
                CHECK(first.cycles == 4u, "Autopan, still: a bar: %u", first.cycles);
            if (fx == EN_FX_TREMOLO)
                CHECK(first.cycles == 16u, "Tremolo, still: quarters: %u", first.cycles);
            if (fx == EN_FX_CHORUS)
                CHECK(first.hz > 0.2f && first.hz < 1.5f, "Chorus, still: %f", (double)first.hz);
            if (fx == EN_FX_FILTER)
                CHECK(fabsf(first.open - 0.4f) < 1e-5f, "Filter, still: Depth says: %f", (double)first.open);
        }
}
static void t_compose_lines(void)
{
    int fx, l, seen_rate = 0, seen_dir = 0, seen_shape = 0;
    uint32_t seed, ph, b;
    for (fx = 1; fx < EN_NFX; fx++)
        for (l = 0; l < 2; l++)
            for (seed = 1u; seed < 30u; seed++)
                for (ph = 0u; ph < 40u; ph++) {
                    en_fx_plan_t a, last;
                    compose_at(fx, l, 70, 80, seed, ph * PHRASE_STEPS, EN_SEC_LIGHT, 2, 1, 5u, &a);
                    for (b = 1u; b < EN_ARR_PHRASE; b++) {
                        en_fx_plan_t p;
                        compose_at(fx, l, 70, 80, seed, (ph * EN_ARR_PHRASE + b) * BAR_STEPS + b, EN_SEC_LIGHT, 2, 1, 5u, &p);
                        CHECK(p.cycles == a.cycles && p.dir == a.dir && p.shape == a.shape && p.hz == a.hz, "%s changed inside a phrase, seed %u, bar %u", EN_FX_NAME[fx], seed, b);
                    }
                    compose_at(fx, l, 70, 80, seed, (ph + 1u) * PHRASE_STEPS - 1u, EN_SEC_LIGHT, 2, 1, 5u, &last);
                    {
                        en_fx_plan_t nx;
                        compose_at(fx, l, 70, 80, seed, (ph + 1u) * PHRASE_STEPS, EN_SEC_LIGHT, 2, 1, 5u, &nx);
                        seen_rate += nx.cycles != last.cycles || nx.hz != last.hz;
                        seen_dir += nx.dir != last.dir;
                        seen_shape += nx.shape != last.shape;
                    }
                    CHECK(a.dir == 1 || a.dir == -1, "dir %d", a.dir);
                    CHECK(a.depth >= 0.0f && a.depth <= 0.8f && a.open >= 0.0f && a.open <= 1.0f, "out of bounds: %f %f", (double)a.depth, (double)a.open);
                    CHECK(a.shape >= -0.25f && a.shape <= 0.5f, "shape %f", (double)a.shape);
                }
    printf("  Motion 70: of the phrase lines looked at, %d a new rate, %d a new direction, %d a new shape\n", seen_rate, seen_dir, seen_shape);
    CHECK(seen_rate > 200 && seen_dir > 500 && seen_shape > 500, "they hardly move: %d %d %d", seen_rate, seen_dir, seen_shape);
}
static void t_compose_rules(void)
{
    uint32_t seed, ph;
    int l;
    for (seed = 1u; seed < 40u; seed++)
        for (ph = 0u; ph < 20u; ph++)
            for (l = 0; l < 2; l++) {
                const uint32_t s = ph * PHRASE_STEPS;
                en_fx_plan_t a, b, c;
                /* Tremolo: by the section, at Motion 100 */
                compose_at(EN_FX_TREMOLO, l, 100, 50, seed, s, EN_SEC_DOWN, 2, 1, 0u, &a);
                compose_at(EN_FX_TREMOLO, l, 100, 50, seed, s, EN_SEC_LIGHT, 2, 1, 0u, &b);
                compose_at(EN_FX_TREMOLO, l, 100, 50, seed, s, EN_SEC_FULL, 2, 1, 0u, &c);
                CHECK(a.cycles == 8u && b.cycles == 16u && c.cycles == 32u, "Tremolo 1/2, 1/4, 1/8: %u %u %u", a.cycles, b.cycles, c.cycles);
                /* Autopan: slower in a breakdown */
                compose_at(EN_FX_AUTOPAN, l, 100, 50, seed, s, EN_SEC_DOWN, 2, 1, 0u, &a);
                compose_at(EN_FX_AUTOPAN, l, 100, 50, seed, s, EN_SEC_LIGHT, 2, 1, 0u, &b);
                compose_at(EN_FX_AUTOPAN, l, 100, 50, seed, s, EN_SEC_FULL, 2, 1, 0u, &c);
                CHECK(a.cycles == 1u && b.cycles == 2u && c.cycles >= 4u, "Autopan's rates: %u %u %u", a.cycles, b.cycles, c.cycles);
                /* Chorus: slower in a breakdown */
                compose_at(EN_FX_CHORUS, l, 100, 50, seed, s, EN_SEC_DOWN, 2, 1, 0u, &a);
                compose_at(EN_FX_CHORUS, l, 100, 50, seed, s, EN_SEC_FULL, 2, 1, 0u, &c);
                CHECK(a.hz < c.hz && a.hz > 0.2f && c.hz < 1.2f, "Chorus's rates: %f %f", (double)a.hz, (double)c.hz);
                /* the filter: opens across the phrase, closes by its end */
                {
                    float top = 0.0f, was = -1.0f;
                    int b_top = 0, k, rising = 1;
                    compose_at(EN_FX_FILTER, l, 100, 100, seed, s + 7u * BAR_STEPS, EN_SEC_FULL, 2, 1, 0u, &c);
                    CHECK(c.open < 1e-5f, "shut at the phrase's end: %f", (double)c.open);
                    for (k = 0; k < 8; k++) {
                        compose_at(EN_FX_FILTER, l, 100, 100, seed, s + (uint32_t)k * BAR_STEPS, EN_SEC_FULL, 2, 1, 0u, &a);
                        if (a.open > top)
                            top = a.open, b_top = k;
                        if (a.open < was && k <= b_top)
                            rising = 0;
                        was = a.open;
                    }
                    CHECK(rising && top > 0.95f && b_top >= 4, "it rises to its peak late: %f at bar %d", (double)top, b_top);
                    compose_at(EN_FX_FILTER, l, 100, 100, seed, s + (uint32_t)b_top * BAR_STEPS, EN_SEC_DOWN, 2, 1, 0u, &a);
                    CHECK(a.open < top - 0.2f, "the breakdown's opens less: %f, %f", (double)a.open, (double)top);
                    compose_at(EN_FX_FILTER, l, 50, 50, seed, s + (uint32_t)b_top * BAR_STEPS, EN_SEC_FULL, 2, 1, 0u, &b);
                    CHECK(fabsf(b.open - (0.5f + 0.25f)) < 0.02f, "Depth shuts, Motion opens: %f", (double)b.open);
                }
                /* the last bar of a phrase eases the Tremolo; the Chorus deepens through it */
                compose_at(EN_FX_TREMOLO, l, 100, 50, seed, s + 6u * BAR_STEPS, EN_SEC_LIGHT, 4, 1, 0u, &a);
                compose_at(EN_FX_TREMOLO, l, 100, 50, seed, s + 7u * BAR_STEPS, EN_SEC_LIGHT, 4, 1, 0u, &b);
                CHECK(b.depth < a.depth, "Tremolo eases: %f %f", (double)a.depth, (double)b.depth);
                compose_at(EN_FX_CHORUS, l, 100, 50, seed, s, EN_SEC_LIGHT, 4, 1, 0u, &a);
                compose_at(EN_FX_CHORUS, l, 100, 50, seed, s + 7u * BAR_STEPS, EN_SEC_LIGHT, 4, 1, 0u, &b);
                CHECK(a.depth < b.depth, "Chorus deepens: %f %f", (double)a.depth, (double)b.depth);
            }
    /* they follow the chords and the melody */
    for (seed = 1u; seed < 20u; seed++) {
        en_fx_plan_t a, b;
        compose_at(EN_FX_CHORUS, EN_L_KEYS, 80, 60, seed, 16u, EN_SEC_LIGHT, 1, 0, 0u, &a);
        compose_at(EN_FX_CHORUS, EN_L_KEYS, 80, 60, seed, 16u, EN_SEC_LIGHT, 4, 0, 0u, &b);
        CHECK(a.depth < b.depth, "a long chord deeper: %f %f", (double)a.depth, (double)b.depth);
        compose_at(EN_FX_TREMOLO, EN_L_MEL, 80, 60, seed, 16u, EN_SEC_LIGHT, 1, 0, 0u, &a);
        compose_at(EN_FX_TREMOLO, EN_L_MEL, 80, 60, seed, 16u, EN_SEC_LIGHT, 1, 1, 0u, &b);
        CHECK(a.depth < b.depth, "less with no melody: %f %f", (double)a.depth, (double)b.depth);
        compose_at(EN_FX_AUTOPAN, EN_L_MEL, 80, 60, seed, 16u, EN_SEC_LIGHT, 1, 1, 6u, &a);
        compose_at(EN_FX_AUTOPAN, EN_L_MEL, 80, 60, seed, 16u, EN_SEC_LIGHT, 1, 1, 7u, &b);
        CHECK(a.dir == -b.dir, "a new phrase of the melody, the other way");
        compose_at(EN_FX_FILTER, EN_L_MEL, 100, 100, seed, 3u * BAR_STEPS, EN_SEC_FULL, 1, 0, 0u, &a);
        compose_at(EN_FX_FILTER, EN_L_MEL, 100, 100, seed, 3u * BAR_STEPS, EN_SEC_FULL, 1, 1, 0u, &b);
        CHECK(a.open < b.open, "the filter's more open with a melody: %f %f", (double)a.open, (double)b.open);
    }
    /* a low Motion takes the composed choice seldom, a high one always */
    {
        int low = 0, n = 0, high = 0;
        for (seed = 1u; seed < 200u; seed++) {
            en_fx_plan_t a;
            compose_at(EN_FX_TREMOLO, EN_L_KEYS, 10, 50, seed, 0u, EN_SEC_FULL, 2, 1, 0u, &a);
            low += a.cycles == 32u;
            compose_at(EN_FX_TREMOLO, EN_L_KEYS, 100, 50, seed, 0u, EN_SEC_FULL, 2, 1, 0u, &a);
            high += a.cycles == 32u;
            n++;
        }
        CHECK(low > 0 && low * 4 < n && high == n, "takes the section's: at 10 %d, at 100 %d, of %d", low, high, n);
    }
}

/* ---- the same in the engine: Motion 0 stands, whatever the song does; above it, the lines; the seed says all of it. Sections go by en_next */
static uint32_t q_hash;
static void q_mix(float x)
{
    union { float f; uint32_t u; } v;
    v.f = x;
    q_hash = (q_hash ^ v.u) * 16777619u;
}
static const int Q_ALL[6] = {EN_FXQ_CYCLES, EN_FXQ_DEPTH, EN_FXQ_SHAPE, EN_FXQ_HZ, EN_FXQ_OPEN, EN_FXQ_FC};
static void t_engine_still(void)
{
    int fx, l;
    for (fx = 1; fx < EN_NFX; fx++)
        for (l = 0; l < 2; l++) {
            float first[6] = {0};
            uint32_t b, i;
            start(99u, EN_AUTO, EN_ST_LIQUID);
            layers(1, 1, 1, 1, 1);
            en_set(P_MELMODE, EN_MEL_ARP);
            fx_set(l, fx, 0, 70);
            en_play(1);
            render(SECS(0.5f));
            for (i = 0u; i < 6u; i++)
                first[i] = en_test_fx_q(l, Q_ALL[i]);
            for (b = 0u; b < SECS(120); b++) {
                render(1u);
                if (b % SECS(20) == 100u)
                    en_next();
                if (b % 8u)
                    continue;
                for (i = 0u; i < 6u; i++)
                    CHECK(en_test_fx_q(l, Q_ALL[i]) == first[i], "%s, layer %d, Motion 0: query %d moved at block %u: %f %f", EN_FX_NAME[fx], l, Q_ALL[i], b, (double)first[i], (double)en_test_fx_q(l, Q_ALL[i]));
                if (fails > 20)
                    return;
            }
        }
}
static uint32_t en_song_bar(void) { return en_test_song() ? (en_test_song() - 1u) / 16u : 0u; }
static uint32_t moved_trace(int fx, int l, uint32_t seed, int motion, int *line_moves, int *off_line)
{
    uint32_t b, was_bar = 0u;
    start(seed, EN_AUTO, EN_ST_LIQUID);
    layers(1, 1, 1, 1, 1);
    en_set(P_MELMODE, EN_MEL_ARP);
    fx_set(l, fx, motion, 70);
    en_play(1);
    q_hash = 2166136261u;
    *line_moves = *off_line = 0;
    for (b = 0u; b < SECS(180); b++) {
        const float r = en_test_fx_q(l, EN_FXQ_CYCLES);
        const uint32_t bar = en_song_bar();
        render(1u);
        if (b > 20u && (en_test_fx_q(l, EN_FXQ_CYCLES) != r)) {
            const uint32_t nb = en_song_bar();
            if (nb % EN_ARR_PHRASE == 0u)
                ++*line_moves;
            else
                ++*off_line;
        }
        if (bar != was_bar) {
            int i;
            was_bar = bar;
            for (i = 0; i < 6; i++)
                q_mix(en_test_fx_q(l, Q_ALL[i]));
        }
    }
    return q_hash;
}
static void t_engine_moves(void)
{
    int fx, l;
    for (fx = 1; fx < EN_NFX; fx++)
        for (l = 0; l < 2; l++) {
            int on, off, on2, off2;
            const uint32_t a = moved_trace(fx, l, 7u, 70, &on, &off), a2 = moved_trace(fx, l, 7u, 70, &on2, &off2), b = moved_trace(fx, l, 8u, 70, &on2, &off2);
            CHECK(a == a2, "%s, layer %d: the same seed, another movement", EN_FX_NAME[fx], l);
            CHECK(a != b, "%s, layer %d: another seed, the same movement", EN_FX_NAME[fx], l);
            CHECK(off == 0, "%s, layer %d: its rate moved %d times off a phrase line", EN_FX_NAME[fx], l, off);
            if (fx == EN_FX_AUTOPAN || fx == EN_FX_TREMOLO) {
                moved_trace(fx, l, 7u, 70, &on, &off);
                CHECK(on >= 2, "%s, layer %d: its rate moved only %d times", EN_FX_NAME[fx], l, on);
            }
        }
}

/* the wave on the grid: at every phrase line, within a hair of its start; through a section's EDIT, a rest, and a clock from outside it comes back. A new rate goes on with no jump */
static void t_grid_locked(void)
{
    int fx;
    for (fx = EN_FX_AUTOPAN; fx <= EN_FX_TREMOLO; fx++) {
        uint32_t b, was = 0u, lines = 0u;
        float worst = 0.0f;
        start(11u, EN_AUTO, EN_ST_LIQUID);
        layers(1, 1, 1, 1, 1);
        fx_set(EN_L_KEYS, fx, 100, 80);
        en_play(1);
        render(SECS(1));
        for (b = 0u; b < SECS(240); b++) {
            const uint32_t s = en_test_song();
            render(1u);
            if (s != en_test_song() && en_test_song() % PHRASE_STEPS == 1u && en_test_song() > 1u && en_test_song() != was) {
                float ph = en_test_fx_q(EN_L_KEYS, EN_FXQ_PHASE), e;
                e = ph > 0.5f ? 1.0f - ph : ph;  /* (a block of its own after the line) */
                worst = e > worst ? e : worst;
                was = en_test_song();
                lines++;
            }
        }
        printf("  %s: %u phrase lines, at most %.4f of a turn off its start, a block in\n", EN_FX_NAME[fx], lines, (double)worst);
        CHECK(lines > 15u && worst < 0.2f, "%s: %u lines, %f off the grid", EN_FX_NAME[fx], lines, (double)worst);
    }
    /* a clock from outside, a little off the Tempo knob: it stays on the clock's grid */
    for (fx = EN_FX_AUTOPAN; fx <= EN_FX_TREMOLO; fx++) {
        float worst = 0.0f;
        double due = 0.0;
        const double tick = 44100.0 * 60.0 / (178.0 * 24.0);
        uint32_t b, frames = 0u;
        start(13u, EN_HANDS, EN_ST_LIQUID);
        layers(1, 0, 0, 0, 0);
        en_set(P_TEMPO, 170);
        fx_set(EN_L_KEYS, fx, 0, 80);
        en_clock(EN_CLK_START);
        render(2u);
        for (b = 0u; b < SECS(60); b++) {
            while ((double)frames >= due) {
                en_clock(EN_CLK_TICK);
                due += tick;
            }
            {
                const uint32_t was = en_test_song();
                render(1u);
                frames += BLOCK;
                if (was == en_test_song())
                    continue;                    /* (right after its step: a block of the wave's own at most) */
            }
            if (b > SECS(5)) {
                float ph = en_test_fx_q(EN_L_KEYS, EN_FXQ_PHASE);
                float t = (float)((en_test_song() - 1u) % 64u) * en_test_fx_q(EN_L_KEYS, EN_FXQ_CYCLES) * (1.0f / 64.0f), e;
                t -= floorf(t);
                e = fabsf(ph - t);
                e = e > 0.5f ? 1.0f - e : e;
                worst = e > worst ? e : worst;
            }
        }
        printf("  %s on a clock at 178, Tempo 170: at most %.3f of a turn off the grid\n", EN_FX_NAME[fx], (double)worst);
        CHECK(worst < 0.12f, "%s: off an outside clock's grid by %f", EN_FX_NAME[fx], (double)worst);
    }
}

/* ---- the effects by themselves: a steady signal through the render's own code */
static float *outl, *outr;
static uint32_t nout;
static float tone_ph;
static float sig_tone(float hz)
{
    const float x = sinf(tone_ph * 6.2831853f);
    tone_ph += hz / FS;
    if (tone_ph >= 1.0f)
        tone_ph -= 1.0f;
    return x;
}
static uint32_t rng32 = 1u;
static float sig_noise(void)
{
    rng32 ^= rng32 << 13;
    rng32 ^= rng32 >> 17;
    rng32 ^= rng32 << 5;
    return (float)(int32_t)rng32 * 4.6566e-10f * 0.5f;
}
/* the layer's bus alone, with a steady tone (or noise, hz 0) */
static void through(int layer, float hz, float amp, uint32_t blocks)
{
    static float il[BLOCK], ir[BLOCK], ol[BLOCK], orr[BLOCK];
    uint32_t b, j;
    for (b = 0u; b < blocks; b++) {
        for (j = 0u; j < BLOCK; j++)
            il[j] = ir[j] = amp * (hz > 0.0f ? sig_tone(hz) : sig_noise());
        en_test_fx_run(layer, il, ir, 0, 0, ol, orr, 0, 0, BLOCK);
        for (j = 0u; j < BLOCK; j++)
            if (nout < 44100u * 100u) {
                outl[nout] = ol[j];
                outr[nout++] = orr[j];
            }
    }
}
static void bus_start(int layer, int fx, int motion, int depth, int tempo)
{
    en_init();
    en_set(P_TEMPO, tempo);
    fx_set(layer, fx, motion, depth);
    en_render(blk, BLOCK, GAIN);                 /* (takes the commands; nothing is playing) */
    nout = 0u;
    tone_ph = 0.0f;
    through(layer, 300.0f, 0.3f, 20u);           /* its fade is over */
    nout = 0u;
}
/* an envelope at this rate: how much of it is there, by a single bin */
static float env_bin(const float *x, uint32_t n, float hz)
{
    float re = 0.0f, im = 0.0f, ph = 0.0f, mean = 0.0f;
    uint32_t i;
    for (i = 0u; i < n; i++)
        mean += fabsf(x[i]);
    mean /= (float)n;
    for (i = 0u; i < n; i++) {
        const float a = fabsf(x[i]) - mean;
        re += a * cosf(ph);
        im += a * sinf(ph);
        ph += 6.2831853f * hz / FS;
        if (ph > 6.2831853f)
            ph -= 6.2831853f;
    }
    return sqrtf(re * re + im * im) * 2.0f / (float)n;
}
static float peak_rate(const float *x, uint32_t n)   /* the strongest rate in the envelope, 0.2 to 12 Hz */
{
    float best = 0.0f, at = 0.0f, hz;
    for (hz = 0.2f; hz < 12.0f; hz *= 1.01f) {
        const float v = env_bin(x, n, hz);
        if (v > best)
            best = v, at = hz;
    }
    return at;
}
static float rms_of(const float *x, uint32_t n)
{
    double s = 0.0;
    uint32_t i;
    for (i = 0u; i < n; i++)
        s += (double)x[i] * (double)x[i];
    return (float)sqrt(s / (double)n);
}
static void t_tremolo_rate(void)
{
    static const int TEMPO[3] = {150, 170, 180};
    int t, l;
    for (l = 0; l < 2; l++)
        for (t = 0; t < 3; t++) {
            const float beat = (float)TEMPO[t] / 60.0f;
            float rate, swing, base;
            bus_start(l, EN_FX_TREMOLO, 0, 50, TEMPO[t]);
            through(l, 300.0f, 0.3f, SECS(16));
            rate = peak_rate(outl, nout);
            CHECK(fabsf(rate / beat - 1.0f) < 0.015f, "Tremolo at %d: %.3f Hz, the beat is %.3f", TEMPO[t], (double)rate, (double)beat);
            swing = env_bin(outl, nout, beat);
            base = 0.3f * 0.6366f;               /* a sine's mean */
            printf("  Tremolo, layer %d, %d BPM: %.3f Hz, in quarters %.3f; swing +-%.1f%% at Depth 50\n", l, TEMPO[t], (double)rate, (double)beat, (double)(swing / base * 100.0f));
            CHECK(swing / base > 0.15f && swing / base < 0.25f, "its swing, by default no pump: %.3f", (double)(swing / base));
            CHECK(fabsf(20.0f * log10f(rms_of(outl, nout) / (0.3f * 0.7071f))) < 0.25f, "its level: %f", (double)rms_of(outl, nout));
            CHECK(rms_of(outl, nout) == rms_of(outr, nout), "both sides the same");
        }
}
static void t_autopan_rate(void)
{
    static const int TEMPO[3] = {150, 170, 180};
    int t, l;
    for (l = 0; l < 2; l++)
        for (t = 0; t < 3; t++) {
            const float bar = (float)TEMPO[t] / 240.0f;
            uint32_t i;
            float rate, swing_l, swing_r;
            double mono = 0.0, corr = 0.0, a2 = 0.0, b2 = 0.0, ma = 0.0, mb = 0.0;
            static double envl[1000], envr[1000];
            bus_start(l, EN_FX_AUTOPAN, 0, 100, TEMPO[t]);
            through(l, 300.0f, 0.3f, SECS(32));
            rate = peak_rate(outl, nout);
            CHECK(fabsf(rate / bar - 1.0f) < 0.015f, "Autopan at %d: %.3f Hz, a bar is %.3f", TEMPO[t], (double)rate, (double)bar);
            swing_l = env_bin(outl, nout, bar);
            swing_r = env_bin(outr, nout, bar);
            CHECK(swing_l > 0.1f && swing_r > 0.1f, "it moves: %f %f", (double)swing_l, (double)swing_r);
            for (i = 0u; i < nout; i++)
                mono += (double)outl[i] * (double)outl[i] + (double)outr[i] * (double)outr[i];
            {                                    /* the sides' levels, a tenth of a second at a time */
                const uint32_t w = 4410u;
                uint32_t k, nw = 0u;
                for (i = 0u; i + w <= nout; i += w, nw++) {
                    double l2 = 0.0, r2 = 0.0;
                    for (k = 0u; k < w; k++) {
                        l2 += (double)outl[i + k] * (double)outl[i + k];
                        r2 += (double)outr[i + k] * (double)outr[i + k];
                    }
                    envl[nw] = sqrt(l2 / w);
                    envr[nw] = sqrt(r2 / w);
                    ma += envl[nw];
                    mb += envr[nw];
                }
                ma /= (double)nw;
                mb /= (double)nw;
                for (i = 0u; i < nw; i++) {
                    const double a = envl[i] - ma, b = envr[i] - mb;
                    corr += a * b;
                    a2 += a * a;
                    b2 += b * b;
                }
            }
            corr /= sqrt(a2 * b2);
            printf("  Autopan, layer %d, %d BPM: %.3f Hz, a bar %.3f; the sides against each other %.2f\n", l, TEMPO[t], (double)rate, (double)bar, corr);
            CHECK(corr < -0.9, "the sides go opposite ways: %f", corr);
            CHECK(fabs(10.0 * log10(mono / (double)nout / (2.0 * 0.3 * 0.3 * 0.5))) < 0.25, "equal power");
        }
}
/* a filter's gain at a frequency: a sine through it */
static float filter_gain(int layer, float hz)
{
    bus_start(layer, EN_FX_FILTER, 0, en_test_par(fx_par(layer) + 2), 170);
    through(layer, hz, 0.3f, SECS(0.3f));
    return 20.0f * log10f(rms_of(outl + 2000, nout - 2000u) / (0.3f * 0.7071f));
}
static void t_filter_spectrum(void)
{
    int l;
    for (l = 0; l < 2; l++) {
        static const int DEPTH[3] = {100, 50, 0};
        int d;
        for (d = 0; d < 3; d++) {
            float g_lo, g_hi, most = -99.0f, hz;
            en_init();
            en_set(fx_par(l) + 2, DEPTH[d]);
            en_render(blk, BLOCK, GAIN);
            g_lo = filter_gain(l, 200.0f);
            g_hi = filter_gain(l, 6000.0f);
            for (hz = 200.0f; hz < 8000.0f; hz *= 1.2f) {
                const float g = filter_gain(l, hz);
                most = g > most ? g : most;
            }
            printf("  Filter, layer %d, Depth %3d, still: at %.0f Hz; 200 Hz %+.1f dB, 6 kHz %+.1f dB, its most %+.1f dB\n", l, DEPTH[d], (double)en_test_fx_q(l, EN_FXQ_FC), (double)g_lo, (double)g_hi, (double)most);
            CHECK(fabsf(g_lo) < 0.5f, "the low end stays: %f", (double)g_lo);
            CHECK(most < 1.0f, "no resonance to whistle: %f", (double)most);
            if (DEPTH[d] == 100)
                CHECK(g_hi < -20.0f, "shut at Depth 100: %f", (double)g_hi);
            if (DEPTH[d] == 0)
                CHECK(g_hi > -3.0f, "open at Depth 0: %f", (double)g_hi);
        }
    }
}
/* it moves in the song: shut at a phrase's top, higher later, in octaves; and it takes its sends with it */
static void t_filter_moves(void)
{
    int l;
    for (l = 0; l < 2; l++) {
        uint32_t b;
        float lo = 1e9f, hi = 0.0f;
        start(21u, EN_AUTO, EN_ST_LIQUID);
        layers(1, 1, 1, 1, 1);
        en_set(P_MELMODE, EN_MEL_ARP);
        fx_set(l, EN_FX_FILTER, 100, 100);
        en_play(1);
        en_next();
        render(SECS(1));
        for (b = 0u; b < SECS(180); b++) {
            const float fc = en_test_fx_q(l, EN_FXQ_FC);
            render(1u);
            lo = fc < lo ? fc : lo;
            hi = fc > hi ? fc : hi;
        }
        printf("  Filter, layer %d, Motion 100, Depth 100, in the song: %.0f to %.0f Hz\n", l, (double)lo, (double)hi);
        CHECK(hi / lo > 3.0f && lo < 2000.0f && hi > 3500.0f && hi <= 7001.0f, "it should sweep: %f to %f", (double)lo, (double)hi);
    }
    {   /* the room hears it too */
        static float il[BLOCK], ir[BLOCK], ol[BLOCK], orr[BLOCK], rs[BLOCK], es[BLOCK], ors[BLOCK], oes[BLOCK];
        uint32_t b, j;
        double in_e = 0.0, out_r = 0.0, out_e = 0.0;
        bus_start(EN_L_KEYS, EN_FX_FILTER, 0, 100, 170);
        for (b = 0u; b < 200u; b++) {
            for (j = 0u; j < BLOCK; j++) {
                il[j] = ir[j] = rs[j] = es[j] = 0.3f * sig_tone(6000.0f);
            }
            en_test_fx_run(EN_L_KEYS, il, ir, rs, es, ol, orr, ors, oes, BLOCK);
            for (j = 0u; j < BLOCK; j++) {
                in_e += (double)rs[j] * (double)rs[j];
                out_r += (double)ors[j] * (double)ors[j];
                out_e += (double)oes[j] * (double)oes[j];
            }
        }
        CHECK(out_r < in_e * 0.1 && out_e < in_e * 0.1, "the sends are darker: %f %f of %f", out_r, out_e, in_e);
    }
}
/* the chorus: its delay alone has a pitch that moves */
static void cents_of(const float *x, uint32_t n, float hz, float *lo, float *hi)
{
    uint32_t i, last = 0u, cross = 0u;
    double last_t = 0.0, prev_t = -1.0;
    uint32_t c = 0u;
    double sum = 0.0;
    *lo = 1e9f;
    *hi = -1e9f;
    (void)last;
    (void)cross;
    for (i = 1u; i < n; i++)
        if (x[i - 1] < 0.0f && x[i] >= 0.0f) {
            const double t = (double)(i - 1u) + (double)(x[i - 1] / (x[i - 1] - x[i]));
            if (prev_t >= 0.0) {
                sum += t - last_t;
                if (++c == 200u) {               /* a smoothed period over 200 cycles */
                    const float f = (float)(44100.0 / (sum / 200.0)), ct = 1200.0f * log2f(f / hz);
                    *lo = ct < *lo ? ct : *lo;
                    *hi = ct > *hi ? ct : *hi;
                    c = 0u;
                    sum = 0.0;
                }
            }
            prev_t = last_t;
            last_t = t;
        }
}
static void t_chorus_detune(void)
{
    int l, d;
    for (l = 0; l < 2; l++)
        for (d = 0; d < 3; d++) {
            static const int DEPTH[3] = {25, 50, 100};
            float lo, hi, lor, hir;
            uint32_t i;
            int differ = 0;
            bus_start(l, EN_FX_CHORUS, 0, DEPTH[d], 170);
            en_test_fx_dry = 0.0f;
            nout = 0u;
            through(l, 2000.0f, 0.3f, SECS(0.1f));
            nout = 0u;
            through(l, 2000.0f, 0.3f, SECS(8));
            cents_of(outl, nout, 2000.0f, &lo, &hi);
            cents_of(outr, nout, 2000.0f, &lor, &hir);
            for (i = 0u; i < nout; i += 100u)
                differ += fabsf(outl[i] - outr[i]) > 0.02f;
            en_test_fx_dry = 1.0f;
            printf("  Chorus, layer %d, Depth %3d, the delay alone: %+.1f to %+.1f cents, right %+.1f to %+.1f\n", l, DEPTH[d], (double)lo, (double)hi, (double)lor, (double)hir);
            CHECK(hi > 2.0f && lo < -2.0f, "a modulated delay's detune: %f %f", (double)lo, (double)hi);
            CHECK(hi < 15.0f && lo > -15.0f, "no out of tune: %f %f", (double)lo, (double)hi);
            CHECK(differ > 100, "wide: the sides differ: %d", differ);
        }
    /* bounded at any rate the instrument can compose */
    {
        en_fx_plan_t p;
        uint32_t seed;
        float top = 0.0f;
        for (seed = 1u; seed < 200u; seed++) {
            compose_at(EN_FX_CHORUS, EN_L_KEYS, 100, 100, seed, 0u, EN_SEC_FULL, 4, 1, 0u, &p);
            top = p.hz > top ? p.hz : top;
        }
        CHECK(top < 1.0f, "Chorus's rate: %f", (double)top);
    }
}

/* ---- the sends: 100 as ever, 0 nothing, 200 twice; both ways, smoothly. The room's own hear them */
static void t_sends(void)
{
    static float il[BLOCK], ir[BLOCK], ol[BLOCK], orr[BLOCK], rs[BLOCK], es[BLOCK], ors[BLOCK], oes[BLOCK];
    int l;
    uint32_t j, b;
    for (j = 0u; j < BLOCK; j++) {
        il[j] = ir[j] = 0.1f;
        rs[j] = 0.2f;
        es[j] = 0.3f;
    }
    for (l = 0; l < 2; l++) {
        static const int V[3] = {0, 100, 200};
        int a;
        for (a = 0; a < 3; a++) {
            float worst = 0.0f, last = -1.0f;
            en_init();
            en_set(fx_par(l) + 3, 100);
            en_set(fx_par(l) + 4, 100);
            en_render(blk, BLOCK, GAIN);
            CHECK(en_test_fx_q(l, EN_FXQ_ECHO) == 1.0f && en_test_fx_q(l, EN_FXQ_REV) == 1.0f && en_test_fx_q(l, EN_FXQ_BUS) == 0.0f, "100: as ever, by no bus");
            en_set(fx_par(l) + 3, V[a]);
            en_set(fx_par(l) + 4, V[2 - a]);         /* (each its own) */
            en_render(blk, BLOCK, GAIN);
            for (b = 0u; b < 200u; b++) {
                en_test_fx_run(l, il, ir, rs, es, ol, orr, ors, oes, BLOCK);
                for (j = 0u; j < BLOCK; j++) {
                    if (last >= 0.0f)
                        worst = fmaxf(worst, fabsf(ors[j] - last));
                    last = ors[j];
                }
            }
            CHECK(fabsf(ors[0] - 0.2f * (float)V[2 - a] * 0.01f) < 1e-3f && fabsf(oes[0] - 0.3f * (float)V[a] * 0.01f) < 1e-3f, "%d: %f %f", V[a], (double)ors[0], (double)oes[0]);
            CHECK(ol[0] == 0.1f, "the mix does not know: %f", (double)ol[0]);
            CHECK(worst < 0.001f, "a send's move: a step of %f", (double)worst);
            if (V[a] != 100)
                CHECK(en_test_fx_q(l, EN_FXQ_BUS) == 1.0f, "a bus while it is not as ever");
            en_set(fx_par(l) + 3, 100);
            en_set(fx_par(l) + 4, 100);
            en_render(blk, BLOCK, GAIN);
            for (b = 0u; b < 200u; b++)
                en_test_fx_run(l, il, ir, rs, es, ol, orr, ors, oes, BLOCK);
            CHECK(en_test_fx_q(l, EN_FXQ_BUS) == 0.0f, "back: by no bus again");
        }
    }
    /* in the engine, with the room: a layer's reverb at 0 and 200 */
    for (l = 0; l < 2; l++) {
        float e[3];
        static const int V[3] = {0, 100, 200};
        int a;
        for (a = 0; a < 3; a++) {
            start(55u, EN_HANDS, EN_ST_LIQUID);
            layers(l == EN_L_KEYS, l == EN_L_MEL, 0, 0, 0);
            en_test_pad_off = 1;
            en_set(P_MELMODE, EN_MEL_ARP);
            en_set(P_ECHO, 0);
            en_set(P_REVERB, 100);
            en_set(fx_par(l) + 4, V[a]);
            en_play(1);
            capture(SECS(15));
            e[a] = rms_of(cap, ncap);
        }
        printf("  layer %d, its reverb 0, 100, 200: %.1f, %.1f, %.1f dB\n", l, 20.0 * log10((double)e[0]), 20.0 * log10((double)e[1]), 20.0 * log10((double)e[2]));
        CHECK(e[0] < e[1] && e[1] < e[2], "the more, the more: %f %f %f", (double)e[0], (double)e[1], (double)e[2]);
    }
}

/* ---- levels: each effect, at every setting, against Off, in the song. The keys alone and the melody alone, with no room, whose own part would hide it */
static float layer_rms(int layer, int fx, int motion, int depth, uint32_t seed)
{
    start(seed, EN_HANDS, EN_ST_LIQUID);
    layers(layer == EN_L_KEYS, layer == EN_L_MEL, 0, 0, 0);
    en_test_pad_off = 1;
    en_set(P_MELMODE, EN_MEL_ARP);
    en_set(P_MRATE, 50);
    en_set(P_ECHO, 0);
    en_set(P_REVERB, 0);
    fx_set(layer, fx, motion, depth);
    en_play(1);
    capture(SECS(30));
    return rms_of(cap, ncap);
}
static void t_levels(void)
{
    static const int MOTION[3] = {0, 50, 100}, DEPTH[3] = {25, 50, 100};
    int l, fx, m, d;
    for (l = 0; l < 2; l++) {
        const float off = layer_rms(l, EN_FX_OFF, 50, 50, 31u);
        for (fx = 1; fx < EN_NFX; fx++) {
            float lo = 99.0f, hi = -99.0f;
            for (m = 0; m < 3; m++)
                for (d = 0; d < 3; d++) {
                    const float r = layer_rms(l, fx, MOTION[m], DEPTH[d], 31u), db = 20.0f * log10f(r / off);
                    lo = db < lo ? db : lo;
                    hi = db > hi ? db : hi;
                    CHECK(fabsf(db) < LEVEL_DB, "%s, layer %d, Motion %d, Depth %d: %+.2f dB against Off", EN_FX_NAME[fx], l, MOTION[m], DEPTH[d], (double)db);
                }
            printf("  %-8s layer %d: %+.2f to %+.2f dB against Off, over Motion and Depth\n", EN_FX_NAME[fx], l, (double)lo, (double)hi);
        }
    }
    /* the melody two octaves up, and two down: the filter's floor goes with it, so it takes colour and not level */
    {
        static const int OCT[2] = {2, -2};
        for (d = 0; d < 2; d++) {
            float off = 1.0f, r = 1.0f;
            for (m = 0; m < 2; m++) {
                start(31u, EN_HANDS, EN_ST_LIQUID);
                layers(0, 1, 0, 0, 0);
                en_test_pad_off = 1;
                en_set(P_MELMODE, EN_MEL_ARP);
                en_set(P_ECHO, 0);
                en_set(P_REVERB, 0);
                en_set(P_MOCT, OCT[d]);
                fx_set(EN_L_MEL, m ? EN_FX_FILTER : EN_FX_OFF, 0, 100);
                en_play(1);
                capture(SECS(30));
                r = rms_of(cap, ncap);
                if (!m)
                    off = r;
            }
            printf("  Filter, the melody at octave %+d, Depth 100, still: %+.2f dB against Off\n", OCT[d], (double)(20.0f * log10f(r / off)));
            CHECK(fabsf(20.0f * log10f(r / off)) < LEVEL_DB, "octave %d: %f dB", OCT[d], (double)(20.0f * log10f(r / off)));
        }
    }
}

/* the filter follows: a note lifts it and it does not shut below the note, at any Motion but 0; a new chord, likewise, in the song */
static uint32_t chord_seen;
static void on_chord(uint32_t step, const en_gchord_t *c, int character)
{
    (void)step;
    (void)c;
    (void)character;
    chord_seen++;
}
static void t_filter_follows(void)
{
    int m;
    for (m = 0; m < 2; m++) {
        float before, after;
        start(9u, EN_HANDS, EN_ST_LIQUID);
        layers(0, 1, 0, 0, 0);
        fx_set(EN_L_MEL, EN_FX_FILTER, m ? 80 : 0, 100);
        render(SECS(1));
        before = en_test_fx_q(EN_L_MEL, EN_FXQ_FC);
        en_test_bell_note(0, 96, 0.9f);          /* C7: 2093 Hz */
        render(SECS(0.5f));
        after = en_test_fx_q(EN_L_MEL, EN_FXQ_FC);
        if (m)
            CHECK(after > before * 1.3f && after >= 1.3f * 2093.0f * 0.99f, "a note: %f, then %f", (double)before, (double)after);
        else
            CHECK(after == before, "Motion 0 does not follow: %f, then %f", (double)before, (double)after);
    }
    {
        uint32_t b, was, chords = 0u, up = 0u;
        float last;
        start(10u, EN_HANDS, EN_ST_LIQUID);
        layers(1, 0, 0, 0, 0);
        fx_set(EN_L_KEYS, EN_FX_FILTER, 100, 100);
        en_play(1);
        render(SECS(2));
        en_trace_chord = on_chord;
        chord_seen = 0u;
        was = 0u;
        last = en_test_fx_q(EN_L_KEYS, EN_FXQ_FC);
        for (b = 0u; b < SECS(120); b++) {
            render(1u);
            if (chord_seen != was) {
                float after;
                was = chord_seen;
                render(4u);
                after = en_test_fx_q(EN_L_KEYS, EN_FXQ_FC);
                chords++;
                up += after > last;
                b += 4u;
            }
            last = en_test_fx_q(EN_L_KEYS, EN_FXQ_FC);
        }
        en_trace_chord = 0;
        printf("  the filter on a new chord: up on %u of %u\n", up, chords);
        CHECK(chords > 10u && up * 10u > chords * 8u, "a new chord lifts it: %u of %u", up, chords);
    }
}

/* ---- no clicks. The measure of sound_test: the largest second difference, the change against what either side does by itself */
static float d2_of(uint32_t from, uint32_t to)
{
    float m = 0.0f;
    uint32_t j;
    int c;
    for (c = 0; c < 2; c++)
        for (j = from < 2u ? 2u : from; j < to && j * 2u + (uint32_t)c < ncap; j++) {
            const float d = fabsf(cap[j * 2u + (uint32_t)c] - 2.0f * cap[(j - 1u) * 2u + (uint32_t)c] + cap[(j - 2u) * 2u + (uint32_t)c]);
            m = d > m ? d : m;
        }
    return m;
}
static void quiet_room(void)
{
    en_set(P_ECHO, 0);
    en_set(P_REVERB, 0);
    en_set(P_MOTIFS, 60);
    en_set(P_BASS, 0);
    en_set(P_DRUMS, 0);
    en_set(P_HATS, 0);
}
static void up_note(int layer, int fx, int motion)
{
    static const int8_t CH[4] = {48, 60, 64, 67};
    int k;
    start(3u, EN_HANDS, EN_ST_LIQUID);
    layers(layer == EN_L_KEYS, layer == EN_L_MEL, 0, 0, 0);
    en_test_pad_off = 1;
    quiet_room();
    fx_set(layer, fx, motion, 100);
    render(2u);
    if (layer == EN_L_KEYS) {
        for (k = 0; k < 4; k++)
            en_test_key_note(CH[k], 0.9f);
    } else {
        en_test_bell_note(0, 72, 0.9f);
    }
    render(SECS(0.3f));
}
static void t_click_changes(void)
{
    int l;
    for (l = 0; l < 2; l++) {
        int a, b;
        float worst = 0.0f;
        for (a = 0; a < EN_NFX; a++)
            for (b = 0; b < EN_NFX; b++) {
                int off;
                if (a == b)
                    continue;
                for (off = 0; off < 3; off++) {
                    float before, across, after, r;
                    up_note(l, a, 50);
                    render((uint32_t)off);
                    ncap = 0u;
                    capture(20u);
                    before = d2_of(0u, ncap / 2u);
                    en_set(fx_par(l), b);
                    capture(40u);
                    across = d2_of(20u * BLOCK, ncap / 2u);
                    capture(40u);
                    after = d2_of(60u * BLOCK, ncap / 2u);
                    r = across / (fmaxf(before, after) + 1e-6f);
                    worst = r > worst ? r : worst;
                    CHECK(r < 1.6f, "layer %d, %s to %s: %.6f across, %.6f before, %.6f after", l, EN_FX_NAME[a], EN_FX_NAME[b], (double)across, (double)before, (double)after);
                }
            }
        printf("  a change of effect in mid-note, layer %d: at most %.2f times what they do by themselves\n", l, (double)worst);
    }
}
/* rapid changes: of everything, on a held note: against the same note with no changes. Then in the song */
static void t_click_rapid(void)
{
    int l, fx;
    for (l = 0; l < 2; l++)
        for (fx = 0; fx < EN_NFX; fx++) {
            float steady, rapid;
            uint32_t b;
            up_note(l, fx ? fx : EN_FX_FILTER, 50);
            ncap = 0u;
            capture(60u);
            steady = d2_of(0u, ncap / 2u);
            up_note(l, fx ? fx : EN_FX_FILTER, 50);
            ncap = 0u;
            for (b = 0u; b < 60u; b++) {
                if (fx) {                        /* Motion, Depth, sends, as fast as blocks */
                    en_set(fx_par(l) + 1, (int)(b * 37u % 101u));
                    en_set(fx_par(l) + 2, (int)(b * 53u % 101u));
                    en_set(fx_par(l) + 3, (int)(b * 71u % 201u));
                    en_set(fx_par(l) + 4, (int)(b * 91u % 201u));
                } else {                         /* the effect itself, every other block */
                    if (b & 1u)
                        en_set(fx_par(l), (int)(b / 2u % EN_NFX));
                }
                capture(1u);
            }
            rapid = d2_of(0u, ncap / 2u);
            printf("  rapid changes, layer %d, %s: %.6f, steady %.6f\n", l, fx ? EN_FX_NAME[fx] : "(effects)", (double)rapid, (double)steady);
            CHECK(rapid < 1.6f * steady + 1e-5f, "layer %d, %s: %.6f, steady %.6f", l, fx ? EN_FX_NAME[fx] : "(effects)", (double)rapid, (double)steady);
        }
}
/* a rest, a new piece (every value at once), a panic and back, a save's duck: what they do with an effect on, no more than they do without */
static float song_event(int kind, int kfx, int mfx)
{
    float d;
    uint32_t p;
    start(17u, EN_HANDS, EN_ST_LIQUID);
    layers(1, 1, 0, 0, 0);
    en_test_pad_off = 1;
    en_set(P_MELMODE, EN_MEL_ARP);
    en_set(P_ECHO, 0);
    en_set(P_REVERB, 0);
    fx_set(EN_L_KEYS, kfx, 70, 100);
    fx_set(EN_L_MEL, mfx, 70, 100);
    en_play(1);
    render(SECS(12));
    ncap = 0u;
    switch (kind) {
    case 0:                                      /* it rests, it grows again */
        en_play(0);
        capture(SECS(3));
        en_play(1);
        capture(SECS(1));
        break;
    case 1: {                                    /* a new piece: all its values, Off and neutral, in one block */
        int16_t v[P_NPARAMS];
        uint16_t pool;
        en_piece(3, v, &pool);
        v[P_ECHO] = 0;
        v[P_REVERB] = 0;
        v[P_BASS] = 0;
        v[P_DRUMS] = 0;
        v[P_HATS] = 0;
        v[P_MELMODE] = EN_MEL_ARP;
        v[P_TEMPO] = 170;
        for (p = 0u; p < P_NPARAMS; p++)
            en_set((int)p, v[p]);
        en_regrow();
        capture(SECS(3));
        break;
    }
    case 2:                                      /* a panic, and back */
        en_panic();
        render(SECS(1));
        en_restore();
        capture(SECS(8));
        break;
    default:                                     /* a save's duck */
        en_duck(1);
        capture(SECS(0.4f));
        en_duck(0);
        capture(SECS(0.6f));
        break;
    }
    d = d2_of(0u, ncap / 2u);
    CHECK(!en_test_nans, "NaN");
    return d;
}
static void t_click_events(void)
{
    static const char *const NAME[4] = {"a rest", "a new piece", "a panic and back", "a save's duck"};
    int k, fx;
    for (k = 0; k < 4; k++) {
        const float off = song_event(k, EN_FX_OFF, EN_FX_OFF);
        float worst = 0.0f;
        for (fx = 1; fx < EN_NFX; fx++) {
            const float d = song_event(k, fx, fx);
            worst = d > worst ? d : worst;
            CHECK(d < 1.6f * off + 1e-5f, "%s, %s: %.6f, Off %.6f", NAME[k], EN_FX_NAME[fx], (double)d, (double)off);
        }
        printf("  %s: with the effects at most %.6f, Off %.6f\n", NAME[k], (double)worst, (double)off);
    }
}

/* ---- Keys and Keys Arp: the player's notes go the same way */
static void t_player(void)
{
    static const int MODE[2] = {EN_MEL_KEYS, EN_MEL_KEYS_ARP};
    int m, fx;
    for (m = 0; m < 2; m++) {
        float off_l = 0.0f;
        uint32_t off_midi = 0u;
        for (fx = 0; fx < EN_NFX; fx++) {
            float swing, r;
            uint32_t b;
            start(61u, EN_HANDS, EN_ST_LIQUID);
            layers(0, 1, 0, 0, 0);
            en_test_pad_off = 1;
            en_set(P_MELMODE, MODE[m]);
            en_set(P_MELSND, 1);
            en_set(P_ECHO, 0);
            en_set(P_REVERB, 0);
            en_set(P_ARPLATCH, 0);
            fx_set(EN_L_MEL, fx, 0, 100);
            en_play(1);
            render(10u);
            ncap = 0u;
            for (b = 0u; b < SECS(12); b++) {    /* a key struck over and over: a note a second */
                en_keys(b % SECS(1) < SECS(0.7f) ? 0x41u : 0u);
                capture(1u);
            }
            r = rms_of(cap, ncap);
            if (!fx) {
                off_l = r;
                off_midi = mh;
            }
            CHECK(mh == off_midi, "%s: MIDI moved", EN_FX_NAME[fx]);
            if (fx) {
                static float ls[44100u * 20u], rs[44100u * 20u];
                uint32_t i, n = ncap / 2u;
                double dl = 0.0;
                for (i = 0u; i < n; i++) {
                    ls[i] = cap[2u * i];
                    rs[i] = cap[2u * i + 1u];
                    dl += (double)fabsf(ls[i] - rs[i]);
                }
                swing = (float)(dl / (double)n);
                CHECK(fabsf(20.0f * log10f(r / off_l)) < 3.0f, "%s: level %f, Off %f", EN_FX_NAME[fx], (double)r, (double)off_l);
                if (fx == EN_FX_AUTOPAN)
                    CHECK(swing > 0.002f, "the player's notes pan: %f", (double)swing);
                CHECK(en_test_fx_q(EN_L_MEL, EN_FXQ_KIND) == (float)fx && en_test_fx_q(EN_L_MEL, EN_FXQ_BUS) == 1.0f, "its bus");
            }
        }
    }
    /* and the sound is another: the effect on the player's note changes it, in each mode */
    for (m = 0; m < 2; m++)
        for (fx = 1; fx < EN_NFX; fx++) {
            uint32_t h[2];
            int on;
            for (on = 0; on < 2; on++) {
                uint32_t b;
                start(61u, EN_HANDS, EN_ST_LIQUID);
                layers(0, 1, 0, 0, 0);
                en_test_pad_off = 1;
                en_set(P_MELMODE, MODE[m]);
                en_set(P_MELSND, 1);
                en_set(P_ECHO, 0);
                en_set(P_REVERB, 0);
                fx_set(EN_L_MEL, on ? fx : EN_FX_OFF, 50, 100);
                en_play(1);
                render(10u);
                for (b = 0u; b < SECS(3); b++) {
                    en_keys(0x41u);
                    render(1u);
                }
                h[on] = ah;
            }
            CHECK(h[0] != h[1], "%s: the player's notes do not pass through it", EN_FX_NAME[fx]);
        }
}

/* ---- at the ends: every effect on both, every end; loud, no NaN, nothing runs away. And the filter's and the chorus's state, in silence and on next to nothing, stays out of the denormals */
static void t_extremes(void)
{
    int a, b;
    float base, most = 0.0f;
    start(71u, EN_HANDS, EN_ST_LIQUID);
    layers(1, 1, 1, 1, 1);
    en_set(P_KEYS, 100);
    en_set(P_MOTIFS, 100);
    en_set(P_ECHO, 100);
    en_set(P_REVERB, 100);
    en_set(P_SIZE, 100);
    en_set(P_MELMODE, EN_MEL_ARP);
    en_set(P_MRATE, 100);
    en_play(1);
    render(SECS(40));
    base = en_test_peak_in;
    for (a = 1; a < EN_NFX; a++)
        for (b = 1; b < EN_NFX; b++) {
            start(71u, EN_HANDS, EN_ST_LIQUID);
            layers(1, 1, 1, 1, 1);
            en_set(P_KEYS, 100);
            en_set(P_MOTIFS, 100);
            en_set(P_ECHO, 100);
            en_set(P_REVERB, 100);
            en_set(P_SIZE, 100);
            en_set(P_MELMODE, EN_MEL_ARP);
            en_set(P_MRATE, 100);
            fx_set(EN_L_KEYS, a, 100, 100);
            fx_set(EN_L_MEL, b, 100, 100);
            en_set(P_KECHO, 200);
            en_set(P_KREV, 200);
            en_set(P_MECHO, 200);
            en_set(P_MREV, 200);
            en_play(1);
            render(SECS(40));
            CHECK(!en_test_nans, "%s, %s: NaN", EN_FX_NAME[a], EN_FX_NAME[b]);
            most = fmaxf(most, en_test_peak_in);
            CHECK(en_test_peak_in < 2.2f * base, "%s, %s: peak %f, as ever %f", EN_FX_NAME[a], EN_FX_NAME[b], (double)en_test_peak_in, (double)base);
        }
    printf("  into the limiter, everything at its end: as ever %.2f; every effect and its sends at 200, at most %.2f\n", (double)base, (double)most);
    /* silence and next to nothing */
    {
        int l;
        for (l = 0; l < 2; l++) {
            uint32_t bl;
            static float il[BLOCK], ol[BLOCK], orr[BLOCK];
            bus_start(l, EN_FX_FILTER, 100, 100, 170);
            through(l, 500.0f, 0.5f, 100u);
            memset(il, 0, sizeof il);
            for (bl = 0u; bl < SECS(20); bl++) {
                const float s = en_test_fx_q(l, EN_FXQ_STATE);
                en_test_fx_run(l, il, il, il, il, ol, orr, 0, 0, BLOCK);
                CHECK(s == 0.0f || s > 1e-37f, "the filter's state among the denormals: %g", (double)s);
            }
            CHECK(en_test_fx_q(l, EN_FXQ_STATE) == 0.0f, "the filter's state, after silence: %g", (double)en_test_fx_q(l, EN_FXQ_STATE));
            for (bl = 0u; bl < 256u; bl++)
                il[bl] = 1e-30f;
            for (bl = 0u; bl < SECS(5); bl++) {
                uint32_t j;
                en_test_fx_run(l, il, il, il, il, ol, orr, 0, 0, BLOCK);
                for (j = 0u; j < BLOCK; j++)
                    CHECK(ol[j] == ol[j] && fabsf(ol[j]) < 1.0f, "next to nothing: %g", (double)ol[j]);
            }
        }
    }
    /* a filter swept as hard as can be asked: nothing rings or runs */
    {
        int l;
        for (l = 0; l < 2; l++) {
            uint32_t bl;
            float top = 0.0f;
            bus_start(l, EN_FX_FILTER, 100, 100, 170);
            nout = 0u;
            for (bl = 0u; bl < SECS(10); bl++) {
                en_set(fx_par(l) + 2, bl & 1u ? 100 : 0);
                en_render(blk, BLOCK, GAIN);
                through(l, 0.0f, 0.3f, 1u);
            }
            for (bl = 0u; bl < nout; bl++)
                top = fmaxf(top, fabsf(outl[bl]));
            CHECK(top == top && top < 0.6f, "a swept filter: %f on noise of 0.15", (double)top);
        }
    }
    /* the echoes' filters, with nothing going to them for a long time: none of them stays among the denormals, whatever is going on elsewhere */
    {
        uint32_t bl;
        start(5u, EN_HANDS, EN_ST_LIQUID);
        layers(1, 0, 0, 0, 0);
        en_play(1);
        render(SECS(10));
        en_set(P_ECHO, 0);
        render(SECS(90));
        for (bl = 0u; bl < 200u; bl++) {
            const float e = en_test_echo_state();
            render(1u);
            CHECK(e == 0.0f, "the echoes' filters are not at rest: %g", (double)e);
        }
    }
    /* nothing is left of it after a panic: the bus's state goes, and comes back by its fade */
    {
        start(5u, EN_HANDS, EN_ST_LIQUID);
        layers(1, 1, 0, 0, 0);
        fx_set(EN_L_KEYS, EN_FX_FILTER, 70, 80);
        fx_set(EN_L_MEL, EN_FX_CHORUS, 70, 80);
        en_play(1);
        render(SECS(5));
        en_panic();
        render(4u);
        CHECK(en_test_fx_q(EN_L_KEYS, EN_FXQ_KIND) == (float)EN_FX_OFF && en_test_fx_q(EN_L_KEYS, EN_FXQ_STATE) == 0.0f, "a panic: nothing left");
        en_restore();
        render(SECS(3));
        CHECK(en_test_fx_q(EN_L_KEYS, EN_FXQ_KIND) == (float)EN_FX_FILTER && en_test_fx_q(EN_L_KEYS, EN_FXQ_WET) == 1.0f, "it comes back");
        CHECK(en_test_fx_q(EN_L_MEL, EN_FXQ_KIND) == (float)EN_FX_CHORUS && en_test_fx_q(EN_L_MEL, EN_FXQ_WET) == 1.0f, "it comes back");
        en_set(P_KFX, EN_FX_OFF);
        en_set(P_MFX, EN_FX_OFF);
        render(SECS(0.3f));
        CHECK(en_test_fx_q(EN_L_KEYS, EN_FXQ_BUS) == 0.0f && en_test_fx_q(EN_L_MEL, EN_FXQ_BUS) == 0.0f, "Off: by no bus again");
    }
}

/* ---- Autopan on the melody changes direction with each new phrase, through the whole of the render, and in what comes out: en_fx_compose alone would not know what the engine told it. Off the melody's own phrases nothing turns: the keys', and Motion 0 */
static uint32_t ap_phrases;
static void ap_trace(uint32_t step, const en_phrase_t *p)
{
    (void)step, (void)p;
    ap_phrases++;
}
static void autopan_run(uint32_t seed, int layer, int motion, uint32_t bars, int *agree, int *differ, int *turns, int *sound_with, int *sound_against)
{
    en_fx_plan_t base;
    uint32_t b, was = 0xFFFFFFFFu, at_bar = 0u;
    int last = 0;
    double corr = 0.0, power = 0.0;
    start(seed, EN_HANDS, 0);
    layers(0, 1, 0, 0, 0);
    en_set(P_MRATE, 100);
    en_set(layer == EN_L_MEL ? P_MECHO : P_KECHO, 0);
    en_set(layer == EN_L_MEL ? P_MREV : P_KREV, 0);
    fx_set(layer, EN_FX_AUTOPAN, motion, 100);
    en_test_pad_off = 1;
    en_play(1);
    ap_phrases = 0u;
    en_trace_mel = ap_trace;
    *agree = *differ = *turns = *sound_with = *sound_against = 0;
    render(4u);
    for (b = 0u; b < bars * 200u && en_test_song() / 16u < bars; b++) {
        const uint32_t s = en_test_song(), bar = s ? (s - 1u) / 16u : 0u;
        uint32_t j;
        float phase, depth;
        if (bar != was) {                        /* a new bar's last blocks told what it came to: judge the bar now over */
            if (was != 0xFFFFFFFFu && was > 0u) {
                const int dir = (at_bar & 1u) && layer == EN_L_MEL && motion > 0 ? -base.dir : base.dir;
                const float d = en_test_fx_q(layer, EN_FXQ_DEPTH);
                const int side = d > 0.0f ? 1 : d < 0.0f ? -1 : 0;
                if (side == dir)
                    ++*agree;
                else
                    ++*differ;
                if (last && side != last)
                    ++*turns;
                last = side;
                if (power > 0.0) {               /* in the sound: which way the left and the right follow the wave, against which way it should go */
                    if ((corr > 0.0) == (dir > 0))
                        ++*sound_with;
                    else
                        ++*sound_against;
                }
            }
            was = bar;
            at_bar = ap_phrases;
            compose_at(EN_FX_AUTOPAN, layer, motion, 100, seed, s ? s - 1u : 0u, EN_SEC_LIGHT, 2, 1, 0u, &base);
            corr = power = 0.0;
        }
        phase = en_test_fx_q(layer, EN_FXQ_PHASE);
        depth = fabsf(en_test_fx_q(layer, EN_FXQ_DEPTH));
        en_render(blk, BLOCK, GAIN);
        mblock++;
        if (depth > 0.3f) {                      /* (not while it is going by way of the middle) */
            double lr = 0.0;
            for (j = 0u; j < BLOCK; j++) {
                const double l = (double)blk[2u * j] * (1.0 / 8300000.0), r = (double)blk[2u * j + 1u] * (1.0 / 8300000.0);
                lr += r * r - l * l;
                power += l * l + r * r;
            }
            corr += lr * sin(2.0 * 3.14159265358979 * (double)phase);
        }
    }
    en_trace_mel = 0;
}
static void t_autopan_phrases(void)
{
    static const uint32_t SEEDS[3] = {11u, 202u, 3003u};
    int s;
    for (s = 0; s < 3; s++) {
        int agree, differ, turns, with, against;
        /* Motion above 0: each phrase turns it. Motion 1: the phrase line's own choice of direction hardly comes in: the turns are the phrases' */
        autopan_run(SEEDS[s], EN_L_MEL, 1, 96u, &agree, &differ, &turns, &with, &against);
        CHECK(ap_phrases >= 6u, "seed %u: %u phrases: too few to say", SEEDS[s], ap_phrases);
        CHECK(differ == 0, "seed %u: the direction went against the phrases' count in %d bars of %d", SEEDS[s], differ, agree + differ);
        CHECK(turns >= 3, "seed %u: the direction turned %d times in %u phrases", SEEDS[s], turns, ap_phrases);
        CHECK(with + against > 20 && (with * 5 >= (with + against) * 4 || against * 5 >= (with + against) * 4), "seed %u: heard: %d bars one way, %d the other: the sound does not follow it", SEEDS[s], with, against);
        printf("  autopan by phrases, seed %u: %u phrases, %d turns; heard %d/%d\n", SEEDS[s], ap_phrases, turns, with, against);
        autopan_run(SEEDS[s], EN_L_MEL, 100, 96u, &agree, &differ, &turns, &with, &against);
        CHECK(differ == 0, "seed %u, Motion 100: the direction went against the phrases' count in %d bars of %d", SEEDS[s], differ, agree + differ);
        CHECK(turns >= 3, "seed %u, Motion 100: it turned %d times", SEEDS[s], turns);
        /* Motion 0: never */
        autopan_run(SEEDS[s], EN_L_MEL, 0, 64u, &agree, &differ, &turns, &with, &against);
        CHECK(ap_phrases >= 4u, "seed %u: %u phrases", SEEDS[s], ap_phrases);
        CHECK(differ == 0 && turns == 0, "seed %u, Motion 0: it moved: %d against, %d turns", SEEDS[s], differ, turns);
    }
    /* a phrase is no more than the melody's business: the keys' at the same Motion does not turn with it */
    {
        int agree, differ, turns, with, against;
        autopan_run(SEEDS[0], EN_L_KEYS, 1, 64u, &agree, &differ, &turns, &with, &against);
        CHECK(differ == 0, "the keys' follows the phrases: %d against", differ);
    }
}

int main(void)
{
    cap = malloc(CAP_MAX * sizeof *cap);
    outl = malloc(44100u * 100u * sizeof *outl);
    outr = malloc(44100u * 100u * sizeof *outr);
    if (!cap || !outl || !outr)
        return 2;
    t_table();
    t_compose_still();
    t_compose_lines();
    t_compose_rules();
    t_tremolo_rate();
    t_autopan_rate();
    t_filter_spectrum();
    t_chorus_detune();
    t_sends();
    t_off_pinned();
    t_midi_never();
    t_others_untouched();
    t_engine_still();
    t_engine_moves();
    t_grid_locked();
    t_autopan_phrases();
    t_filter_moves();
    t_filter_follows();
    t_levels();
    t_click_changes();
    t_click_rapid();
    t_click_events();
    t_player();
    t_extremes();
    if (fails) {
        printf("%d FAILED\n", fails);
        return 1;
    }
    printf("fx: all passed\n");
    return 0;
}
