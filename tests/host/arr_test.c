/* SPDX-License-Identifier: MPL-2.0 */
/* The arranger: what it decides (pure), and that the engine does what it decides, for hours. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "amb.h"
#include "brk.h"

static int fails;
#define CHECK(c, ...)                                                                                                  \
    do {                                                                                                               \
        if (!(c)) {                                                                                                    \
            if (fails < 40) {                                                                                          \
                printf("FAIL %s:%d: ", __FILE__, __LINE__);                                                            \
                printf(__VA_ARGS__);                                                                                   \
                printf("\n");                                                                                          \
            }                                                                                                          \
            fails++;                                                                                                   \
        }                                                                                                              \
    } while (0)

static uint32_t midi_hash = 2166136261u, midi_notes;
void en_midi_out(uint32_t st, uint32_t d1, uint32_t d2)
{
    if ((st & 0xF0u) == 0x90u) {
        midi_hash = (midi_hash ^ (st << 16 | d1 << 8 | d2)) * 16777619u;
        midi_notes++;
    }
}

#define NSEEDS 300u
#define NPASS 4u
#define DEF_ENERGY 70
#define SECS_PER_BAR (4.0 * 60.0 / 170.0)

/* ---- the pure part ---- */

/* one seed's passes through a style: reachability, order, lengths, whole passes */
static void walk_checks(void)
{
    static const int ENERGY[] = {0, 30, DEF_ENERGY, 100};
    int st, e;
    for (st = 0; st < EN_NSTYLES; st++)
        for (e = 0; e < 4; e++) {
            const int energy = ENERGY[e];
            uint32_t seen = 0u, sd, pass_lo = 0xFFFFFFFFu, pass_hi = 0u;
            for (sd = 0u; sd < NSEEDS; sd++) {
                en_arr_t a, b;
                uint32_t bars = 0u, passes = 0u;
                en_arr_first(&a, sd, st, energy);
                CHECK(a.section == EN_SEC_INTRO && a.pass == 0u, "%s: starts at section %d", EN_STYLE_NAME[st], a.section);
                while (passes < NPASS) {
                    int lo, hi;
                    en_arr_range(st, a.section, energy, &lo, &hi);
                    seen |= 1u << a.section;
                    CHECK(a.bars % EN_ARR_PHRASE == 0u && a.bars > 0u, "%s: %u bars", EN_STYLE_NAME[st], a.bars);
                    CHECK((int)a.bars >= lo && (int)a.bars <= hi, "%s seed %u section %d: %u bars, not %d..%d", EN_STYLE_NAME[st], sd,
                          a.section, a.bars, lo, hi);
                    bars += a.bars;
                    b = a;
                    en_arr_next(&b, sd, st, energy);
                    if (a.section == EN_SEC_OUT) {
                        CHECK(b.section == EN_SEC_INTRO && b.pass == a.pass + 1u, "%s: after the way out: section %d pass %u",
                              EN_STYLE_NAME[st], b.section, b.pass);
                        if (bars < pass_lo)
                            pass_lo = bars;
                        if (bars > pass_hi)
                            pass_hi = bars;
                        bars = 0u;
                        passes++;
                    } else if (a.section == EN_SEC_DOWN) {
                        CHECK(b.section == EN_SEC_FULL || b.section == EN_SEC_OUT, "%s: after the breakdown: %d", EN_STYLE_NAME[st], b.section);
                        CHECK(b.pass == a.pass, "the pass changed inside");
                    } else {
                        CHECK(b.section == a.section + 1, "%s: section %d after %d", EN_STYLE_NAME[st], b.section, a.section);
                        CHECK(b.pass == a.pass, "the pass changed inside");
                    }
                    a = b;
                }
            }
            CHECK(seen == (1u << EN_NSECTIONS) - 1u, "%s energy %d: reached only %02x", EN_STYLE_NAME[st], energy, seen);
            if (energy == DEF_ENERGY) {
                printf("  %-8s a pass: %.0f..%.0f s at 170, %.0f..%.0f s at 150, %.0f..%.0f s at 180\n", EN_STYLE_NAME[st], pass_lo * SECS_PER_BAR,
                       pass_hi * SECS_PER_BAR, pass_lo * SECS_PER_BAR * 170.0 / 150.0, pass_hi * SECS_PER_BAR * 170.0 / 150.0,
                       pass_lo * SECS_PER_BAR * 170.0 / 180.0, pass_hi * SECS_PER_BAR * 170.0 / 180.0);
                CHECK(pass_lo * SECS_PER_BAR >= 180.0 && pass_hi * SECS_PER_BAR <= 360.0, "%s: a pass of %.0f..%.0f s", EN_STYLE_NAME[st],
                      pass_lo * SECS_PER_BAR, pass_hi * SECS_PER_BAR);
            }
        }
}

