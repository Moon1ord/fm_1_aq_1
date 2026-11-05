/* SPDX-License-Identifier: MPL-2.0 */
/* Nothing the render keeps from one block to the next sits among the denormals: on the device (and on most hosts) those are many times slower, and the voices and effects are the hottest loops there are.
 * Each block every float of the engine's state is looked at, by name, through every sound of the lists, the player's own sets at their ends, every effect, long held notes, long rests and what a panic leaves. What is listed here is every file-scope float there is as of writing, against the debug info: a new one in the engine belongs in reg_all. */
#include "amb.c"
#define stem brk_stem_
#define kit brk_kit_
#define inc brk_inc_
#include "brk.c"
#undef stem
#undef kit
#undef inc
#include <stdio.h>
#include <math.h>

void en_midi_out(uint32_t st, uint32_t d1, uint32_t d2) { (void)st, (void)d1, (void)d2; }

#define GAIN 2430u
#define SECS(s) ((uint32_t)((float)(s) * 44100.0f / 256.0f))
#define MAXREG 1024
static struct {
    const char *name;
    const volatile float *p;
    int n, every;
    uint32_t hits, first_block;
    const char *first_case;
    float worst;
} reg[MAXREG];
static int nreg, fails;
static uint32_t blocks;
static const char *now_case = "";
static int32_t buf[512];

static void reg1(const char *name, const volatile float *p, int n, int every)
{
    if (nreg >= MAXREG) {
        printf("FAIL: more than %d kinds of state: MAXREG\n", MAXREG);
        fails++;
        return;
    }
    reg[nreg].name = name, reg[nreg].p = p, reg[nreg].n = n, reg[nreg].every = every;
    nreg++;
}
#define F(x) reg1(#x, &(x), 1, 1)
#define A(x) reg1(#x, (x), (int)(sizeof(x) / sizeof(float)), 1)
#define FROM_TO(nm, first, last) reg1(nm, &(first), (int)(&(last) - &(first)) + 1, 1)

