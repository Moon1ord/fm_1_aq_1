/* SPDX-License-Identifier: MPL-2.0 */
/* The engine under the chords: that it plays what en_gen_next composes, and that the piano, the pad, the bass, the motifs and
 * the screen follow a chord of any length. The pure generator is gen_test.c's, the arranger arr_test.c's. And the drums, pure
 * functions of a seed too. */
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "amb.h"
#include "brk.h"

static int fails;
#define CHECK(c, ...)                                                                                                  \
    do {                                                                                                               \
        if (!(c)) {                                                                                                    \
            if (fails < 60) {                                                                                          \
                printf("FAIL %s:%d: ", __FILE__, __LINE__);                                                            \
                printf(__VA_ARGS__);                                                                                   \
                printf("\n");                                                                                          \
            }                                                                                                          \
            fails++;                                                                                                   \
        }                                                                                                              \
    } while (0)

static int popcount(uint32_t m)
{
    int n = 0;
    for (; m; m &= m - 1u)
        n++;
    return n;
}

/* what the engine really plays, from its MIDI: step, kind, note */
#define NEV 40000
typedef struct {
    uint32_t step;
    uint8_t kind, note;                          /* kind: 0 chord's piano note, 1 lone piano note, 2 motif, 3 bass */
} ev_t;
static ev_t ev[NEV];
static int nev;
static uint32_t ev_singles;
void en_midi_out(uint32_t st, uint32_t d1, uint32_t d2)
{
    int kind = -1;
    (void)d2;
    if (st == 0x90u) {
        kind = en_ep_singles != ev_singles ? 1 : 0;
        ev_singles = en_ep_singles;
    } else if (st == 0x91u) {
        kind = 2;
    } else if (st == 0x92u) {
        kind = 3;
    }
    if (kind >= 0 && nev < NEV) {
        ev[nev].step = en_bars;
        ev[nev].kind = (uint8_t)kind;
        ev[nev].note = (uint8_t)d1;
        nev++;
    }
}

/* and the chords, as they start */
#define NCH 8000
typedef struct {
    uint32_t step;
    en_gchord_t g;
    uint8_t character, level;
} ch_t;
static ch_t chs[NCH];
static int nch;
static void on_chord(uint32_t step, const en_gchord_t *g, int character)
{
    if (nch < NCH) {
        chs[nch].step = step;
        chs[nch].g = *g;
        chs[nch].character = (uint8_t)character;
        nch++;
    }
}
static uint8_t bar_level[NCH * 4];
static void on_bar(uint32_t step, int section, int level, int character, int brk)
{
    (void)section, (void)character, (void)brk;
    if (step / 16u < NCH * 4u)
        bar_level[step / 16u] = (uint8_t)level;
}

static uint32_t bad_out;
static void render_until(uint32_t step)
{
    static int32_t buf[512];
    int j;
    while (en_bars < step) {
        en_render(buf, 256, 1024);
        for (j = 0; j < 512; j++)
            bad_out += !(buf[j] > -(1 << 24) && buf[j] < (1 << 24));
        bad_out += en_pad_level != en_pad_level || en_bass_level != en_bass_level;
    }
}
static void engine_start(uint32_t p, int root, uint32_t seed)
{
    static int32_t b[512];
    en_play(0);
    en_render(b, 256, 1024);
    en_init();
    en_trace_chord = on_chord;
    en_trace = on_bar;
    en_set(P_ROOT, root);
    en_pool(p);
    en_seed(seed);
    en_play(1);
    nch = 0;
    bad_out = 0u;
    en_render(b, 256, 1024);                     /* (en_bars starts again, after the first step) */
    nev = 0;
    ev_singles = en_ep_singles;
}

static int abs_in(uint32_t p, int root, int note) { return p >> (((note - root) % 12 + 12) % 12) & 1u; }
static int rel_in(uint32_t tones, int root, int note) { return tones >> (((note - root) % 12 + 12) % 12) & 1u; }
/* the chord sounding at a step */
static const ch_t *chord_at(uint32_t step)
{
    int i;
    for (i = nch - 1; i >= 0; i--)
        if (chs[i].step <= step)
            return &chs[i];
    return 0;
}

/* the chords the engine plays are the generator's, given the characters it was told; on bars, each 1, 2 or 4 long, the next
 * exactly when it says; the screen follows */