/* how long each drum level sounds, a share of the bars */
static void shares(int st, int energy, double *sh)
{
    uint32_t sd, n[5] = {0u, 0u, 0u, 0u, 0u}, all = 0u;
    int l;
    for (sd = 0u; sd < NSEEDS; sd++) {
        en_arr_t a;
        en_arr_first(&a, sd, st, energy);
        while (a.pass < NPASS) {
            uint32_t b;
            for (b = 0u; b < a.bars; b++) {
                en_part_t p;
                en_arr_part(&a, b, st, energy, &p);
                CHECK(p.level <= 4u && p.section == a.section, "a part out of range");
                n[p.level]++;
                all++;
            }
            en_arr_next(&a, sd, st, energy);
        }
    }
    for (l = 0; l < 5; l++)
        sh[l] = (double)n[l] / (double)all;
}

static void energy_checks(void)
{
    int st, e;
    for (st = 0; st < EN_NSTYLES; st++) {
        double sh[5];
        /* energy 0: hats at most, anywhere; a low energy never the full rhythm */
        shares(st, 0, sh);
        CHECK(sh[2] == 0.0 && sh[3] == 0.0 && sh[4] == 0.0, "%s energy 0: kick and snare levels %.3f %.3f %.3f", EN_STYLE_NAME[st], sh[2], sh[3], sh[4]);
        shares(st, 30, sh);
        CHECK(sh[2] == 0.0 && sh[3] == 0.0, "%s energy 30: level 2 %.3f, level 3 %.3f", EN_STYLE_NAME[st], sh[2], sh[3]);
        shares(st, 50, sh);
        CHECK(sh[3] == 0.0, "%s energy 50: level 3 %.3f", EN_STYLE_NAME[st], sh[3]);
        shares(st, DEF_ENERGY, sh);
        printf("  %-8s bars by level 0 %4.1f%%, hats %4.1f%%, light %4.1f%%, full %4.1f%%, half %4.1f%%\n", EN_STYLE_NAME[st], 100 * sh[0], 100 * sh[1],
               100 * sh[2], 100 * sh[3], 100 * sh[4]);
        switch (st) {
        case EN_ST_DRIFT:
            CHECK(sh[3] == 0.0, "Drift: the full rhythm %.3f", sh[3]);
            CHECK(sh[0] + sh[1] > 0.8, "Drift: drums too much: %.3f without", sh[0] + sh[1]);
            break;
        case EN_ST_LOADING:
            CHECK(sh[3] > 0.0 && sh[3] < 0.08, "Loading: the full rhythm %.3f", sh[3]);
            break;
        default:
            CHECK(sh[3] > 0.2, "%s: the full rhythm only %.3f", EN_STYLE_NAME[st], sh[3]);
            break;
        }
        /* a high energy keeps the breakdown shorter, never longer */
        for (e = 70; e < 100; e += 10) {
            int lo0, hi0, lo1, hi1;
            en_arr_range(st, EN_SEC_DOWN, e, &lo0, &hi0);
            en_arr_range(st, EN_SEC_DOWN, e + 10, &lo1, &hi1);
            CHECK(lo1 <= lo0 && hi1 <= hi0 && lo1 >= (int)EN_ARR_PHRASE, "%s: breakdown at energy %d: %d..%d, then %d..%d", EN_STYLE_NAME[st], e, lo0, hi0, lo1, hi1);
        }
        {
            int lo0, hi0, lo1, hi1;
            en_arr_range(st, EN_SEC_DOWN, DEF_ENERGY, &lo0, &hi0);
            en_arr_range(st, EN_SEC_DOWN, 100, &lo1, &hi1);
            CHECK(hi1 < hi0, "%s: energy 100 does not shorten the breakdown: %d, %d", EN_STYLE_NAME[st], hi0, hi1);
        }
    }
    {
        double sh[5];
        shares(EN_ST_LIQUID, 100, sh);
        CHECK(sh[3] > 0.2, "Liquid at energy 100: level 3 %.3f", sh[3]);
    }
}