static void reg_all(void)
{
    int i;
    static const char *const EP[NKEYS > 12 ? NKEYS : 12] = {"ep[0]", "ep[1]", "ep[2]", "ep[3]", "ep[4]", "ep[5]", "ep[6]", "ep[7]", "ep[8]", "ep[9]", "ep[10]", "ep[11]"};
    F(tunefac), F(break_vol), F(step_len), F(pulse_left), A(brk_lp), F(brk_hp), A(plp), A(eplp);
    F(echo_send), F(rev_send), F(echo_len), F(echo_len_t), F(echo_fb);
    for (i = 0; i < NMOD; i++)
        FROM_TO("mod[]", mod[i].v, mod[i].rate);
    for (i = 0; i < EN_NPAD; i++)
        FROM_TO("pad[]", pad[i].ph[0], pad[i].now);
    F(pad_gain), F(pad_gate), F(pad_vol), F(pad_swell), F(pad_swell_t), F(pad_env);
    reg1("pad_s", (const float *)&pad_s, (int)(sizeof pad_s / sizeof(float)), 1);
    F(keys_comp), F(keys_comp_to), F(keys_comp_was), F(ku.att), F(ku.dec_k), F(ku.rel_k), F(pu.att_k), F(pu.dec_k), F(pu.rel_k);
    for (i = 0; i < NKEYS; i++)
        FROM_TO(i < 12 ? EP[i] : "ep[]", ep[i].cph, ep[i].urate);
    F(keys_bus), F(keys_bus_to), F(ep_vol), F(ep_trem), F(song_touch), F(bloom_att), F(bloom_open), F(bloom_close), F(bright_body), F(bright_tine), F(bright_bus), A(ep_gone);
    F(bass.ph), F(bass.inc), F(bass.inc_t), F(bass.amp), F(bass.sus), F(bass.idx), F(bass.spk), F(bass.mph);
    F(bass_vol), F(bass_lvl), F(bass_song), F(bass_song_t), F(bass_glide), F(bass_easy_rate), F(bass_k), F(bass_att), F(bass_rel), F(bass_dk);
    for (i = 0; i < NBELL; i++) {
        F(bell[i].cph), F(bell[i].mph), F(bell[i].cinc), F(bell[i].minc), F(bell[i].amp), F(bell[i].adec), F(bell[i].rdec), F(bell[i].idx), F(bell[i].idec), F(bell[i].ifl), F(bell[i].att), F(bell[i].arate);
        F(bell[i].gl), F(bell[i].gr), F(bell[i].esg), F(bell[i].edec), F(bell[i].vph), F(bell[i].vinc), F(bell[i].vib), F(bell[i].vramp), F(bell[i].ratio), F(bell[i].sh), F(bell[i].shd);
        F(bell[i].dg), F(bell[i].dgto), F(bell[i].iflto), F(bell[i].ui);
    }
    F(bell_vol), F(bell_stolen), A(bell_gone);
    for (i = 0; i < 2; i++)
        FROM_TO("hat[]", hat[i].amp, hat[i].lp2);
    F(hat_lvl), F(hat_lvl_t), F(hat_vol);
    A(mute_pos), A(mute_to), A(mg), A(mgd);
    F(brk_fc), F(brk_fc_t), F(pad_fc), F(pad_fc_t), F(duck), F(duck_s), F(lift), F(lift_t), F(duck0), F(duck1);
    F(pl_bw), F(pl_damp_l), F(pl_damp_r), F(pl_decay), F(pl_decay_t), F(pl_dd2), F(lfo_s), F(lfo_c);
    reg1("pl_buf", pl_buf, DL_TOTAL, 16);
    F(tape_acc), F(tape_lp), F(tape_hp), F(tape_p1), F(tape_p2), F(tape_c1), F(tape_c2), F(head2), F(head2_t), F(head2_v), F(fb_lean), A(hg), F(plex_lp), F(plex_lp2);
    for (i = 0; i < EN_FX_NLAYERS; i++) {
        F(fx[i].wet0), F(fx[i].wet), F(fx[i].ph), F(fx[i].err), F(fx[i].depth), F(fx[i].depth_to), F(fx[i].shape), F(fx[i].shape_to), F(fx[i].hz), F(fx[i].hz_to);
        F(fx[i].open), F(fx[i].open_to), F(fx[i].open_rate), F(fx[i].lift), F(fx[i].floor_hz), F(fx[i].floor_to), F(fx[i].note_floor), F(fx[i].fc);
        A(fx[i].a0), A(fx[i].a1), F(fx[i].c0), F(fx[i].c1), A(fx[i].ic), A(fx[i].slp), A(fx[i].g0), A(fx[i].g1), A(fx[i].d0), A(fx[i].d1);
        F(fx[i].eg), F(fx[i].eg0), F(fx[i].eg_to), F(fx[i].rg), F(fx[i].rg0), F(fx[i].rg_to);
    }
    F(vol), F(vol_t);
    FROM_TO("sys", sys.cph, sys.vel);
    F(en_drift), F(en_pad_level), F(en_bass_level), reg1("en_loop_glow", en_loop_glow, EN_NLOOP, 1), F(en_break_level);
    for (i = 0; i < BRK_NSTEM; i++) {
        int j;
        FROM_TO("stem[]", brk_stem_[i].lvl, brk_stem_[i].rem);
        for (j = 0; j < 2; j++) {
            FROM_TO("stem[].v", brk_stem_[i].v[j].frac, brk_stem_[i].v[j].late);
        }
    }
    F(xf_out), F(next_vel), F(brk_hat_vel), F(brk_inc_), F(tol);
    A(mixl), A(mixr), A(rsend), A(esend), A(padl), A(padr), A(fx_rs), A(fx_es), A(brk_mix), A(brk_top);   /* (a chunk's, not kept: what goes down the mix as it ends) */
}