static void engine_checks(void)
{
    const uint32_t lyd = EN_MODE_POOL[MODE_LYDIAN];
    en_gen_t st;
    int i, evo;
    for (evo = 0; evo < 3; evo++) {
        const int e = evo == 0 ? 50 : evo == 1 ? 0 : 100;
        uint32_t last_c = 0u;
        float last_drift = -1.0f;
        int last_n = 0, publish_ok = 0;
        engine_start(lyd, 2, 1001u + (uint32_t)evo);
        en_set(P_EVOLVE, e);
        en_gen_start(&st);
        {
            /* en_set arrives after the first chord was made: restart with it in force */
            static int32_t b[512];
            en_play(0);
            en_render(b, 256, 1024);
            en_play(1);
            nch = 0;
            en_render(b, 256, 1024);
        }
        while (en_bars < 8000u) {
            static int32_t b[512];
            en_render(b, 64, 1024);
            CHECK(en_drift >= 0.0f && en_drift < 1.0f, "drift %f", (double)en_drift);
            if (nch != last_n) {
                last_n = nch;
                last_drift = -1.0f;
                last_c = en_chord_root;
                {
                    /* on the screen: this chord, its name by the same function */
                    const ch_t *c = &chs[nch - 1];
                    int pcs[5], v;
                    char x[24], y[24];
                    pcs[0] = en_chord_root;
                    for (v = 0; v < 4; v++)
                        pcs[v + 1] = en_chord[v] % 12;
                    en_chord_name(pcs, 5, x);
                    en_hchord_name(&c->g.ch, y);
                    publish_ok += !strcmp(x, y);
                    CHECK(en_chord_bars == c->g.bars, "the screen's length %d, the chord's %d", en_chord_bars, c->g.bars);
                }
            }
            CHECK(en_drift >= last_drift || en_drift < 0.05f, "the path to the next chord went back: %f after %f", (double)en_drift, (double)last_drift);
            last_drift = en_drift;
            (void)last_c;
        }
        CHECK(publish_ok >= nch - 1, "the screen names %d of %d chords", publish_ok, nch);
        CHECK(nch > 30, "only %d chords in 8000 steps", nch);
        for (i = 0; i < nch; i++) {
            en_gchord_t want;
            en_gen_next(&st, lyd, 2, 1001u + (uint32_t)evo, e, chs[i].character, &want);
            CHECK(!memcmp(&want.ch.tones, &chs[i].g.ch.tones, sizeof want.ch.tones) && want.shape == chs[i].g.shape && want.ch.root == chs[i].g.ch.root,
                  "evolve %d chord %d: plays another chord than the generator says", e, i);
            CHECK(!memcmp(want.keys_v, chs[i].g.keys_v, 4) && !memcmp(want.pad_v, chs[i].g.pad_v, 4), "evolve %d chord %d: other voices", e, i);
            CHECK(chs[i].step % 16u == 0u, "chord %d at step %u, off the bar", i, chs[i].step);
            if (i + 1 < nch)
                CHECK(chs[i + 1].step == chs[i].step + 16u * chs[i].g.bars, "chord %d of %d bars lasted %u steps", i, chs[i].g.bars, chs[i + 1].step - chs[i].step);
        }
        CHECK(!bad_out, "evolve %d: %u bad samples", e, bad_out);
        printf("  engine evolve %3d: %d chords in 8000 steps, each on its bar, each as the generator says\n", e, nch);
    }
}

/* the piano: one soft chord a change, its lone notes under the limit for its length; no voice stolen; the motifs inside one
 * chord; the bass on its chord, leading to the chord that really comes */