/* one seed, one result; other seeds, others */
static void seed_checks(void)
{
    int st;
    for (st = 0; st < EN_NSTYLES; st++) {
        uint32_t sd;
        int differ = 0;
        uint16_t first[32];
        for (sd = 0u; sd < NSEEDS; sd++) {
            en_arr_t a, b;
            uint16_t seq[32];
            int i;
            en_arr_first(&a, sd, st, DEF_ENERGY);
            en_arr_first(&b, sd, st, DEF_ENERGY);
            for (i = 0; i < 32; i++) {
                CHECK(!memcmp(&a, &b, sizeof a), "%s seed %u: not repeatable", EN_STYLE_NAME[st], sd);
                seq[i] = (uint16_t)(a.bars * 8u + a.section);
                en_arr_next(&a, sd, st, DEF_ENERGY);
                en_arr_next(&b, sd, st, DEF_ENERGY);
            }
            if (!sd)
                memcpy(first, seq, sizeof seq);
            else
                differ += memcmp(first, seq, sizeof seq) != 0;
        }
        CHECK(differ > (int)NSEEDS * 9 / 10, "%s: only %d seeds differ", EN_STYLE_NAME[st], differ);
    }
}

/* a break wanted: on a bar, inside its phrase, its length by the style, none when the knob says none */
static void break_checks(void)
{
    int st;
    double rate[EN_NSTYLES];
    for (st = 0; st < EN_NSTYLES; st++) {
        uint32_t sd, phrases = 0u, with = 0u;
        for (sd = 0u; sd < NSEEDS / 3u; sd++) {
            uint32_t bar;
            for (bar = 0u; bar < 800u; bar++) {
                uint32_t in = 99u;
                const int len = en_arr_break(bar, 3, st, 50, sd, &in);
                CHECK(en_arr_break(bar, 3, st, 0, sd, &in) == 0, "%s: a break with the knob at 0", EN_STYLE_NAME[st]);
                CHECK(en_arr_break(bar, 1, st, 100, sd, &in) == 0, "%s: a break over the hats", EN_STYLE_NAME[st]);
                CHECK(en_arr_break(bar, 0, st, 100, sd, &in) == 0, "%s: a break over nothing", EN_STYLE_NAME[st]);
                CHECK(en_arr_break(bar, 4, st, 100, sd, &in) == 0, "%s: a break over the half time", EN_STYLE_NAME[st]);
                in = 99u;
                {
                    const int l2 = en_arr_break(bar, 3, st, 50, sd, &in);
                    CHECK(l2 == len, "not repeatable");
                    if (l2) {
                        CHECK(l2 == 1 || l2 == 2 || l2 == 4, "%s: a break of %d bars", EN_STYLE_NAME[st], l2);
                        CHECK(in < (uint32_t)l2, "%s: %u bars into a break of %d", EN_STYLE_NAME[st], in, l2);
                        CHECK(bar % EN_ARR_PHRASE + (uint32_t)l2 - in == EN_ARR_PHRASE, "%s: a break at bar %u ends off the phrase", EN_STYLE_NAME[st], bar);
                    }
                }
                if (bar % EN_ARR_PHRASE == EN_ARR_PHRASE - 1u) {
                    phrases++;
                    with += len != 0;
                }
            }
        }
        rate[st] = (double)with / (double)phrases;
        {
            const brk_rhythm_t *r = en_style_rhythm(st);
            printf("  %-8s density %3u, syncopation %3u, breaks %3u/256 of a phrase (seen at level 3: %.2f), length %3u, ghosts %3u\n", EN_STYLE_NAME[st], r->density,
                   r->synco, r->break_rate, rate[st], r->break_len, r->ghost);
        }
    }
    CHECK(rate[EN_ST_JUNGLE] > rate[EN_ST_LIQUID], "Jungle's breaks %.2f, Liquid's %.2f", rate[EN_ST_JUNGLE], rate[EN_ST_LIQUID]);
    CHECK(rate[EN_ST_DRIFT] < rate[EN_ST_LIQUID], "Drift's breaks %.2f, Liquid's %.2f", rate[EN_ST_DRIFT], rate[EN_ST_LIQUID]);
    /* the knob: more, more breaks */
    {
        uint32_t sd, n[3] = {0u, 0u, 0u};
        for (sd = 0u; sd < 100u; sd++) {
            uint32_t bar, in;
            for (bar = 7u; bar < 800u; bar += 8u) {
                n[0] += en_arr_break(bar, 3, EN_ST_LIQUID, 20, sd, &in) != 0;
                n[1] += en_arr_break(bar, 3, EN_ST_LIQUID, 50, sd, &in) != 0;
                n[2] += en_arr_break(bar, 3, EN_ST_LIQUID, 100, sd, &in) != 0;
            }
        }
        CHECK(n[0] < n[1] && n[1] < n[2], "the Breaks knob: %u %u %u", n[0], n[1], n[2]);
    }
}