static int sub(float x) { return x != 0.0f && fabsf(x) < 1.17549435e-38f; }
static void scan(void)
{
    int r, i;
    for (r = 0; r < nreg; r++) {
        if (blocks % (uint32_t)reg[r].every)
            continue;
        for (i = 0; i < reg[r].n; i++) {
            const float x = reg[r].p[i];
            if (sub(x)) {
                if (!reg[r].hits)
                    reg[r].first_case = now_case, reg[r].first_block = blocks;
                reg[r].hits++;
                break;
            }
        }
    }
}
static void blk(uint32_t n)
{
    while (n--) {
        en_render(buf, 256u, GAIN);
        blocks++;
        scan();
    }
}
static void start(const char *name)
{
    now_case = name;
    en_init();
    en_seed(4242u);
    en_arrange(1);
    en_restore();
    en_play(1);
    en_set(P_TEMPO, 170);
    blk(4);
}
static void user_ends(int l, int ends)           /* ends: 0 all at their lowest, 1 all at their highest, 2 Mod time short and the decay long, 3 no depth; 4.. one value to one end, the rest as the first sound */
{
    en_user_t u;
    int p;
    en_user_load(l, 0, &u);
    if (ends < 2) {
        for (p = 0; p < EN_NUP; p++)
            u.v[p] = (uint8_t)(ends ? en_user_steps(l, p) - 1 : 0);
    } else if (ends == 2) {
        u.v[EN_UP_MOD] = 0u;
        u.v[EN_UP_DEC] = (uint8_t)(en_user_steps(l, EN_UP_DEC) - 1);
    } else if (ends == 3) {
        u.v[EN_UP_DEPTH] = 0u;
    } else {
        const int q = ends - 4;
        u.v[q / 2] = (uint8_t)(q & 1 ? en_user_steps(l, q / 2) - 1 : 0);
    }
    en_user_set(l, &u);
}
#define NENDS (4 + 2 * EN_NUP)

static void a_bass(int snd, int ends)
{
    en_set(P_BASSSND, snd);
    if (ends >= 0)
        user_ends(EN_U_BASS, ends);
    en_play(0);                                  /* the figure's notes out of the way: one long note, then one with no attack of its own */
    blk(40);
    en_test_bass_note(40, 12u);
    blk(60);
    en_test_bass_note(43, 400u);                 /* legato on it, the speak going by */
    blk(SECS(4));
    en_test_bass_note(36, 600u);
    blk(SECS(6));
    blk(SECS(2));
}
static void a_melody(int snd, int ends, uint32_t held)
{
    en_set(P_MELMODE, EN_MEL_KEYS);
    en_set(P_MELSND, snd);
    if (ends >= 0)
        user_ends(EN_U_MEL, ends);
    blk(4);
    en_keys(1u << 4 | 1u << 7 | 1u << 11 | 1u << 14);   /* all four of its voices */
    blk(held);
    en_keys(0u);
    blk(SECS(4));
    en_keys(1u << 9);
    blk(held / 4u);
    en_keys(0u);
    blk(SECS(1));
}
static void a_song(uint32_t secs)
{
    en_play(1);
    blk(SECS(secs));
    en_hold(1);                                  /* a chord that stays */
    blk(SECS(secs) / 2u);
    en_hold(0);
    en_play(0);
    blk(SECS(secs));
}

static void report(void)
{
    int r;
    const char *seen[MAXREG];
    int nseen = 0;
    for (r = 0; r < nreg; r++)
        if (reg[r].hits) {
            int i, dup = 0;
            for (i = 0; i < nseen; i++)
                dup |= seen[i] == reg[r].name;
            if (dup)
                continue;
            seen[nseen++] = reg[r].name;
            printf("FAIL denormal in %s: %u blocks, first in \"%s\" at block %u\n", reg[r].name, reg[r].hits, reg[r].first_case, reg[r].first_block);
            fails++;
        }
}