#define LONE_LIMIT(bars) ((bars) >= 4u ? 3u : (uint32_t)(bars))
static void flow_run(const char *name, uint32_t p, int root, int live)
{
    uint32_t next_change = 44100u * 50u, frames = 0u, c_seen = 0u, s_seen = 0u, att = 0u, sing = 0u;
    uint32_t lone_by_len[5] = {0u, 0u, 0u, 0u, 0u}, n_by_len[5] = {0u, 0u, 0u, 0u, 0u}, leads = 0u, exempt = 0u, motifs = 0u, motif_chords = 0u;
    int k = 0, last_n = 0, i, ei;
    uint32_t change_step[16];
    int nchange = 0;
    engine_start(p, root, 77u + (uint32_t)root);
    c_seen = en_ep_chords;
    s_seen = en_ep_singles;
    last_n = nch;
    while (frames < 44100u * 248u) {
        static int32_t buf[512];
        int j;
        en_render(buf, 256, 1024);
        frames += 256u;
        for (j = 0; j < 512; j++)
            CHECK(buf[j] > -(1 << 24) && buf[j] < (1 << 24), "%s: out of range", name);
        if (nch != last_n) {                     /* a chord has just ended */
            const ch_t *was = &chs[nch - 2 >= 0 ? nch - 2 : 0];
            const uint32_t a = en_ep_chords - c_seen, b = en_ep_singles - s_seen;
            /* (this count includes the new chord's own attack, the old one's is in the last) */
            CHECK(a <= 1u, "%s: %u chord attacks a change", name, a);
            CHECK(b <= LONE_LIMIT(was->g.bars), "%s: %u lone notes on a chord of %d bars", name, b, was->g.bars);
            att += a;
            sing += b;
            lone_by_len[was->g.bars] += b;
            n_by_len[was->g.bars]++;
            c_seen = en_ep_chords;
            s_seen = en_ep_singles;
            last_n = nch;
        }
        if (live && frames >= next_change && nchange < 16) {
            next_change += 44100u * 25u;
            change_step[nchange++] = en_bars;
            switch (k++) {
            case 0: en_pool(EN_MODE_POOL[MODE_AEOLIAN]); break;
            case 1: en_set(P_ROOT, 9); break;
            case 2: en_pool(EN_MODE_POOL[MODE_AEOLIAN] | 1u << 9); break;
            case 3: en_pool(EN_MODE_POOL[MODE_AEOLIAN] & ~(1u << 8)); break;
            case 4: en_pool(EN_MODE_POOL[MODE_PENT_MAJ]); break;
            case 5: en_set(P_ROOT, 2); break;
            default: en_pool(p); break;
            }
        }
    }
    /* each change lands, at the next chord, within four bars */
    for (i = 0; i < nchange; i++) {
        int c;
        uint32_t after = 0u;
        for (c = 0; c < nch && !after; c++)
            if (chs[c].step > change_step[i])
                after = chs[c].step;
        CHECK(after && after - change_step[i] <= 65u, "%s: a change at %u waited until %u", name, change_step[i], after);
    }
    /* the notes against the chords */
    for (ei = 0; ei < nev; ei++) {
        const ev_t *e = &ev[ei];
        const ch_t *c = chord_at(e->step);
        int near = 0;
        if (!c)
            continue;
        for (i = 0; i < nchange; i++)
            near |= e->step + 72u >= change_step[i] && e->step <= change_step[i] + 72u;
        if (near)
            continue;                            /* (live_checks looks at what happens around a change) */
        if (e->kind == 2) {
            const uint32_t in = e->step - c->step, steps = 16u * c->g.bars;
            motifs++;
            CHECK(steps >= 32u, "%s: a motif on a chord of %d bars", name, c->g.bars);
            CHECK(in + 2u * EN_ECHO_STEPS < steps, "%s: a motif note at %u of a chord of %u: its echo leaves", name, in, steps);
            CHECK(rel_in(c->g.ch.tones, c->g.ch.tonic, e->note), "%s: motif note %d not in the chord", name, e->note);
            motif_chords += in == 8u;
        } else if (e->kind == 3) {
            const uint32_t steps = 16u * c->g.bars, in = e->step - c->step;
            const ch_t *nx = c + 1;
            const int lv = bar_level[e->step / 16u];
            if (in + 2u == steps && (lv == 2 || lv == 3) && nx < &chs[nch]) {
                /* a note here on a chord of two bars or more can only be the leading tone; on a one-bar chord it may be its figure's */
                const int pc = (e->note - c->g.ch.tonic + 120) % 12, to = nx->g.ch.bass;
                int d = (pc - to + 12) % 12;
                d = d > 6 ? 12 - d : d;
                if (steps >= 32u || !rel_in(c->g.ch.tones, 0, pc)) {
                    CHECK(d >= 1 && d <= 2, "%s: chord at %u: the bass leads with %d to %d, %d away", name, c->step, pc, to, d);
                    leads++;
                } else {
                    exempt++;
                }
            } else {
                CHECK(rel_in(c->g.ch.tones, c->g.ch.tonic, e->note), "%s: bass note %d at %u of %u: not on its chord", name, e->note, in, steps);
            }
            CHECK((e->note - c->g.ch.tonic + 120) % 12 != 0, "%s: the tonic in the bass", name);
        } else if (e->kind == 1) {
            CHECK(abs_in(p, root, e->note) || c->g.ch.borrowed || live, "%s: lone note %d outside the pool", name, e->note);
        }
    }
    printf("  flow %-10s %d chords: %.2f chord attacks a change; lone notes a chord of 1 bar %.2f, 2 %.2f, 4 %.2f; %u motif notes in %u phrases; %u leading tones; peak %d voices, %u stolen\n", name, nch, (double)att / (nch > 1 ? nch - 1 : 1),
           (double)lone_by_len[1] / (n_by_len[1] ? n_by_len[1] : 1u), (double)lone_by_len[2] / (n_by_len[2] ? n_by_len[2] : 1u), (double)lone_by_len[4] / (n_by_len[4] ? n_by_len[4] : 1u), motifs, motif_chords, leads, en_ep_peak, en_ep_steals);
    (void)sing;
    (void)exempt;
    CHECK(leads > 0u, "%s: no leading tones", name);
    CHECK(motifs > 0u, "%s: no motif notes", name);
    CHECK(en_ep_steals == 0u, "%s: %u voices stolen while sounding", name, en_ep_steals);
    CHECK(en_ep_peak <= 12, "%s: peak %d voices", name, en_ep_peak);
}

static void flow_checks(void)
{
    static const int ROOT[EN_NMODES] = {2, 2, 4, 2, 2, 9, 2, 2};
    int m;
    for (m = 0; m < EN_NMODES; m++)
        flow_run(EN_MODE_NAME[m], EN_MODE_POOL[m], ROOT[m], 0);
    flow_run("live", EN_MODE_POOL[MODE_LYDIAN], 2, 1);
}

/* someone at the keys all the time, a note in, a note out: a chord's lone notes stay under the limit for its length */
static void lone_checks(void)
{
    static const uint8_t NOTES[3] = {1, 3, 8};
    const uint32_t ion = EN_MODE_POOL[MODE_IONIAN];
    uint32_t k = 0u, full[5] = {0u, 0u, 0u, 0u, 0u};
    int i, c;
    engine_start(ion, 2, 606u);
    while (en_bars < 6000u) {
        render_until(en_bars + 2u);
        en_pool(k % 2u ? ion : ion | 1u << NOTES[(k / 2u) % 3u]);
        k++;
    }
    CHECK(!bad_out, "at the keys: %u bad samples", bad_out);
    for (c = 0; c + 1 < nch; c++) {
        uint32_t lone = 0u;
        for (i = 0; i < nev; i++)
            lone += ev[i].kind == 1 && ev[i].step >= chs[c].step && ev[i].step < chs[c + 1].step;
        CHECK(lone <= LONE_LIMIT(chs[c].g.bars), "at the keys: %u lone notes on a chord of %d bars", lone, chs[c].g.bars);
        full[chs[c].g.bars] += lone == LONE_LIMIT(chs[c].g.bars);
    }
    printf("  at the keys: chords at their limit of lone notes: of 1 bar %u, 2 %u, 4 %u\n", full[1], full[2], full[4]);
    CHECK(full[2] > 0u && full[4] > 0u, "the limit never reached");
}