/* ---- the harmony, as the arranger leads it ---- */
/* the chords of whole passes, with the character the arranger says at each chord's first bar */
#define WALK_CHORDS 240u
static void harmony_checks(void)
{
    static const int ENERGY[] = {0, 30, DEF_ENERGY, 100};
    const uint32_t POOLS[2] = {EN_MODE_POOL[MODE_LYDIAN], EN_MODE_POOL[MODE_AEOLIAN]};
    int st, e, p;
    for (st = 0; st < EN_NSTYLES; st++)
        for (e = 0; e < 4; e++)
            for (p = 0; p < 2; p++) {
                uint32_t sd, elsewhere = 0u, seconds = 0u;
                int worst = 0;
                for (sd = 0u; sd < (p ? 10u : 60u); sd++) {
                    en_arr_t a;
                    en_gen_t g;
                    en_gchord_t c;
                    uint32_t bar = 0u, in = 0u, i, lastpass = 0u, first_root = 0u, passes = 1u, last_b = 0u;
                    int gap = 0, v, counted = 0, any_b = 0;
                    en_gen_start(&g);
                    en_arr_first(&a, sd, st, ENERGY[e]);
                    for (i = 0u; i < WALK_CHORDS; i++) {
                        en_part_t pt;
                        const int prev_root = g.prev_root, prev_bass = g.prev_bass;
                        while (in >= a.bars) {
                            in -= a.bars;
                            en_arr_next(&a, sd, st, ENERGY[e]);
                        }
                        en_arr_part(&a, in, st, ENERGY[e], &pt);
                        if (a.pass != lastpass) {
                            lastpass = a.pass;
                            passes++;
                        }
                        en_gen_next(&g, POOLS[p], 2, sd, 50, pt.character, EN_MASK_ALL, &c);
                        if (!i)
                            first_root = c.ch.root;
                        CHECK(c.ch.bass != 0, "%s: the tonic in the bass", EN_STYLE_NAME[st]);
                        if (i) {
                            CHECK(c.ch.root != prev_root, "%s seed %u: the same root twice", EN_STYLE_NAME[st], sd);
                            CHECK(c.ch.bass != prev_bass, "%s seed %u: the same bass twice", EN_STYLE_NAME[st], sd);
                        }
                        for (v = 0; v < 4; v++) {
                            CHECK(c.keys_v[v] >= 54 && c.keys_v[v] <= 77, "%s seed %u: keys %d", EN_STYLE_NAME[st], sd, c.keys_v[v]);
                            CHECK(c.pad_v[v] >= 51 && c.pad_v[v] <= 74, "%s seed %u: pad %d", EN_STYLE_NAME[st], sd, c.pad_v[v]);
                        }
                        if (c.ch.borrowed) {
                            CHECK(!any_b || i - last_b >= 40u, "%s seed %u: borrowed chords %u apart", EN_STYLE_NAME[st], sd, i - last_b);
                            any_b = 1;
                            last_b = i;
                        }
                        /* the key stays clear, in every style: the tonic area comes round. Lydian: F#, B and the tonic; Aeolian: C, F and the tonic */
                        gap = c.ch.root == (p ? 3 : 4) || c.ch.root == (p ? 8 : 9) || !c.ch.root ? 0 : gap + 1;
                        worst = gap > worst ? gap : worst;
                        if (passes == 2u && !counted) {       /* the second pass starts from where the music has got to, not from the top */
                            elsewhere += c.ch.root != first_root;
                            counted = 1;
                        }
                        seconds += pt.character == 3u;
                        bar += c.bars;
                        in += c.bars;
                    }
                    (void)bar;
                }
                /* 24 chords of 2 to 4 bars: about two minutes at most, under the shortest pass (3:12): the key is clear at least once each pass */
                CHECK(worst <= 24, "%s energy %d: %d chords away from the tonic area", EN_STYLE_NAME[st], ENERGY[e], worst);
                if (!p) {
                    printf("  %-8s energy %3d: of 60 seeds, %u start their second pass on another root; at most %d chords from the tonic area\n", EN_STYLE_NAME[st], ENERGY[e], elsewhere, worst);
                    CHECK(elsewhere > 60u / 2u, "%s energy %d: the second pass starts where the first did: %u", EN_STYLE_NAME[st], ENERGY[e], elsewhere);
                    /* the peak: at the energy that reaches it, and never below */
                    if (ENERGY[e] < 60)
                        CHECK(seconds == 0u, "%s energy %d: the peak %u times", EN_STYLE_NAME[st], ENERGY[e], seconds);
                    else
                        CHECK(seconds > 0u, "%s energy %d: never the peak", EN_STYLE_NAME[st], ENERGY[e]);
                }
            }
}