int main(void)
{
    int s, e, l, fxk, m, d;
    static char names[512][48];
    int nn = 0;
    reg_all();
#define NAME(...) (snprintf(names[nn], sizeof names[nn], __VA_ARGS__), names[nn++ % 512])
    /* the bass: every sound of its list, Sub, base 0, the first among them; the player's at its ends */
    for (s = 0; s < 4; s++) {
        start(NAME("bass sound %d", s));
        a_bass(s, -1);
    }
    for (e = 0; e < NENDS; e++) {
        start(NAME("bass User ends %d", e));
        a_bass(4, e);
    }
    /* the melody: every timbre, its Auto; the player's, at its ends: Mod time 5 ms and Decay 18 s, the reviewer's */
    for (s = 0; s <= EN_NTIMBRES; s++) {
        start(NAME("melody sound %d", s));
        a_melody(s, -1, SECS(12));
    }
    for (e = 0; e < NENDS; e++) {
        start(NAME("melody User ends %d", e));
        a_melody(EN_MELSND_USER, e, SECS(e == 2 ? 20 : 6));
    }
    /* the keys and the pad: every sound in the song; the player's at its ends */
    for (s = 0; s < 4; s++) {
        start(NAME("keys sound %d, pad sound %d", s, s));
        en_set(P_KEYSND, s);
        en_set(P_PADSND, s);
        a_song(12);
    }
    for (l = 0; l < 2; l++)
        for (e = 0; e < NENDS; e++) {
            const int u = l ? EN_U_PAD : EN_U_KEYS;
            start(NAME("%s User ends %d", l ? "pad" : "keys", e));
            en_set(l ? P_PADSND : P_KEYSND, 4);
            user_ends(u, e);
            en_user_hear(u);
            a_song(6);
        }
    /* every effect on each of the two, Motion and Depth at their ends, the sends at 0 and as ever, in the song and in a long rest */
    for (l = 0; l < 2; l++)
        for (fxk = 1; fxk < EN_NFX; fxk++)
            for (m = 0; m <= 100; m += 100)
                for (d = 0; d <= 100; d += 100) {
                    const int base = l ? P_MFX : P_KFX;
                    start(NAME("effect %s on %s, motion %d, depth %d", EN_FX_NAME[fxk], l ? "melody" : "keys", m, d));
                    en_set(base, fxk);
                    en_set(base + 1, m);
                    en_set(base + 2, d);
                    if (d == 0) {
                        en_set(base + 3, 0);
                        en_set(base + 4, 0);
                    }
                    if (l && m) {
                        en_set(P_MELMODE, EN_MEL_KEYS);
                        blk(8);
                        en_keys(1u << 9);
                        blk(SECS(2));
                        en_keys(0u);
                        en_set(P_MELMODE, EN_MEL_PHRASES);
                    }
                    a_song(10);
                    en_set(base, EN_FX_OFF);     /* it fades out, in a rest */
                    blk(SECS(2));
                }
    /* a long rest after all of it, the echo and the room at the top; Echo and Reverb at 0; what a mute leaves; what a panic leaves */
    start("a rest of minutes, Echo and Reverb up");
    en_set(P_ECHO, 100);
    en_set(P_REVERB, 100);
    en_set(P_SIZE, 100);
    en_set(P_KFX, EN_FX_CHORUS);
    en_set(P_MFX, EN_FX_FILTER);
    a_song(20);
    blk(SECS(120));
    start("Echo and Reverb off");
    en_set(P_ECHO, 0);
    en_set(P_REVERB, 0);
    a_song(15);
    blk(SECS(60));
    start("every layer muted");
    a_song(5);
    for (l = 0; l < EN_NLAYERS; l++)
        en_layer(l, 0);
    en_play(1);
    blk(SECS(60));
    for (l = 0; l < EN_NLAYERS; l++)
        en_layer(l, 1);
    blk(SECS(5));
    start("a panic");
    a_song(5);
    en_play(1);
    blk(SECS(5));
    en_panic();
    blk(SECS(30));
    en_restore();
    blk(SECS(10));
    start("the system's voice in a rest");
    en_play(0);
    blk(SECS(1));
    en_sys(0);
    blk(SECS(5));
    en_sys(1);
    blk(SECS(5));
    start("a slow clock under a held chord");
    en_set(P_TEMPO, en_param_info(P_TEMPO)->lo);
    en_set(P_BRIGHT, 100);
    en_set(P_BLOOM, 100);
    en_hold(1);
    blk(SECS(60));
    report();
    printf("den_test: %u blocks, %d kinds of state looked at\n", blocks, nreg);
    printf(fails ? "den_test: %d FAILED\n" : "den_test: ok\n", fails);
    return fails != 0;
}