/* the bass as a pure function: every set of figures on chords of 1, 2 and 4 bars: long notes, on the chord, never the tonic, the
 * time filled, the leading tone to the next chord's bass. The first note of a chord is its bass */
static void bass_checks(void)
{
    static const int SETS[4] = {EN_BASS_LIGHT, EN_BASS_FULL, EN_BASS_ROLL, EN_BASS_LONG};
    static const uint32_t POOLS[4] = {0xAD5u, 0xAB5u, 0x5ADu, 0x295u};
    int set, pi;
    for (set = 0; set < 4; set++) {
        const int level = SETS[set];
        uint32_t notes = 0u, bars = 0u, leads = 0u, chords = 0u, by_len[5] = {0u, 0u, 0u, 0u, 0u};
        for (pi = 0; pi < 4; pi++) {
            uint32_t sd;
            for (sd = 0u; sd < 60u; sd++) {
                en_gen_t st;
                en_gchord_t cur, nx;
                uint32_t n;
                en_gen_start(&st);
                en_gen_next(&st, POOLS[pi], 2, sd, 50, (int)(sd & 3u), &cur);
                for (n = 0u; n < 200u; n++) {
                    const uint32_t steps = 16u * cur.bars;
                    uint32_t cs, filled = 0u, next_at = 0u;
                    en_gen_next(&st, POOLS[pi], 2, sd, 50, (int)(sd & 3u), &nx);
                    for (cs = 0u; cs < steps; cs++) {
                        int len = 0;
                        const int off = en_bass_ch(&cur.ch, &nx.ch, POOLS[pi], n, cs, steps, level, sd, &len);
                        const int pc = (off + 120) % 12;
                        if (off < 0)
                            continue;
                        CHECK(cs >= next_at, "seed %u: a note at %u before the last ended at %u", sd, cs, next_at);
                        next_at = cs + (uint32_t)len;
                        notes++;
                        filled += (uint32_t)len;
                        CHECK(pc != 0, "pool %03x seed %u: the tonic in the bass", POOLS[pi], sd);
                        CHECK(off >= 2 && off <= 24, "pool %03x seed %u: bass note %d", POOLS[pi], sd, off);
                        CHECK(len >= 2 && cs + (uint32_t)len <= steps, "seed %u: a note of %d at %u of %u", sd, len, cs, steps);
                        if (!cs)
                            CHECK(off == (cur.ch.bass < 4 ? cur.ch.bass + 12 : cur.ch.bass), "the chord's first note is not its bass");
                        if (cs + 2u == steps && !(cur.ch.tones >> pc & 1u)) {
                            int d = (pc - nx.ch.bass + 12) % 12;
                            d = d > 6 ? 12 - d : d;
                            CHECK(d >= 1 && d <= 2, "the leading tone %d is %d from the next bass %d", pc, d, nx.ch.bass);
                            leads++;
                        } else if (cs + 2u == steps && steps >= 32u) {
                            int d = (pc - nx.ch.bass + 12) % 12;
                            d = d > 6 ? 12 - d : d;
                            CHECK(d >= 1 && d <= 2, "the leading tone %d is %d from the next bass %d", pc, d, nx.ch.bass);
                            leads++;
                        } else {
                            CHECK(cur.ch.tones >> pc & 1u, "seed %u: %d not on the chord", sd, off);
                        }
                    }
                    CHECK(filled == steps, "pool %03x set %d: the notes fill %u of %u steps", POOLS[pi], level, filled, steps);
                    bars += cur.bars;
                    by_len[cur.bars]++;
                    chords++;
                    cur = nx;
                }
            }
        }
        printf("  bass set %d: %.2f notes a bar; chords of 1 bar %u, 2 %u, 4 %u; %u leading tones in %u chords\n", level, (double)notes / bars, by_len[1], by_len[2], by_len[4], leads, chords);
        CHECK((double)notes / bars <= (level == EN_BASS_ROLL ? 3.25 : level == EN_BASS_FULL ? 3.0 : 2.25), "set %d: too many notes: %.2f", level, (double)notes / bars);
        CHECK(leads * 4u >= chords, "set %d: %u leading tones in %u chords", level, leads, chords);
    }
}