/* ---- the drums the styles ask for: as bars, tests/host/rhy_test.c. Here: that a wish for a break is one ---- */
static void rhythm_checks(void)
{
    int st;
    for (st = 0; st < EN_NSTYLES; st++) {
        const brk_rhythm_t *r = en_style_rhythm(st);
        uint32_t sd, bar;
        for (sd = 0u; sd < 40u; sd++) {
            brk_gen_t g;
            brk_bar_t o;
            brk_gen_start(&g);
            for (bar = 0u; bar < 64u; bar++) {
                int lv = (int)(bar / 8u % 5u);
                const int want = (lv == 2 || lv == 3) && bar % EN_ARR_PHRASE >= EN_ARR_PHRASE - 2u ? 2 : 0;
                brk_gen_bar(&g, r, lv, DEF_ENERGY, 50, sd, bar, want, bar & 1u, &o);
                CHECK(o.is_break == (want != 0), "%s: a break wanted %d, played %u", EN_STYLE_NAME[st], want, o.is_break);
            }
        }
    }
}

/* ---- the engine does as it says ---- */
#define MAX_BARS 40000u
static struct {
    uint32_t step;
    uint8_t section, level, character, brk;
} tr[MAX_BARS];
static uint32_t ntr;
static void trace(uint32_t step, int section, int level, int character, int brk)
{
    if (ntr < MAX_BARS) {
        tr[ntr].step = step;
        tr[ntr].section = (uint8_t)section;
        tr[ntr].level = (uint8_t)level;
        tr[ntr].character = (uint8_t)character;
        tr[ntr].brk = (uint8_t)brk;
        ntr++;
    }
}

static int32_t buf[2 * 256];
static uint32_t bad_out;
static void render(uint32_t frames, uint32_t chunk)
{
    while (frames) {
        const uint32_t k = frames < chunk ? frames : chunk;
        uint32_t j;
        en_render(buf, k, 1024);
        for (j = 0u; j < 2u * k; j++)
            bad_out += buf[j] <= -(1 << 24) || buf[j] >= (1 << 24);
        bad_out += en_pad_level != en_pad_level || en_bass_level != en_bass_level || en_break_level != en_break_level;
        frames -= k;
    }
}

/* sixteenths, with this much sound after each */
static void drive(uint32_t steps, uint32_t frames_each)
{
    while (steps--) {
        en_test_ticks(1u);
        render(frames_each, 256u);
    }
}

static void engine_start(int style, int energy, int breaks, uint32_t seed)
{
    en_play(0);                                  /* from a standstill: the clock's pulse starts at its top */
    en_render(buf, 1, 1024);
    en_init();
    en_trace = trace;
    en_set(P_STYLE, style);
    en_set(P_ENERGY, energy);
    en_set(P_BREAKS, breaks);
    en_seed(seed);
    en_play(1);
    en_render(buf, 1, 1024);                     /* takes them in, and plays its first step */
    ntr = 0u;
    midi_hash = 2166136261u;
    midi_notes = 0u;
    brk_kicks = brk_snares = 0u;
}

/* a run's bars against the pure arranger's */
static void follow_checks(void)
{
    int st;
    for (st = 0; st < EN_NSTYLES; st++) {
        uint32_t sd;
        for (sd = 1u; sd <= 3u; sd++) {
            en_arr_t a;
            uint32_t i, in = 0u, last_step = 0u, chg = 0u;
            uint8_t sect = 0u, lev = 0u, was_brk = 0u;
            engine_start(st, DEF_ENERGY, 50, sd * 1000u);
            {
                uint32_t k;
                for (k = 0u; k < 5440u; k++) {           /* a song of eight minutes */
                    drive(1u, 32u);
                    if (en_section != sect || en_drum_level != lev) {   /* it moved on a bar, and only there */
                        CHECK((en_bars - 1u) % 16u == 0u, "%s: moved at step %u", EN_STYLE_NAME[st], en_bars - 1u);
                        sect = en_section;
                        lev = en_drum_level;
                        chg++;
                    }
                }
            }
            CHECK(ntr > 100u, "%s: %u bars traced", EN_STYLE_NAME[st], ntr);
            CHECK(chg > 2u, "%s: nothing changed", EN_STYLE_NAME[st]);
            en_arr_first(&a, sd * 1000u, st, DEF_ENERGY);
            for (i = 0u; i < ntr; i++) {
                const uint32_t bar = tr[i].step / 16u;
                en_part_t p;
                CHECK(tr[i].step % 16u == 0u, "a bar at step %u", tr[i].step);
                CHECK(i == 0u || tr[i].step == last_step + 16u, "%s: a bar missed at %u", EN_STYLE_NAME[st], tr[i].step);
                last_step = tr[i].step;
                while (in + a.bars <= bar) {
                    in += a.bars;
                    en_arr_next(&a, sd * 1000u, st, DEF_ENERGY);
                }
                en_arr_part(&a, bar - in, st, DEF_ENERGY, &p);
                CHECK(tr[i].section == p.section && tr[i].level == p.level && tr[i].character == p.character, "%s seed %u bar %u: plays %d %d %d, the arranger says %d %d %d",
                      EN_STYLE_NAME[st], sd, bar, tr[i].section, tr[i].level, tr[i].character, p.section, p.level, p.character);
                /* breaks: on bars, ending with their phrase, on the levels that have them */
                if (tr[i].brk) {
                    CHECK(p.level == 2u || p.level == 3u, "%s: a break at level %u", EN_STYLE_NAME[st], p.level);
                } else if (was_brk) {
                    CHECK(bar % EN_ARR_PHRASE == 0u, "%s: a break ended at bar %u", EN_STYLE_NAME[st], bar);
                }
                was_brk = tr[i].brk;
            }
        }
    }
}