/* the motifs, pure: the note and its first two echoes in the chord, whatever the pool; long enough chords have them whole */
static void motif_checks(void)
{
    int m, n;
    for (m = 0; m < 2; m++)
        for (n = 0; n < 4; n++) {
            CHECK(en_motif_fits(m, n, 64u), "motif %d note %d not whole on four bars", m, n);
            CHECK(!en_motif_fits(m, n, 16u), "motif %d note %d on one bar", m, n);
            if (n < 3)
                CHECK(en_motif_fits(m, n, 32u), "motif %d: note %d left out on two bars", m, n);
            else
                CHECK(!en_motif_fits(m, n, 32u), "motif %d: its echoes leave a chord of two bars", m);
        }
    {
        uint32_t mask;
        for (mask = 1u; mask <= 4095u; mask += 2u) {
            en_gen_t st;
            en_gchord_t g;
            int i;
            if (popcount(mask) < 3)
                continue;
            en_gen_start(&st);
            for (i = 0; i < 12; i++) {
                en_gen_next(&st, mask, 0, 3u, 50, i & 3, &g);
                for (m = 0; m < 2; m++)
                    for (n = 0; n < 4; n++)
                        CHECK(g.ch.tones >> ((en_motif_note_ch(m, n, &g.ch) + 120) % 12) & 1u, "pool %03x: motif out of its chord", mask);
            }
        }
    }
}

/* a change while it plays: lone notes and motifs obey at once, the chord and the bass wait for the next chord; then they are in
 * it, within four bars. A change late in a chord remakes what was prepared */
static int motif_early;
static void change_seen(const char *what, uint32_t p0, int r0, uint32_t p1, int r1, uint32_t at, int by_root)
{
    int i, lone = 0, motif = 0, before = 0, after = 0, c0;
    uint32_t seam = 0u, end;
    motif_early = 0;
    engine_start(p0, r0, 4242u);
    render_until(at);
    nev = 0;
    c0 = nch;
    if (by_root)
        en_set(P_ROOT, r1);
    else
        en_pool(p1);
    render_until(at + 200u);
    for (i = c0; i < nch && !seam; i++)
        if (chs[i].step >= at)
            seam = chs[i].step;
    CHECK(seam && seam - at <= 64u, "%s: no chord within 4 bars of step %u: %u", what, at, seam);
    end = at + 200u;
    {
        const ch_t *nc = chord_at(seam);
        CHECK(nc && (!((uint32_t)nc->g.ch.tones & ~p1) || nc->g.ch.borrowed), "%s: the chord at %u not in the new pool", what, seam);
        CHECK(nc && nc->g.ch.tonic == r1, "%s: the chord at %u not in the new key", what, seam);
    }
    /* the first chord after the change, and what comes after */
    for (i = 0; i < nev; i++) {
        const ev_t *e = &ev[i];
        const ch_t *c = chord_at(e->step);
        const int late = e->step >= seam;
        if (e->step < at || e->step >= end)
            continue;
        if (e->kind == 1 || e->kind == 2) {
            if (!(late && c->g.ch.borrowed))
                CHECK(abs_in(p1, r1, e->note), "%s: step %u: a %s note %d outside the new pool", what, e->step, e->kind == 1 ? "lone" : "motif", e->note);
            lone += e->kind == 1;
            motif += e->kind == 2;
            motif_early += e->kind == 2 && !late;
        } else if (!late) {
            if (e->kind == 0)
                CHECK(rel_in(c->g.ch.tones, r0, e->note), "%s: step %u: a chord note %d not of the old chord", what, e->step, e->note);
            else
                CHECK(abs_in(p0, r0, e->note) || c->g.ch.borrowed, "%s: step %u: bass %d left the old key before the chord", what, e->step, e->note);
            before++;
        } else {
            if (!c->g.ch.borrowed)
                CHECK(abs_in(p1, r1, e->note), "%s: step %u: %d after the chord, outside the new pool", what, e->step, e->note);
            after++;
        }
    }
    CHECK(after > 0, "%s: %d notes before, %d after", what, before, after);
    /* the screen: what is playing */
    {
        const ch_t *c = &chs[nch - 1];
        int pcs[5], v;
        char a[24], b[24];
        pcs[0] = en_chord_root;
        for (v = 0; v < 4; v++)
            pcs[v + 1] = en_chord[v] % 12;
        en_chord_name(pcs, 5, a);
        en_hchord_name(&c->g.ch, b);
        CHECK(!strcmp(a, b), "%s: the screen says %s, the chord is %s", what, a, b);
    }
    printf("  %s at step %u: next chord at %u: %d lone, %d motif notes obeyed at once; %d chord and bass notes waited, %d followed\n", what, at, seam, lone, motif, before, after);
}

/* just before a chord: find out when it comes, by a first run, then change it that late */
static void change_late(uint32_t p0, uint32_t p1, int back)
{
    uint32_t at = 0u, tgt;
    int i;
    engine_start(p0, 2, 4242u);
    render_until(1500u);
    for (i = 0; i < nch; i++)
        if (chs[i].step > 1000u) {
            at = chs[i].step;
            break;
        }
    tgt = at - (uint32_t)back;
    engine_start(p0, 2, 4242u);
    render_until(tgt);
    nev = 0;
    en_pool(p1);
    render_until(at + 2u);
    {
        const ch_t *c = chord_at(at);
        CHECK(c && c->step == at, "a chord was due at %u", at);
        CHECK(c && (!((uint32_t)c->g.ch.tones & ~p1) || c->g.ch.borrowed), "a change %d steps before the chord did not take it", back);
    }
}