/* the same seed, the same notes; another seed, others; the arrangement goes by bars, whatever the tempo */
static void repeat_checks(void)
{
    int st;
    for (st = 0; st < EN_NSTYLES; st++) {
        uint32_t h1, n1;
        static uint32_t first[MAX_BARS];
        uint32_t i, n0;
        engine_start(st, DEF_ENERGY, 50, 4242u);
        drive(3600u, 64u);
        h1 = midi_hash;
        n1 = midi_notes;
        n0 = ntr;
        for (i = 0u; i < n0; i++)
            first[i] = (uint32_t)tr[i].section << 16 | (uint32_t)tr[i].level << 8 | tr[i].brk;
        engine_start(st, DEF_ENERGY, 50, 4242u);
        drive(3600u, 64u);
        CHECK(midi_hash == h1 && midi_notes == n1, "%s: the same seed, %u then %u notes", EN_STYLE_NAME[st], n1, midi_notes);
        engine_start(st, DEF_ENERGY, 50, 4243u);
        drive(3600u, 64u);
        CHECK(midi_hash != h1, "%s: two seeds, the same notes", EN_STYLE_NAME[st]);
        engine_start(st, DEF_ENERGY, 50, 4242u);
        en_set(P_TEMPO, 150);
        drive(3600u, 64u);
        for (i = 0u; i < ntr && i < n0; i++)
            CHECK(first[i] == ((uint32_t)tr[i].section << 16 | (uint32_t)tr[i].level << 8 | tr[i].brk), "%s at 150: bar %u differs from 170", EN_STYLE_NAME[st], i);
        CHECK(!bad_out, "%s at 150: %u bad samples", EN_STYLE_NAME[st], bad_out);
    }
}

/* as it really runs, at the ends of the tempo's range: the grid's pace, and sound all along */
static void tempo_checks(void)
{
    static const int BPM[2] = {150, 180};
    int i;
    for (i = 0; i < 2; i++) {
        engine_start(EN_ST_LIQUID, DEF_ENERGY, 50, 31u);
        en_set(P_TEMPO, BPM[i]);
        render(44100u * 60u, 256u);
        {
            const uint32_t want = (uint32_t)BPM[i] * 4u;
            CHECK(en_bars + 2u >= want && en_bars <= want + 2u, "%d BPM: %u sixteenths in a minute", BPM[i], en_bars);
        }
        CHECK(!bad_out, "%d BPM: %u bad samples", BPM[i], bad_out);
    }
}

static uint32_t hand_n[2];
static double hand_top[2];
static void hand_seen(uint32_t step, const en_gchord_t *g, int character)
{
    (void)step;
    hand_n[character == 3]++;
    hand_top[character == 3] += g->keys_v[3];
}

static void energy_engine_checks(void)
{
    int st;
    for (st = 0; st < EN_NSTYLES; st++) {
        uint32_t sd;
        for (sd = 0u; sd < 6u; sd++) {
            uint32_t i, h0;
            engine_start(st, 0, 100, 900u + sd);
            h0 = en_hits;
            drive(7000u, 64u);
            CHECK(brk_kicks == 0u && brk_snares == 0u && en_hits == h0, "%s seed %u, energy 0: %u kicks, %u snares", EN_STYLE_NAME[st], sd, brk_kicks, brk_snares);
            for (i = 0u; i < ntr; i++)
                CHECK(tr[i].level <= 1u && !tr[i].brk, "%s energy 0: level %d, break %d", EN_STYLE_NAME[st], tr[i].level, tr[i].brk);
        }
    }
    /* Liquid at 100 reaches the full rhythm, within a pass, in every seed; with the Breaks knob at 0 no breaks */
    {
        uint32_t sd;
        for (sd = 0u; sd < 12u; sd++) {
            uint32_t i, full = 0u, brks = 0u;
            engine_start(EN_ST_LIQUID, 100, 0, 700u + sd);
            drive(5000u, 32u);
            for (i = 0u; i < ntr; i++) {
                full += tr[i].level == 3u;
                brks += tr[i].brk != 0u;
            }
            CHECK(full > 0u, "Liquid at energy 100, seed %u: no full rhythm in %u bars", sd, ntr);
            CHECK(brks == 0u, "Breaks at 0: %u bars of break", brks);
            CHECK(brk_kicks > 0u, "no kicks");
        }
    }
    /* the peak: over a good many songs the hand sits higher than elsewhere, with no fixed second cycle */
    {
        uint32_t sd;
        en_trace_chord = hand_seen;
        hand_n[0] = hand_n[1] = 0u;
        hand_top[0] = hand_top[1] = 0.0;
        for (sd = 0u; sd < 12u; sd++) {
            engine_start(EN_ST_LIQUID, DEF_ENERGY, 50, 300u + sd);
            drive(7000u, 16u);
        }
        en_trace_chord = 0;
        CHECK(hand_n[0] > 30u && hand_n[1] > 30u, "the peak seen %u times, the rest %u", hand_n[1], hand_n[0]);
        printf("  the top of the hand: %.1f elsewhere, %.1f at the peak (%u and %u chords)\n", hand_top[0] / hand_n[0], hand_top[1] / hand_n[1], hand_n[0], hand_n[1]);
        CHECK(hand_top[1] / hand_n[1] > hand_top[0] / hand_n[0] + 1.0, "the top of the hand at the peak %.1f, elsewhere %.1f", hand_top[1] / hand_n[1], hand_top[0] / hand_n[0]);
    }
}

/* hours: every style for two hours of its own steps; all the counters still count; a song's counter goes round */
static void hours_checks(void)
{
    const uint32_t steps = 2u * 3600u * 170u * 4u / 60u;
    int st;
    bad_out = 0u;
    for (st = 0; st < EN_NSTYLES; st++) {
        uint32_t moves = 0u, sects = 0u, sect_was = 99u, was;
        engine_start(st, DEF_ENERGY, 50, 2024u + (uint32_t)st);
        was = en_chord[0] | (uint32_t)en_chord[1] << 8 | (uint32_t)en_chord[2] << 16 | (uint32_t)en_chord[3] << 24;
        while (en_bars < steps) {
            uint32_t now;
            drive(1u, 16u);
            now = en_chord[0] | (uint32_t)en_chord[1] << 8 | (uint32_t)en_chord[2] << 16 | (uint32_t)en_chord[3] << 24;
            if (now != was) {
                was = now;
                moves++;
            }
            if (en_section != sect_was) {
                sect_was = en_section;
                sects++;
            }
        }
        printf("  %-8s 2 h: %u steps, %u sections, %u chord moves, %u kicks, peak %d voices\n", EN_STYLE_NAME[st], en_bars, sects, moves, brk_kicks, en_ep_peak);
        CHECK(sects > 100u, "%s: only %u sections in two hours", EN_STYLE_NAME[st], sects);
        CHECK(moves > 1000u, "%s: only %u chord moves", EN_STYLE_NAME[st], moves);
        CHECK(en_pad_level > 0.1f, "%s: the pad has gone (%f)", EN_STYLE_NAME[st], (double)en_pad_level);
        CHECK(!bad_out, "%s: hours: %u bad samples", EN_STYLE_NAME[st], bad_out);
    }
    /* the counter itself, past its end: twelve years of steps come round to the top of the song */
    {
        uint32_t top = 0u, i, from, left = 8192u;
        engine_start(EN_ST_ROLLER, DEF_ENERGY, 50, 5u);
        drive(160u, 16u);
        en_test_jump(0xFFFFF000u);
        ntr = 0u;
        while (left-- && !top) {
            drive(1u, 16u);
            for (i = ntr ? ntr - 1u : 0u; i < ntr; i++)
                top |= tr[i].step == 0u;
        }
        from = ntr;
        drive(320u, 16u);
        CHECK(top, "the song's counter did not come round");
        CHECK(ntr > from, "no bars after it came round");
        CHECK(!bad_out, "round the counter: %u bad samples", bad_out);
    }
    /* someone at the knobs for an hour: never a bad sample, never a section off the bar */
    {
        uint32_t h = 12345u, k;
        engine_start(EN_ST_LIQUID, DEF_ENERGY, 50, 77u);
        for (k = 0u; k < 2400u; k++) {
            h = h * 1664525u + 1013904223u;
            en_set(P_STYLE, (int)((h >> 16) % EN_NSTYLES));
            en_set(P_ENERGY, (int)((h >> 8) % 101u));
            en_set(P_BREAKS, (int)((h >> 20) % 101u));
            en_set(P_TEMPO, 150 + (int)((h >> 12) % 31u));
            drive(16u, 32u);
        }
        CHECK(!bad_out, "at the knobs: %u bad samples", bad_out);
    }
    en_trace = 0;
}

int main(void)
{
    walk_checks();
    energy_checks();
    seed_checks();
    break_checks();
    harmony_checks();
    rhythm_checks();
    en_init();
    follow_checks();
    repeat_checks();
    tempo_checks();
    energy_engine_checks();
    hours_checks();
    printf(fails ? "arr: %d FAILED\n" : "arr: ok\n", fails);
    return fails != 0;
}