static void live_checks(void)
{
    const uint32_t ion = EN_MODE_POOL[MODE_IONIAN], lyd = EN_MODE_POOL[MODE_LYDIAN];
    change_seen("mode", lyd, 2, EN_MODE_POOL[MODE_AEOLIAN], 2, 700u, 0);
    change_seen("mode, later", lyd, 2, EN_MODE_POOL[MODE_DORIAN], 2, 1900u, 0);
    change_seen("root", ion, 2, ion, 9, 900u, 1);
    change_seen("a note out", ion, 2, ion & ~(1u << 4), 2, 1100u, 0);
    change_seen("into a pentatonic", lyd, 2, EN_MODE_POOL[MODE_PENT_MIN], 2, 1276u, 0);
    {
        static const int BACK[6] = {12, 8, 4, 2, 1, 0};   /* 0: the last step has run, the chord's has not */
        int b;
        for (b = 0; b < 6; b++)
            change_late(lyd, EN_MODE_POOL[MODE_PENT_MIN], BACK[b]);
    }
    /* a motif under way: its remaining notes obey at once */
    {
        int m0, ok = 0, tries;
        for (tries = 0; tries < 8 && !ok; tries++) {
            int i, got = -1;
            uint32_t from = 0u;
            engine_start(ion, 2, 900u + (uint32_t)tries);
            while (en_bars < 4000u && got < 0) {
                render_until(en_bars + 1u);
                for (i = 0; i < nev; i++)
                    if (ev[i].kind == 2) {
                        got = i;
                        from = ev[i].step;
                        break;
                    }
            }
            if (got < 0)
                continue;
            en_pool(EN_MODE_POOL[MODE_PENT_MIN]);
            m0 = nev;
            render_until(from + 8u);
            for (i = m0; i < nev; i++)
                if (ev[i].kind == 2 && ev[i].step > from + 1u) {
                    CHECK(abs_in(EN_MODE_POOL[MODE_PENT_MIN], 2, ev[i].note), "a motif note %d at step %u, after the change, outside the new pool", ev[i].note, ev[i].step);
                    ok = 1;
                }
        }
        CHECK(ok, "no motif under way to judge by");
    }
    /* a note in: heard on the next eighth, once */
    {
        const uint32_t p1 = ion | 1u << 3;       /* F natural into D Ionian */
        int i, heard = -1;
        uint32_t at = 0u;
        engine_start(ion, 2, 4242u);
        /* a moment in a long chord, so the limit on lone notes leaves it room */
        while (!at && en_bars < 4000u) {
            render_until(en_bars + 1u);
            if (nch && chs[nch - 1].g.bars == 4u && en_bars == chs[nch - 1].step + 2u)
                at = en_bars;
        }
        CHECK(at, "no chord of four bars");
        nev = 0;
        en_pool(p1);
        render_until(at + 6u);
        for (i = 0; i < nev && heard < 0; i++)
            if (ev[i].kind == 1 && (ev[i].note + 120 - 2) % 12 == 3)
                heard = (int)ev[i].step;
        CHECK(heard >= 0 && (uint32_t)heard <= at + 3u, "the new note speaks at step %d after %u", heard, at);
        printf("  a note in at step %u: heard on step %d\n", at, heard);
    }
    /* same seed, same notes: the human errors of time and touch never choose one */
    {
        static uint32_t a[NEV];
        const uint32_t p0 = EN_MODE_POOL[MODE_DORIAN];
        int i, na;
        engine_start(p0, 2, 4242u);
        render_until(600u);
        en_pool(lyd);
        render_until(1200u);
        na = nev;
        for (i = 0; i < na; i++)
            a[i] = ev[i].step << 10 | (uint32_t)ev[i].kind << 8 | ev[i].note;
        engine_start(p0, 2, 4242u);
        render_until(600u);
        en_pool(lyd);
        render_until(1200u);
        CHECK(nev == na, "the same seed: %d notes, then %d", na, nev);
        for (i = 0; i < na && i < nev; i++)
            CHECK(a[i] == (ev[i].step << 10 | (uint32_t)ev[i].kind << 8 | ev[i].note), "the same seed: note %d differs", i);
    }
    /* freeze: the chord stays, the rest of the way waits */
    {
        int n0;
        uint8_t root;
        static int32_t b[512];
        int k;
        engine_start(lyd, 2, 4242u);
        render_until(400u);
        n0 = nch;
        root = en_chord_root;
        en_freeze(1);
        for (k = 0; k < 4000; k++)
            en_render(b, 256, 1024);
        CHECK(nch == n0 && en_chord_root == root, "frozen, the chord moved");
        en_freeze(0);
        render_until(en_bars + 90u);
        CHECK(nch > n0, "after the freeze no chord");
    }
}

/* An hour, fast, with someone at everything: every chord in the pool it was made for, on its bar, never long; never a bad sample. */
static void hour_checks(void)
{
    const uint32_t steps = 3600u * 170u * 4u / 60u;
    static const uint32_t POOLS[6] = {0xAD5u, 0xAB5u, 0x5ADu, 0x6ADu, 0x295u, 0x4A9u};
    uint32_t h = 2024u, last = 0u, k = 0u, pool_now = POOLS[0];
    int root_now = 2, seen = 0;
    uint32_t pool_was = pool_now, change_at = 0u;
    static int32_t b[512];
    engine_start(POOLS[0], 2, 31u);
    nch = 0;
    while (en_bars < steps) {
        int j;
        if (nch >= NCH - 8) {                    /* (a long run: forget what has been looked at) */
            nch = 0;
            seen = 0;
        }
        en_test_ticks(1u);
        en_render(b, 32, 1024);
        for (j = 0; j < 64; j++)
            bad_out += !(b[j] > -(1 << 24) && b[j] < (1 << 24));
        bad_out += en_pad_level != en_pad_level || en_bass_level != en_bass_level || en_drift != en_drift || en_drift < 0.0f || en_drift >= 1.0f;
        if (++k % 96u == 0u) {
            h = h * 1664525u + 1013904223u;
            switch ((h >> 16) % 8u) {
            case 0: en_set(P_EVOLVE, (int)((h >> 8) % 101u)); break;
            case 1: en_set(P_ENERGY, (int)((h >> 8) % 101u)); break;
            case 2: en_set(P_STYLE, (int)((h >> 8) % EN_NSTYLES)); break;
            case 3: en_set(P_TEMPO, 150 + (int)((h >> 8) % 31u)); break;
            case 4: en_set(P_BREAKS, (int)((h >> 8) % 101u)); break;
            case 5:
                pool_was = pool_now;
                pool_now = POOLS[(h >> 8) % 6u];
                change_at = en_bars;
                en_pool(pool_now);
                break;
            case 6:
                root_now = (int)((h >> 8) % 12u);
                en_set(P_ROOT, root_now);
                break;
            default: break;
            }
        }
        while (seen < nch) {
            const ch_t *c = &chs[seen];
            if (seen)
                CHECK(c->step - chs[seen - 1].step <= 64u, "a chord lasted %u", c->step - chs[seen - 1].step);
            CHECK(c->step % 16u == 0u, "a chord off the bar at %u", c->step);
            CHECK(c->g.ch.bass != 0u, "the tonic in the bass");
            if (!c->g.ch.borrowed)
                CHECK(!(c->g.ch.tones & ~pool_now) || !(c->g.ch.tones & ~pool_was) || c->step < change_at + 64u, "a chord outside the pool");
            last = c->step;
            seen++;
        }
    }
    CHECK(!bad_out, "an hour at the knobs: %u bad samples or NaN", bad_out);
    CHECK(en_ep_peak <= 12, "%d voices", en_ep_peak);
    printf("  an hour at the knobs: %u steps, last chord at %u, no bad sample\n", en_bars, last);
    en_set(P_TEMPO, 170);
}

/* the drums, as the player sees them: a plain two-step, breaks now and then, back on the grid; deterministic for a seed */
#define DSTEPS 6400u
static char core_a[DSTEPS], top_a[DSTEPS], hat_a[DSTEPS];
static uint8_t brk_in_bar[DSTEPS / 16u];
static void drums(uint32_t seed, int level, char *c, char *t, char *h)
{
    uint32_t i;
    brk_gen_t g;
    brk_gen_start(&g);
    brk_init();
    brk_levels(1.0f, 1.0f, 1.0f);
    for (i = 0u; i < DSTEPS; i++) {
        if (!(i & 15u)) {
            uint32_t in = 0u;
            const int want = en_arr_break(i / 16u, level, EN_ST_LIQUID, en_param_info(P_BREAKS)->def, seed, &in);
            brk_gen_bar(&g, en_style_rhythm(EN_ST_LIQUID), level, en_param_info(P_ENERGY)->def, en_param_info(P_EVOLVE)->def, seed, i / 16u, want, in, &brk_bar);
            brk_in_bar[i / 16u] = (uint8_t)want;
        }
        brk_pattern_step(i, 0, seed);
        c[i] = brk_hit_core;
        t[i] = brk_hit_top;
        h[i] = brk_hat_hit;
    }
}
static int is_ghost(char c) { return c == 'g' || c == 'f' || c == 'e'; }

static void drum_checks(void)
{
    static char c2[DSTEPS], t2[DSTEPS], h2[DSTEPS];
    int level;
    uint32_t i;
    for (level = 2; level <= 3; level++) {
        uint32_t breaks = 0u, bbars = 0u, plain_k = 0u, plain_s = 0u, b;
        int diff = 0, prev = -1;
        drums(1234u, level, core_a, top_a, hat_a);
        drums(1234u, level, c2, t2, h2);
        CHECK(!memcmp(core_a, c2, DSTEPS) && !memcmp(top_a, t2, DSTEPS) && !memcmp(hat_a, h2, DSTEPS),
              "level %d: same seed, other drums", level);
        for (i = 0u; i < DSTEPS; i++) {
            const uint32_t bs = i % 16u;
            const int in = brk_in_bar[i / 16u] ? 0 : -1;
            const char c = core_a[i];
            CHECK(c != 0 && top_a[i] != 0 && hat_a[i] != 0, "a pattern too short at %u", i);
            if (in < 0) {                        /* plain: the two-step's backbone (tests/host/rhy_test.c has the rest) */
                if (bs == 4u || bs == 12u)
                    CHECK(c == 'S' || c == 's', "plain bar: no snare at %u", i);
                else
                    CHECK(c != 'S' && c != 's' && c != 'c', "plain bar: a snare off the backbone at %u", i);
                plain_k += c == 'K' || c == 'k';
                plain_s += c == 'S' || c == 's';
                if (prev >= 0)
                    CHECK(i % 128u == 0u, "a break ended off the block's edge at %u", i);
            } else {
                if (prev < 0) {
                    CHECK(bs == 0u, "a break began off the bar at %u", i);
                    breaks++;
                }
                if (bs == 0u)
                    bbars++;
            }
            prev = in;
        }
        /* a break's bar is busier but bounded, and they are occasional */
        for (b = 0u; b + 16u <= DSTEPS; b += 16u) {
            uint32_t hits = 0u, ghosts = 0u, j;
            for (j = 0u; j < 16u; j++) {
                hits += core_a[b + j] != '.';
                ghosts += is_ghost(top_a[b + j]);
            }
            CHECK(hits <= 8u, "level %d: %u hits in a bar", level, hits);
            CHECK(ghosts <= 5u, "level %d: %u ghosts in a bar", level, ghosts);
        }
        printf("  drums level %d: %u breaks, %u bars of break in %u bars; plain %u kicks, %u snares (seed 1234)\n", level, breaks, bbars,
               DSTEPS / 16u, plain_k, plain_s);
        CHECK(breaks > 0u, "level %d: no breaks", level);
        CHECK(bbars * 3u <= DSTEPS / 16u, "level %d: %u of %u bars are breaks", level, bbars, DSTEPS / 16u);
        drums(99u, level, c2, t2, h2);
        for (i = 0u; i < DSTEPS; i++)
            diff += core_a[i] != c2[i] || top_a[i] != t2[i];
        CHECK(diff > 0, "level %d: two seeds, the same drums", level);
    }
    drums(1234u, 1, core_a, top_a, hat_a);
    for (i = 0u; i < DSTEPS; i++) {
        CHECK(core_a[i] == '.', "level 1 has a kick or a snare at %u", i);
        CHECK(!is_ghost(top_a[i]), "level 1 has a ghost at %u", i);
    }
}

/* the hats alone, as they play in their section (the reverb and the echo open): dry and short */
#define HAT_SEED 1234u
static void hat_checks(void)
{
    static int32_t buf[512];
    static float mono[44100 * 40];
    const double st = 44100.0 * 60.0 / 170.0 / 4.0;
    uint32_t i = 0u, s, hats_from = 0u, hats_to = 0u;
    double peak_sum = 0.0, tail_sum = 0.0;
    int n = 0;
    en_play(0);                                  /* from a standstill: the clock's pulse starts at its top */
    en_render(buf, 256, 1024);
    en_init();
    en_trace = 0;
    en_trace_chord = 0;
    en_seed(HAT_SEED);
    en_set(P_MOTIFS, 0);
    en_set(P_BASS, 0);
    en_set(P_KEYS, 0);
    en_play(1);
    while (i + 256u < 44100u * 40u) {
        int j;
        en_render(buf, 256, 1024);
        if (en_drum_level == 1u && !hats_from)
            hats_from = en_bars;                 /* the first bar of hats */
        if (hats_from && (en_drum_level != 1u) && !hats_to)
            hats_to = en_bars;
        for (j = 0; j < 256; j++)
            mono[i + (uint32_t)j] = (float)(buf[2 * j] + buf[2 * j + 1]) * (1.0f / 16777216.0f);
        i += 256u;
    }
    if (!hats_to)
        hats_to = en_bars;
    CHECK(hats_from > 0u && hats_to > hats_from + 64u, "no section of hats in 40 s: %u..%u", hats_from, hats_to);
    /* level 1's hats: steps 2, 6, 10, 14 of a bar, a little after they begin: the next hit is 4 steps off */
    for (s = hats_from + 16u; s + 8u < hats_to; s++) {
        if (s % 4u == 2u) {
            const uint32_t i0 = (uint32_t)((double)s * st);
            double p = 0.0, t = 0.0;
            uint32_t k;
            for (k = i0; k < i0 + 1100u; k++)    /* the hit: up to 25 ms, with its swing */
                p = fabs((double)mono[k]) > p ? fabs((double)mono[k]) : p;
            for (k = i0 + 2646u; k < i0 + 3528u; k++)   /* its tail: 60 to 80 ms later */
                t += (double)mono[k] * (double)mono[k];
            peak_sum += p;
            tail_sum += sqrt(t / 882.0);
            n++;
        }
    }
    printf("  hats: %d hits, tail at 60-80 ms %.1f dB under the peak, reverb and echo open\n", n,
           20.0 * log10(peak_sum / (tail_sum + 1e-12)));
    CHECK(n > 10, "too few hats");
    CHECK(peak_sum > 0.001 * n, "the hats are silent: %f", peak_sum / n);
    CHECK(peak_sum > 56.0 * tail_sum, "the hats ring: %f against %f", peak_sum, tail_sum);   /* 35 dB */
    CHECK(en_hat_peak <= 2, "%d hat voices", en_hat_peak);
    CHECK(en_hat_peak >= 1, "no hat voices");
}

int main(void)
{
    en_init();
    engine_checks();
    live_checks();
    flow_checks();
    lone_checks();
    bass_checks();
    motif_checks();
    hour_checks();
    drum_checks();
    hat_checks();
    printf(fails ? "harm: %d FAILED\n" : "harm: ok\n", fails);
    return fails != 0;
}
