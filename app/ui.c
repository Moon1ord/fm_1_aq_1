/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal */
/* AQ-1 UI: the panel and the screen. Part of the unity build (after gfx.c and project.c); the host
 * simulator includes it too.
 *
 *   keys           the scale: each key adds or removes its note from the pool (the root stays)
 *   SEL + a key    whether chords may stand on that note; it stays in the scale either way (two roots stay, besides the tonic)
 *   SEL + ENV ARP LFO SEQ FX   keys and pad, melody, bass, kick and snare, hats: switches (kept through the pieces; power-on: keys and pad; levels untouched)
 *   SEL + HOME     the arrangement: Hands (the switches and Energy are yours, no section or break comes by itself), or Auto (the arranger leads), kept with the piece
 *   PLAY           it grows / rests: drums, bass and motifs stop and ring out, the hand and the pad stay on the chord
 *   REC            hold: no new chord, no new section; the drums, the bass and the motifs play on over the chord
 *   EDIT           the next section, on the next bar    SEL + EDIT   a break, on the next bar, if there are drums then
 *   OCT- / OCT+    the melody's octave
 *   HOME + SELECT  tempo   ALGORITHM mode (loads a pool)   PRESETS the piece (Root is on the Setup page)
 *   SEL + KNOB 1   on Keys, Melody and Bass: the layer's sound (Keys: SEL + KNOB 2, the pad's). The four knobs are as ever otherwise; SEL + a knob on the other pages too
 *   SEL + KNOB 2, SEL + KNOB 3   on Melody: Phrases or Arp, and Arp's pattern. In Arp, Rate is its division
 *   SEL + KNOB 4   on Melody: Arp's skip, 0-100 (as a knob, not round and round)
 *   HOME ENV ARP LFO SEQ FX GLO   the pages: Home, Keys, Melody, Bass, Drums, Space, Setup (the SELECT encoder steps through them)
 *   KNOB 1-4       the page's four values
 *   SAVE           saves (it also saves by itself a few seconds after a change, while resting);
 *                  held 1.5 s: the piece back to its recipe */
#include "fastmath.h"

/* the pages, one a layer (a button each); SELECT steps through them in this order */
enum { V_GARDEN, V_KEYS, V_MOTIFS, V_BASS, V_DRUMS, V_SPACE, V_SETUP, NVIEWS };
static const char *const VIEW_NAME[NVIEWS] = {"Home", "Keys", "Melody", "Bass", "Drums", "Space", "Setup"};
static const uint8_t VIEW_BTN[NVIEWS] = {B_HOME, B_ENV, B_ARP, B_LFO, B_SEQ, B_FX, B_GLO};
/* the four knobs of each page: a parameter, or one of these; and what the knob is called there */
enum { K_LEDS = 100, K_CPU, K_NONE };
static const uint8_t VIEW_KNOB[NVIEWS][4] = {
    {P_TEMPO, P_ENERGY, P_EVOLVE, P_STYLE},
    {P_KEYS, P_BLOOM, P_BRIGHT, P_NOTES},
    {P_MOTIFS, P_MRATE, P_MTONE, P_MOCT},
    {P_BASS, P_BMOVE, P_GLIDE, P_BASSOCT},
    {P_DRUMS, P_BREAKS, P_HATS, P_KIT},
    {P_ECHO, P_ETIME, P_REVERB, P_SIZE},
    {P_ROOT, P_TUNE, P_MIDI, K_LEDS},
};
static const char *const KNOB_LABEL[NVIEWS][4] = {
    {"Tempo", "Energy", "Evolve", "Style"},
    {"Level", "Bloom", "Bright", "Notes"},
    {"Level", "Rate", "Tone", "Octave"},
    {"Level", "Move", "Glide", "Octave"},
    {"Level", "Breaks", "Hats", "Kit"},
    {"Echo", "Echo time", "Reverb", "Size"},
    {"Root", "Tune", "MIDI out", "Lights"},
};

#include "art.h"
#include "art_data.h"

#ifdef PLAT_HOST
static int32_t host_blk_peak[2], host_write_peak = -1;   /* the simulator's: the last two blocks at their loudest (the DMA's ring); that at the last write */
#endif
static struct {
    uint8_t view;
    uint8_t sel_layer;                 /* SEL is down and has gone with a page's button: no degrees this hold */
    uint32_t btn, keys, btn_used;
    uint32_t enc_t[NE];
    uint8_t enc_run[NE];               /* detents in the current quick run, per knob (accel) */
    uint32_t down_t[NB];               /* when each button went down */
    uint32_t turn_t;                   /* the last knob turn */
    uint8_t help;                      /* a button's card is up: the button + 1 */
    uint8_t reset_done;                /* SAVE held: the piece went back to its recipe this hold */
    uint8_t edit_sel;                  /* EDIT went down with SEL held: a break, whichever comes up first */
    uint8_t dirty;                     /* the project changed since the last save */
    uint32_t act_t, saved_t;           /* the last touch, the last write */
    uint32_t save_t;                   /* the last time SAVE saved (the screen's animation) */
    char msg[2][24];
    uint32_t msg_until;
    int8_t touched;                    /* the knob turned last (0..3), shown bright for a moment */
    uint32_t touch_until;
    uint32_t frame;
} ui;

/* the screen's (below): which scene is up, how far its picture has gone, and what moves on it */
enum { SC_TITLE, SC_HOME, SC_PAGE, SC_BREAK, SC_CARD, SC_SAVE, SC_REST, SC_HOLD, SC_DEG, SC_HELP };
enum { E_HDR, E_K0, E_K1, E_K2, E_K3, E_TEXT, E_TEXT2, E_CHORD, E_BEAD0, E_BEAD11 = E_BEAD0 + 11, E_EYES, E_MOUTH, E_LAYERS, E_METER, E_FEATHER, E_STAR0, E_STAR4 = E_STAR0 + 4, E_N };
typedef struct {
    uint32_t sig;
    uint8_t x, y, w, h;                /* where it stands */
    uint8_t dx0, dy0, dx1, dy1;        /* what is to be painted of it */
} el_t;
static el_t el[E_N];
static struct {
    uint32_t now, key, title_until, act_boot, card_until, card_t;
    uint32_t r_t, r_cpu, r_max, r_x, r_s;   /* Setup's reading, as shown */
    uint16_t wipe_y, wipe_left;        /* the picture going on: the next row, the rows to go */
    int32_t sx;                        /* the canvas's left edge on the screen */
    uint8_t scene, back_ok, fresh, settling;
    uint8_t eye, blinking, sec_seen, feather_on, feather_x, star_on, star_i;
    uint32_t eye_t, blink_t, blink_bar, bar_seen, open_until, smile_until, chord_seen, feather_t, star_t, star_bar;
#ifdef PLAT_HOST
    uint32_t f_rows, f_bytes, f_px, p_rows, p_bytes, p_px, t_bytes, wipes, blinks;   /* the simulator's: a frame's, the most in one, all so far */
    uint32_t brk_shown, brk_quiet, brk_wrong;   /* and frames: of the break's frame; of a break under way that nobody hears; of the frame over one */
#endif
} scr;

/* the screen reader (announce.c; speak() below): a message went to it this frame, so speak() leaves what it
 * changed be; and the transport as last said (PLAY says its own: the engine takes it a block later) */
static uint8_t sp_said;
static struct {
    uint8_t playing;
    uint32_t play_t;                   /* when PLAY (or the start) set it: the engine catches up within ms */
} sp_tp;
static void sp_piece(void);
#define PIECE_CARD_MS 2600u
static void msg_text(const char *a, const char *b, char *t)   /* the message as a reader gets it: "a b" */
{
    uint32_t i, k = 0;
    for (i = 0; i < 23u && a && a[i]; i++)
        t[k++] = a[i];
    if (b && b[0])
        t[k++] = ' ';
    for (i = 0; i < 23u && b && b[i]; i++)
        t[k++] = b[i];
    while (k && t[k - 1] == '.')               /* "UPDATE MODE IN 3..": the dots are for the eye */
        k--;
    t[k] = 0;
}
/* a message for a moment in the header; flags: how a screen reader gets it (0: the caller tells it) */
static void say_as(const char *a, const char *b, uint32_t flags)
{
    uint32_t i;
    char t[48];
    for (i = 0; i < 23u && a && a[i]; i++)
        ui.msg[0][i] = a[i];
    ui.msg[0][i] = 0;
    for (i = 0; i < 23u && b && b[i]; i++)
        ui.msg[1][i] = b[i];
    ui.msg[1][i] = 0;
    ui.msg_until = plat_ms() + 1300u;
    if (flags && ann_on()) {
        msg_text(a, b, t);
        ann_say(t, 0, flags);
        sp_said = 1;
    }
}
static void say(const char *a, const char *b) { say_as(a, b, ANN_NOW); }
/* something did not happen: a screen reader says it whole (what follows does not cut it off) */
static void warn(const char *a, const char *b) { say_as(a, b, ANN_NOW | ANN_KEEP); }
void ui_say(const char *a, const char *b) { say(a, b); }

static void itoa_u(uint32_t v, char *b)
{
    char t[12];
    int n = 0;
    do {
        t[n++] = (char)('0' + v % 10u);
        v /= 10u;
    } while (v);
    while (n)
        *b++ = t[--n];
    *b = 0;
}

/* SAVING: a flash erase stops everything for tens of ms, so every write goes through the volume's duck (en_duck). It fades, it writes, it comes back, and all
 * that without holding up the main loop; only the write itself does. SAVE works at any time; AUTOSAVE only in a rest, once nothing has been touched for
 * AUTOSAVE_QUIET, and a touch or MIDI's transport before the write cancels it. For the screen: sv.st says a save is under way, ui.save_t when SAVE last saved */
#define AUTOSAVE_QUIET 4000u
#define SAVE_LATE 400u                 /* the engine has not said zero by now: no render running, so nothing to cut */
#define SAVE_BACK 300u
#define SAVE_QUEUE 8u
#define Q_SEL 0x80u
enum { SV_IDLE, SV_FADE, SV_RETURN };
static struct {
    uint8_t st, manual;                /* manual: SAVE's. It reports and nothing cancels it; an autosave does neither */
    uint8_t ok;                        /* the write went well */
    uint8_t failed;                    /* the last write failed: no autosave until something changes (no breathing every few seconds) */
    uint8_t hushed;                    /* a panic, and no ui_init yet: nothing saves itself */
    uint8_t n, q[SAVE_QUEUE];          /* buttons pressed during it, in order, with Q_SEL for an EDIT that had SELECT down: played after it. A knob, a key or PLAY is not held up */
    uint32_t t;
} sv;

static void mark_dirty(void)
{
    ui.dirty = 1;
    sv.failed = 0;
}
int ui_dirty(void) { return ui.dirty != 0; }

static int pool_count(uint32_t p)
{
    int n = 0, i;
    for (i = 0; i < 12; i++)
        n += (int)(p >> i & 1u);
    return n;
}
static uint32_t pool_abs(void)                 /* the pool as absolute pitch classes */
{
    uint32_t p = proj.pool, r = (uint32_t)proj.par[P_ROOT];
    return ((p << r) | (p >> (12u - r))) & 0xFFFu;
}
static const char *mode_name(void)
{
    return proj.pool == EN_MODE_POOL[proj.par[P_MODE]] ? EN_MODE_NAME[proj.par[P_MODE]] : "Custom";
}

/* ------------------------------------------------------------ values --- */
static int knob_get(int k)
{
    if (k < P_NPARAMS)
        return proj.par[k];
    if (k == K_LEDS)
        return proj.leds;
    if (k == K_CPU)
        return (int)plat_cpu_pct();
    return 0;
}
static void knob_range(int k, int *lo, int *hi)
{
    if (k < P_NPARAMS) {
        *lo = en_param_info(k)->lo;
        *hi = en_param_info(k)->hi;
    } else {
        *lo = 0;
        *hi = k == K_LEDS ? 2 : k == K_CPU ? 100 : 0;
    }
}
static const char *knob_name(int k)
{
    if (k < P_NPARAMS)
        return en_param_info(k)->name;
    return k == K_LEDS ? "Lights" : k == K_CPU ? "CPU" : "";
}
static void knob_text(int k, char *b)
{
    int v = knob_get(k);
    if (k == P_MRATE && proj.par[P_MELMODE]) {   /* in Arp, Rate is its division */
        const int d = en_arp_steps(v);
        const char *n = d == 4 ? "1/4" : d == 2 ? "1/8" : "1/16";
        while ((*b++ = *n++))
            ;
    } else if (k < P_NPARAMS) {
        en_param_text(k, v, b);
    } else if (k == K_LEDS) {                    /* On: the keys, and the unlit buttons glow; Keys: no glow */
        const char *n = v == 2 ? "Keys" : v ? "On" : "Off";
        while ((*b++ = *n++))
            ;
    } else if (k == K_CPU) {
        itoa_u((uint32_t)v, b);
        while (*b)
            b++;
        *b++ = '%';
        *b = 0;
    } else {
        b[0] = 0;
    }
}

static void knob_set(int k, int v)
{
    int lo, hi;
    if (k == K_CPU || k == K_NONE)
        return;
    knob_range(k, &lo, &hi);
    v = v < lo ? lo : v > hi ? hi : v;
    if (v == knob_get(k))
        return;
    if (k < P_NPARAMS) {
        proj.par[k] = (int16_t)v;
        en_set(k, v);
    } else if (k == K_LEDS) {
        proj.leds = (uint8_t)v;
    }
    mark_dirty();
}

/* knob acceleration by speed (X0X's): a few clicks are one step each however quick, so small
 * corrections stay exact; a longer quick turn speeds up to 8 a detent, so half a turn sweeps 0-100.
 * Small ranges never accelerate. */
static uint8_t accel_off;              /* the host simulator's "spin": exact steps */
static int32_t accel(int role, int32_t s, int range)
{
    uint32_t now = plat_ms(), dt = now - ui.enc_t[role], a = (uint32_t)(s < 0 ? -s : s), m;
    ui.enc_t[role] = now;
    if (!a)
        return s;
    if (a > 1)
        dt /= a;                                      /* per detent */
    ui.enc_run[role] = (uint8_t)(dt < 60u ? (ui.enc_run[role] + a > 255u ? 255u : ui.enc_run[role] + a) : a);
    if (range <= 24 || accel_off || ui.enc_run[role] < 4u)
        return s;
    m = dt < 12u ? 8u : dt < 25u ? 5u : dt < 45u ? 3u : 1u;
    if (range < 100 && m > 3u)
        m = 3u;
    return s * (int32_t)m;
}

static void turn(int role, int k, int32_t e)
{
    int lo, hi;
    if (!e || k == K_NONE || k == K_CPU)
        return;
    knob_range(k, &lo, &hi);
    knob_set(k, knob_get(k) + accel(role, e, hi - lo));
}

/* ------------------------------------------------------------ the sounds --- */
/* SEL + a knob on a layer's page. Each detent is the next, round and round, as ALGORITHM's and PRESETS' are. SEL's other jobs: SEL + a button is on the press, so no turn is in it; SEL alone is the Degrees view,
 * and as with SEL + a layer's button, a turn says this hold is not that (ui.sel_layer) */
static const struct {
    uint8_t view, knob, par;
    const char *say, *spoken;
} SOUND_KNOB[] = {{V_KEYS, 0, P_KEYSND, "KEYS SOUND", ". Sound "}, {V_KEYS, 1, P_PADSND, "PAD SOUND", ", pad sound "}, {V_MOTIFS, 0, P_MELSND, "MELODY SOUND", ". Sound "},
                  {V_MOTIFS, 1, P_MELMODE, "MELODY MODE", ", mode "}, {V_MOTIFS, 2, P_ARPPAT, "ARP PATTERN", ", pattern "}, {V_MOTIFS, 3, P_ARPSKIP, "ARP SKIP", ", skip "}, {V_BASS, 0, P_BASSSND, "BASS SOUND", ". Sound "}};
#define NSOUND_KNOBS ((int)(sizeof SOUND_KNOB / sizeof SOUND_KNOB[0]))
static int sound_said(int i)                    /* (the page has no use for a pattern or a skip in Phrases, or a skip of 0: their knobs still set them) */
{
    const int p = SOUND_KNOB[i].par;
    return p == P_ARPSKIP ? proj.par[P_MELMODE] && proj.par[P_ARPSKIP] : p != P_ARPPAT || proj.par[P_MELMODE];
}
static int sound_turn(int knob, int32_t e)      /* 1: it was a sound's */
{
    int i;
    for (i = 0; i < NSOUND_KNOBS; i++)
        if (SOUND_KNOB[i].view == ui.view && SOUND_KNOB[i].knob == knob) {
            const int p = SOUND_KNOB[i].par, n = en_param_info(p)->hi + 1;
            char t[16];
            if (p == P_ARPSKIP)                  /* a quantity, not a list: to its ends and no further, faster for a long turn */
                turn(EN_K1 + knob, p, e);
            else
                knob_set(p, (proj.par[p] + (e > 0 ? 1 : -1) + n) % n);
            en_param_text(p, proj.par[p], t);
            if (p == P_MELSND)
                en_mel_hear();
            say(SOUND_KNOB[i].say, t);
            ui.sel_layer = 1u;
            ui.btn_used |= 1u << B_SEL;
            return 1;
        }
    return 0;
}

/* ------------------------------------------------------------- the pool --- */
static void toggle_note(int pc)                 /* a key: its note in or out of the scale */
{
    int rel = (pc - proj.par[P_ROOT] + 12) % 12;
    uint32_t p = proj.pool ^ (1u << rel);
    if (rel == 0) {
        warn("THE ROOT STAYS", "(SETUP)");
        return;
    }
    if (pool_count(p) < 3) {
        warn("3 NOTES", "AT LEAST");
        return;
    }
    proj.pool = (uint16_t)p;
    en_pool(p);
    say(p >> rel & 1u ? "ADDED" : "REMOVED", EN_NOTE_NAME[pc]);
    mark_dirty();
}

/* SEL + a key: chords on this note, or not. The mask's rule is the generator's: what it would not use, it is not offered */
static void toggle_degree(int pc)
{
    const int rel = (pc - proj.par[P_ROOT] + 12) % 12;
    const uint32_t m = (uint32_t)proj.mask ^ 1u << rel;
    if (!(proj.pool >> rel & 1u)) {
        warn("NOT IN", "THE SCALE");
        return;
    }
    if (!(m >> rel & 1u) && !en_mask_usable(proj.pool, m)) {
        warn("2 DEGREES", "AT LEAST");
        return;
    }
    proj.mask = (uint16_t)m;
    en_mask(m);
    say(m >> rel & 1u ? "CHORDS ON" : "NO CHORDS ON", EN_NOTE_NAME[pc]);
    mark_dirty();
}
/* what the eye and the ear get: the degrees in force. A pool that leaves too few has them all, until the note comes back */
static uint32_t degrees_shown(void) { return en_mask_effective(proj.pool, proj.mask); }
static int degree_off(int pc)                   /* a note of the scale that chords may not stand on (pc: absolute) */
{
    const int rel = (pc - proj.par[P_ROOT] + 12) % 12;
    return (int)(proj.pool >> rel & 1u) && !(degrees_shown() >> rel & 1u);
}

static void set_mode(int m)
{
    m = (m % EN_NMODES + EN_NMODES) % EN_NMODES;
    proj.par[P_MODE] = (int16_t)m;
    proj.pool = EN_MODE_POOL[m];
    en_set(P_MODE, m);
    en_pool(proj.pool);
    say("MODE", EN_MODE_NAME[m]);
    mark_dirty();
}

/* a new seed at each power-on and each new piece, from the crystal's counter (at boot it only differs by the jitter of the
 * start, so the master pot's reading goes in too). The simulator's is fixed at boot and moves by a fixed step with each piece, so that a scenario repeats and
 * can see a new one: "seed N" there */
#if defined PLAT_HOST && !defined PLAT_WEB
static uint32_t host_pieces;                    /* the simulator's: pieces changed to since it booted */
#endif
static uint32_t seed_now(void)
{
#if defined PLAT_HOST && !defined PLAT_WEB
    return EN_HARM_SEED + host_pieces * 1009u;
#else
    const uint32_t t = plat_ticks24();
    return (t ^ t >> 13) * 2654435761u + plat_master();
#endif
}

/* What the engine is owed. A piece is a whole queue's worth of commands, and the detents of a quick turn can come faster than the blocks that take them, so: the project now,
 * and the commands when they all fit, read from the project as it is by then. A piece that went by in the meantime never reaches the engine; the last one does. The same way back for
 * values the queue refused (en_dropped): they are all said again once there is room, and being after whatever is still queued, they are what stays. So the two cannot stay apart,
 * whatever the timing. With room, as there is but for a quick turn, it all goes at once */
enum { OWE_APPLY = 1, OWE_REGROW = 2, OWE_PIECE = 4 };
#define OWED_POSTS (P_NPARAMS + 6u)              /* project_apply's, the seed, the new key, the blip */
typedef char queue_holds_a_piece[OWED_POSTS <= EN_ROOM_MAX ? 1 : -1];
static uint8_t owed;
static void owed_tick(void)
{
    if (en_dropped())
        owed |= OWE_APPLY;
    if (!owed || en_room() < OWED_POSTS)
        return;
    project_apply();
    if (owed & OWE_PIECE)
        en_seed(seed_now());
    if (owed & (OWE_PIECE | OWE_REGROW))
        en_regrow();
    if (owed & OWE_PIECE)
        en_sys(EN_SYS_BLIP);
    owed = 0u;
}

static void set_piece(int i)                    /* the piece playing kept as it is; piece i as it was left */
{
    project_piece(i);
#if defined PLAT_HOST && !defined PLAT_WEB
    host_pieces++;
#endif
    say_as("PIECE", piece_name(proj.piece), 0);
    sp_piece();                                  /* a screen reader: the piece with its key and tempo */
    scr.card_t = plat_ms();
    scr.card_until = scr.card_t + PIECE_CARD_MS;
    mark_dirty();
    owed |= OWE_APPLY | OWE_PIECE;
    owed_tick();
}

/* ------------------------------------------------------------ buttons --- */
static void set_view(int v)
{
    ui.view = (uint8_t)v;
    ui.touched = -1;
    scr.card_until = plat_ms();                  /* a page's button: the page, now */
}

/* EDIT: the engine does it on the bar. A break needs drums on that bar, and a level turned up to hear them */
static void edit(int sel)
{
    if (!sel) {
        en_next();
        say("NEXT SECTION", en_playing ? "ON THE BAR" : "WHEN IT PLAYS");
    } else if (en_playing && en_drum_next >= 2u && proj.par[P_DRUMS] > 0 && !en_mute_drums) {
        en_break();
        say("BREAK", "ON THE BAR");
    } else {
        warn("NO DRUMS", "NO BREAK");
    }
}

/* SELECT + a page's button: its layer on or off, the page stays where it is. HOME: the arrangement. Setup has nothing to turn off */
static const struct {
    uint8_t btn, layer;
    const char *name;
} LAYER_BTN[EN_NLAYERS] = {{B_ENV, EN_L_KEYS, "KEYS"}, {B_ARP, EN_L_MEL, "MELODY"}, {B_LFO, EN_L_BASS, "BASS"}, {B_SEQ, EN_L_DRUMS, "DRUMS"}, {B_FX, EN_L_HATS, "HATS"}};
static int layer_off(int l)
{
    return l == EN_L_KEYS ? en_mute_keys : l == EN_L_MEL ? en_mute_mel : l == EN_L_BASS ? en_mute_bass : l == EN_L_DRUMS ? en_mute_drums : en_mute_hats;
}
static int sel_button(int b)                     /* 1: it was a page's */
{
    int v, i;
    for (i = 0; i < EN_NLAYERS; i++)
        if (LAYER_BTN[i].btn == b) {
            const int on = en_layer_press(LAYER_BTN[i].layer);
            if (on)
                scr.smile_until = plat_ms() + 1500u;
            if (on && proj.arrange == EN_AUTO && LAYER_BTN[i].layer == EN_L_DRUMS && proj.par[P_ENERGY] < 10)
                warn("DRUMS ON", "ENERGY UP");   /* (under 10 the arranger has no drums anywhere to go to) */
            else
                say(LAYER_BTN[i].name, on ? "ON" : "OFF");
            return 1;
        }
    if (b == B_HOME) {
        proj.arrange = proj.arrange == EN_AUTO ? EN_HANDS : EN_AUTO;
        en_arrange(proj.arrange);
        say(proj.arrange == EN_HANDS ? "HANDS" : "AUTO", 0);
        mark_dirty();
        return 1;
    }
    for (v = 0; v < NVIEWS; v++)
        if (VIEW_BTN[v] == b)
            return 1;
    return 0;
}

static void save_begin(int manual)
{
    if (sv.st == SV_IDLE) {
        sv.st = SV_FADE;
        sv.manual = (uint8_t)manual;
        sv.t = plat_ms();
        en_duck(1);
    } else if (sv.st == SV_FADE && manual) {
        sv.manual = 1u;                /* (a press seen between frames: nothing has cancelled it yet) now it is the button's */
    }
}

/* a button pressed during a save, or while those pressed before it are still going: 1 if it waits, in order; PLAY never does. An autosave's fade waits for nobody: a press cancels it (save_tick) */
static int save_holds(int b)
{
    if (b == B_PLAY)                   /* the transport does not wait, ever: the save goes on over it (what it says is the engine's: a duck, a write, the way back know nothing of it), and an autosave's fade is cancelled by the press, as by any */
        return 0;
    if (sv.st == SV_IDLE ? !sv.n : sv.st == SV_FADE && (!sv.manual || b == B_SAVE))
        return 0;
    if (b == B_SAVE && sv.st == SV_RETURN && !ui.dirty && sv.ok) {   /* written just now, and nothing since: what it asked for is done */
        sv.manual = 1u;
        say("SAVED", 0);
        ui.save_t = plat_ms();
        return 1;
    }
    if (sv.n < SAVE_QUEUE)
        sv.q[sv.n++] = (uint8_t)(b == B_EDIT && ui.edit_sel ? (uint32_t)b | Q_SEL : (uint32_t)b);
    return 1;
}

static void button_now(int b, int edit_sel)
{
    int v;
    if (b == B_GLO && ui.view == V_SETUP) {      /* GLO on Setup: the reading starts afresh */
        app_render_max_us = 0;
        app_sheds = 0;
    }
    for (v = 0; v < NVIEWS; v++)                 /* a page's button: its page */
        if (VIEW_BTN[v] == b) {
            set_view(v);
            en_sys(EN_SYS_BLIP);
            return;
        }
    switch (b) {
    case B_EDIT:
        edit(edit_sel);
        break;
    case B_PLAY:
        sp_tp.playing = !en_playing;
        sp_tp.play_t = plat_ms();
        if (en_playing) {
            en_play(0);
            say("RESTING", 0);
        } else {
            en_play(1);
            say("PLAYING", 0);
        }
        break;
    case B_REC:
        say("HOLD", en_held ? "OFF" : "ON");
        en_hold(!en_held);                       /* the engine takes it at its next block */
        break;
    case B_OCTDN: case B_OCTUP:
        knob_set(P_MOCT, proj.par[P_MOCT] + (b == B_OCTUP ? 1 : -1));
        {
            char t[8];
            en_param_text(P_MOCT, proj.par[P_MOCT], t);
            say("MELODY OCTAVE", t);
        }
        break;
    case B_SAVE:
        save_begin(1);
        break;
    default: break;
    }
}

static void button(int b)
{
    if (!save_holds(b))
        button_now(b, ui.edit_sel);
}

/* MIDI in (USB and the TRS jack): channel 1 notes add or remove their note from the scale; clock and
 * start / stop drive the grid */
static void midi_in(void)
{
    uint32_t pkt;
    while (plat_midi_in(&pkt, 0)) {
        uint32_t st = (pkt >> 8) & 0xFFu, n = (pkt >> 16) & 0x7Fu, v = pkt >> 24;
        if (st == 0xF8u || st == 0xFAu || st == 0xFBu || st == 0xFCu) {
            en_clock(st == 0xF8u ? EN_CLK_TICK : st == 0xFAu ? EN_CLK_START : st == 0xFBu ? EN_CLK_CONTINUE : EN_CLK_STOP);
            if (st != 0xF8u)
                ui.act_t = plat_ms();          /* a touch, and seen before en_playing can say so: an autosave's fade stops here, before the write */
        } else if (st == 0x90u && v) {
            toggle_note((int)(n % 12u));
            ui.act_t = plat_ms();
        }
    }
}

/* HELP CARDS (X0X's): hold one button alone for HELP_HOLD and a card says what it does. A key, a
 * turn or another button puts it away; letting go after its card does nothing else. PLAY acts on
 * its press, and SEL has no job here: neither has a card. */
#define HELP_HOLD 900u
#define RESET_HOLD 1500u
static void help_tick(uint32_t btn, uint32_t keys)
{
    uint32_t now = plat_ms();
    int i;
    if (ui.help) {
        uint32_t m = 1u << (ui.help - 1);
        if (btn != m || keys || (int32_t)(ui.turn_t - ui.down_t[ui.help - 1]) > 0)
            ui.help = 0;
        return;
    }
    if (!btn || (btn & (btn - 1u)) || keys)
        return;
    for (i = 0; i < NB; i++)
        if (btn == 1u << i && i != B_PLAY && i != B_SEL && !(ui.btn_used & btn) && now - ui.down_t[i] >= HELP_HOLD &&
            (int32_t)(ui.turn_t - ui.down_t[i]) <= 0) {
            ui.help = (uint8_t)(i + 1);
            ui.btn_used |= btn;                  /* its release now does nothing */
        }
}

static void input(void)
{
    uint32_t btn = plat_buttons(), keys = plat_keys(), ch, i;
    int32_t e;
    owed_tick();
    ch = btn ^ ui.btn;
    ui.btn = btn;
    for (i = 0; i < NB; i++) {
        uint32_t m = 1u << i;
        if (!(ch & m))
            continue;
        if (btn & m) {
            ui.btn_used &= ~m;
            ui.down_t[i] = plat_ms();
            if (i == B_SEL)
                ui.sel_layer = 0u;
            else if (btn >> B_SEL & 1u)
                ui.sel_layer = 1u;
            if (i == B_SAVE)
                ui.reset_done = 0;
            if (i == B_EDIT) {
                ui.edit_sel = (uint8_t)(btn >> B_SEL & 1u);
                if (ui.edit_sel)
                    ui.btn_used |= 1u << B_SEL;
            }
            if (btn >> B_SEL & 1u && (int)i != B_SEL && sel_button((int)i))
                ui.btn_used |= m | 1u << B_SEL;  /* on the press; nothing on its release, no page, no card */
            if (i == B_PLAY)                   /* transport on the press */
                button((int)i);
        } else if (!(ui.btn_used & m) && i != B_PLAY) {
            button((int)i);
        }
    }
    ch = keys ^ ui.keys;
    ui.keys = keys;
    for (i = 0; i < NKEYS; i++)
        if ((ch & keys) >> i & 1u) {             /* key 0 is F3 */
            if (btn >> B_SEL & 1u) {
                toggle_degree((int)((5u + i) % 12u));
                ui.btn_used |= 1u << B_SEL;
            } else {
                toggle_note((int)((5u + i) % 12u));
            }
        }
    if (btn || keys || ch)
        ui.act_t = plat_ms();
    midi_in();
    if ((e = plat_enc(EN_SELECT)) != 0) {        /* SELECT: the pages; with HOME held, the tempo */
        if (btn >> B_HOME & 1u) {
            turn(EN_SELECT, P_TEMPO, e);
            ui.btn_used |= 1u << B_HOME;
            if (ui.view == V_GARDEN) {           /* Tempo is the Garden's first knob */
                ui.touched = 0;
                ui.touch_until = plat_ms() + 1200u;
            } else {
                char t[8];
                itoa_u((uint32_t)proj.par[P_TEMPO], t);
                say("TEMPO", t);
            }
        } else {
            int v = (int)ui.view + (e > 0 ? 1 : -1);
            if (v >= 0 && v < NVIEWS)
                set_view(v);
        }
        ui.act_t = ui.turn_t = plat_ms();
    }
    if ((e = plat_enc(EN_ALGO)) != 0) {
        set_mode(proj.par[P_MODE] + (e > 0 ? 1 : -1));
        ui.act_t = ui.turn_t = plat_ms();
    }
    if ((e = plat_enc(EN_PRESET)) != 0) {
        set_piece(proj.piece + (e > 0 ? 1 : -1));
        ui.act_t = ui.turn_t = plat_ms();
    }
    if ((btn >> B_SAVE & 1u) && !ui.reset_done && plat_ms() - ui.down_t[B_SAVE] > RESET_HOLD) {
        ui.reset_done = 1;                     /* held: the piece back to its recipe (not saved) */
        ui.btn_used |= 1u << B_SAVE;
        ui.help = 0;
        project_piece_reset();
        owed |= OWE_APPLY | OWE_REGROW;
        owed_tick();
        say("RESET", piece_name(proj.piece));
        mark_dirty();
    }
    for (i = 0; i < 4; i++)
        if ((e = plat_enc(EN_K1 + (int)i)) != 0) {
            if (!(btn >> B_SEL & 1u) || !sound_turn((int)i, e)) {
                turn(EN_K1 + (int)i, VIEW_KNOB[ui.view][i], e);
                ui.touched = (int8_t)i;
                ui.touch_until = plat_ms() + 1200u;
            }
            ui.act_t = ui.turn_t = plat_ms();
        }
    help_tick(btn, keys);
}

/* ------------------------------------------------------------- screen --- */
/* The owner's pictures under the panels: one painter (paint: the picture, then everything on it, cut to a rectangle), and what changes goes through it: a whole
 * picture by bands over several frames, the rest by the small rectangle that changed (el_set). The render is in the audio interrupt and the code is read from
 * the same flash as the pictures, so what is read from it is kept down:
 *   - WIPE_ROWS rows a frame at most come from the flash, whatever is going on, so no frame reads more than a band's worth; a whole picture takes five frames;
 *   - a new picture only when the scene changes (scene_pick): a page, a break, a card;
 *   - the rows under the header and the knobs and the rectangle under the meters are kept once decoded, panel and all, in the part of the canvas no band uses
 *     (BACK_*): no new buffer, and a knob, the header or a meter moving reads nothing from the flash. They last until the next picture. */
#define HDR_H 28
#define PANEL_Y 178                    /* the knobs' panel, as in the approved previews: its line, the rows under it */
#define PANEL_H (240 - PANEL_Y)
#define WIPE_ROWS 48
#define PX_FRAME (240u * WIPE_ROWS)    /* what a frame draws at most */
#define MTR_X 206
#define MTR_Y 146
#define MTR_W 30
#define MTR_H 30
#define BACK_HDR (cv_px + 240u * WIPE_ROWS)
#define BACK_KNB (BACK_HDR + 240u * HDR_H)
#define BACK_MTR (BACK_KNB + 240u * PANEL_H)
typedef char canvas_holds_it[240u * WIPE_ROWS + 240u * HDR_H + 240u * PANEL_H + MTR_W * MTR_H <= CV_MAX ? 1 : -1];

/* how long things stay, ms */
#define TITLE_MS 2600u
#define SAVE_ANIM 1600u
#define DEG_HOLD 250u                  /* SEL held this long, alone: not a SELECT + button on its way */
#define BLINK_BARS 8u                  /* a blink, at most every so many bars: 11 s at 170 */
#define EYE_STEP 80u                   /* the half-open cell between two others */
#define OPEN_MS 8000u                  /* a new section: eyes open so long */
#define SMILE_MS 1500u
#define FEATHER_MS 3000u
#define STAR_MS 640u

#define K_INK RGB(246, 249, 255)
#define K_SOFT RGB(190, 206, 248)
#define K_MID RGB(120, 144, 226)
#define K_TRACK RGB(72, 94, 194)
#define K_DARK RGB(8, 10, 44)
#define K_GLASS RGB(10, 14, 60)
#define K_WHITE RGB(255, 255, 255)
#define K_HOT RGB(44, 58, 146)
#define K_ICE RGB(198, 235, 255)
#define K_OFF RGB(96, 118, 214)

static uint32_t hash(uint32_t h, uint32_t v) { return (h ^ v) * 16777619u; }
static uint32_t hash_s(uint32_t h, const char *s)
{
    while (*s)
        h = hash(h, (uint8_t)*s++);
    return h;
}

/* ---- the scene: what is on the screen, the first of these that holds */
static int deg_shown(uint32_t now)     /* SEL held, alone, and no layer's button has gone with it */
{
    return (ui.btn >> B_SEL & 1u) && !(ui.btn & ~(1u << B_SEL)) && !ui.sel_layer && now - ui.down_t[B_SEL] >= DEG_HOLD;
}
static int save_shown(uint32_t now)    /* SAVE's, not an autosave's; a failed one says so in the header instead */
{
    if (sv.manual && (sv.st == SV_FADE || (sv.st == SV_RETURN && sv.ok)))
        return 1;
    return ui.save_t && now - ui.save_t < SAVE_ANIM;
}
/* a break the player hears: resting it is only where the song stopped; with the drums muted or at level 0 there is no break to see (edit's own conditions for asking for one) */
static int break_heard(void) { return en_break_bars && en_playing && proj.par[P_DRUMS] > 0 && !en_mute_drums; }
static int scene_pick(uint32_t now)
{
    if (ui.help)
        return SC_HELP;
    if (deg_shown(now))
        return SC_DEG;
    if ((int32_t)(scr.title_until - now) > 0 && ui.act_t == scr.act_boot)
        return SC_TITLE;
    if (save_shown(now))
        return SC_SAVE;
    if ((int32_t)(scr.card_until - now) > 0 && (int32_t)(ui.turn_t - scr.card_t) <= 0)
        return SC_CARD;
    if (break_heard() && now >= ui.touch_until)
        return SC_BREAK;
    if (ui.view != V_GARDEN)
        return SC_PAGE;
    return !en_playing ? SC_REST : en_held ? SC_HOLD : SC_HOME;
}
static int scene_art(int sc)
{
    return sc == SC_TITLE || sc == SC_CARD ? ART_TITLE : sc == SC_BREAK ? ART_BREAK : sc == SC_PAGE ? ART_KEYS + ui.view - V_KEYS : ART_FACE;
}
static int scene_look(int sc)
{
    return sc == SC_REST || sc == SC_DEG ? LOOK_DIM3 : sc == SC_HELP ? LOOK_DIM2 : sc == SC_HOLD ? LOOK_HOLD : LOOK_NORMAL;
}
static int has_header(int sc) { return sc == SC_HOME || sc == SC_PAGE || sc == SC_HOLD || sc == SC_DEG; }
static int has_knobs(int sc) { return sc == SC_HOME || sc == SC_PAGE || sc == SC_HOLD || sc == SC_REST; }

/* ---- glass: the picture through a dark pane. keep: how much of the picture, of 32 */
static void glass_px(uint16_t *q, int32_t n, uint32_t keep)
{
    const uint32_t d = ((uint32_t)K_GLASS | (uint32_t)K_GLASS << 16) & 0x07E0F81Fu, dk = d * (32u - keep);
    int32_t i;
    for (i = 0; i < n; i++) {
        uint32_t c = swap16(q[i]);
        c = (c | c << 16) & 0x07E0F81Fu;
        c = ((c * keep + dk) >> 5) & 0x07E0F81Fu;
        q[i] = swap16(c | c >> 16);
    }
}
static int32_t round_in(int32_t r, int32_t j)   /* a rounded corner's inset, j rows from the edge */
{
    int32_t d = 0;
    if (j >= r)
        return 0;
    while (d < r && (r - d) * (r - d) + (r - j) * (r - j) > r * r)
        d++;
    return d;
}
/* the row's pane in a scene's picture: it is part of what is kept under the header and the knobs */
static int base_pane(int sc, int32_t y, int32_t *x0, int32_t *x1, uint32_t *keep)
{
    if (has_header(sc) && y >= 3 && y < 26) {
        const int32_t j = y - 3 < 25 - y ? y - 3 : 25 - y, d = round_in(7, j);
        *x0 = 3 + d;
        *x1 = 237 - d;
        *keep = 10u;
        return 1;
    }
    if ((has_knobs(sc) && y >= PANEL_Y) || (sc == SC_CARD && y >= 158) || (sc == SC_SAVE && y >= 184 && y < 225)) {
        *x0 = 0;
        *x1 = 240;
        *keep = sc == SC_SAVE ? 6u : sc == SC_CARD ? 11u : 7u;
        return 1;
    }
    return 0;
}

/* ---- the painter. The canvas is cut to the rectangle and drawing is in the screen's coordinates: y through cv_oy, x through scr.sx (the canvas has no such thing) */
#define SX (scr.sx)
static int in_view(int32_t x, int32_t y, int32_t w, int32_t h)
{
    return x + w > SX && x < SX + (int32_t)cv_w && y + h > -cv_oy && y < -cv_oy + (int32_t)cv_h;
}
static void rect(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t c) { cv_rect(x - SX, y, w, h, c); }
static int32_t text(int32_t x, int32_t y, const felucca_font_t *f, const char *s, uint16_t c)
{
    const int32_t w = text_w(f, s);
    if (in_view(x - f->pad, y, w + 2 * f->pad, f->h))
        cv_text(x - SX, y, f, s, c);
    return x + w;
}
static void text_c(int32_t cx, int32_t y, const felucca_font_t *f, const char *s, uint16_t c) { text(cx - text_w(f, s) / 2, y, f, s, c); }
static void text_r(int32_t rx, int32_t y, const felucca_font_t *f, const char *s, uint16_t c) { text(rx - text_w(f, s), y, f, s, c); }

/* anti-aliased drawing (X0X's): a pixel blended over the canvas, a disc, and a knob's ring */
static void px_blend(int32_t x, int32_t y, uint16_t c, uint32_t a)
{
    uint16_t *q;
    uint32_t bg;
    y += cv_oy;
    if ((uint32_t)x >= cv_w || y < cv_y0 || y >= cv_y1 || !a)
        return;
    q = &cv_px[(uint32_t)y * cv_w + (uint32_t)x];
    if (a >= 16u) {
        *q = swap16(c);
        return;
    }
    bg = swap16(*q);
    *q = swap16((uint32_t)(((((c >> 11) * a + (bg >> 11) * (16u - a)) >> 4) << 11) |
                           (((((c >> 5) & 63u) * a + ((bg >> 5) & 63u) * (16u - a)) >> 4) << 5) |
                           (((c & 31u) * a + (bg & 31u) * (16u - a)) >> 4)));
}
static void disc(int32_t cx, int32_t cy, float r, uint16_t c)   /* (screen coordinates) */
{
    const int32_t R = (int32_t)r + 2;
    int32_t x, y;
    if (!in_view(cx - R, cy - R, 2 * R + 1, 2 * R + 1))
        return;
    for (y = -R; y <= R; y++)
        for (x = -R; x <= R; x++) {
            const float d = fm_sqrtf((float)(x * x + y * y));
            px_blend(cx + x - SX, cy + y, c, (uint32_t)(fm_clampf(r - d + 0.5f, 0.0f, 1.0f) * 16.0f + 0.5f));
        }
}
static void circle(int32_t cx, int32_t cy, float r, float th, uint16_t c, uint32_t a)   /* a circle's line */
{
    const int32_t R = (int32_t)r + 2;
    int32_t x, y;
    if (!in_view(cx - R, cy - R, 2 * R + 1, 2 * R + 1))
        return;
    for (y = -R; y <= R; y++)
        for (x = -R; x <= R; x++) {
            const float d = fm_sqrtf((float)(x * x + y * y));
            const float k = fm_clampf(r - d + 0.5f, 0.0f, 1.0f) * fm_clampf(d - (r - th) + 0.5f, 0.0f, 1.0f);
            px_blend(cx + x - SX, cy + y, c, (uint32_t)(k * (float)a + 0.5f));
        }
}
/* a pane with rounded corners on the picture, a thin line round it, and a lighter one at its top */
static void pane(int32_t x, int32_t y, int32_t w, int32_t h, int32_t r, uint32_t keep, int in_base)
{
    int32_t j, i;
    if (!in_view(x, y, w, h))
        return;
    if (!in_base)
        for (j = 0; j < h; j++) {
            const int32_t e = j < h - 1 - j ? j : h - 1 - j, d = round_in(r, e), ry = y + j + cv_oy;
            int32_t a = x + d - SX, b = x + w - d - SX;
            if (ry < 0 || ry >= (int32_t)cv_h)
                continue;
            a = a < 0 ? 0 : a;
            b = b > (int32_t)cv_w ? (int32_t)cv_w : b;
            if (b > a)
                glass_px(cv_px + (uint32_t)ry * cv_w + (uint32_t)a, b - a, keep);
        }
    for (j = 0; j < h; j++) {
        const int32_t e = j < h - 1 - j ? j : h - 1 - j, d = round_in(r, e), d1 = e ? round_in(r, e - 1) : w / 2;
        for (i = d; i <= d1 && i < w / 2; i++) {         /* the sides, and across where a row moves in (the top and the bottom: all of it) */
            px_blend(x + i - SX, y + j, K_SOFT, 9u);
            px_blend(x + w - 1 - i - SX, y + j, K_SOFT, 9u);
        }
    }
    for (i = r; i < w - r; i++)
        px_blend(x + i - SX, y + 1, K_WHITE, 9u);
}

/* atan2 in turns, 0..1 from +x going clockwise on screen (y down) */
static float turns(float y, float x)
{
    float ax = fm_fabsf(x), ay = fm_fabsf(y), a = ax < ay ? ax / (ay + 1e-9f) : ay / (ax + 1e-9f), s = a * a, r;
    r = ((-0.0464964749f * s + 0.15931422f) * s - 0.327622764f) * s * a + a;   /* atan on [0, 1] */
    if (ay > ax)
        r = 1.57079637f - r;
    if (x < 0.0f)
        r = FM_PI - r;
    if (y < 0.0f)
        r = FM_TWO_PI - r;
    return r * (1.0f / FM_TWO_PI);
}

/* a knob: a 270-degree track from 7:30 to 4:30, the value filled from the start (or from the top, for a centred value) */
static void arc(int32_t cx, int32_t cy, int32_t r, float v, uint16_t track, uint16_t fill, int bipolar)
{
    const float th = 3.0f, a0 = 0.375f, sweep = 0.75f;
    float ro = (float)r, ri = ro - th, lo, hi;
    int32_t x, y;
    if (!in_view(cx - r - 1, cy - r - 1, 2 * r + 3, 2 * r + 3))
        return;
    v = fm_clampf(v, 0.0f, 1.0f);
    lo = bipolar ? (v < 0.5f ? v : 0.5f) : 0.0f;
    hi = bipolar ? (v < 0.5f ? 0.5f : v) : v;
    for (y = -r - 1; y <= r + 1; y++)
        for (x = -r - 1; x <= r + 1; x++) {
            float dx = (float)x, dy = (float)y, d = fm_sqrtf(dx * dx + dy * dy), t, a;
            a = fm_clampf(ro + 0.5f - d, 0.0f, 1.0f) * fm_clampf(d - ri + 0.5f, 0.0f, 1.0f);
            if (a <= 0.0f)
                continue;
            t = turns(dy, dx) - a0;
            if (t < 0.0f)
                t += 1.0f;
            t /= sweep;
            if (t > 1.0f)
                continue;
            px_blend(cx + x - SX, cy + y, (t >= lo && t <= hi) ? fill : track, (uint32_t)(a * 16.0f + 0.5f));
        }
}

static void caps(char *d, const char *s, int n)  /* s in capitals, at most n - 1 characters */
{
    int i;
    for (i = 0; i < n - 1 && s[i]; i++)
        d[i] = (char)(s[i] >= 'a' && s[i] <= 'z' ? s[i] - 32 : s[i]);
    d[i] = 0;
}

#define FEATHER_H 16
/* a star of four points, as the pictures have them */
static void star(int32_t x, int32_t y, int32_t s, uint32_t a)
{
    const int32_t t = s >= 6 ? s / 3 : 1;
    int32_t i, j;
    if (s <= 0 || !in_view(x - s, y - s, 2 * s + 1, 2 * s + 1))
        return;
    for (j = -s; j <= s; j++)
        for (i = -s; i <= s; i++) {
            const int32_t ai = i < 0 ? -i : i, aj = j < 0 ? -j : j;
            if (ai * s + aj * t <= t * s || aj * s + ai * t <= t * s)
                px_blend(x + i - SX, y + j, K_WHITE, a);
        }
}
/* a feather falling: a quill, and barbs that are longer at its middle */
static void feather(int32_t x, int32_t y, int lean)
{
    int32_t t, i;
    if (!in_view(x - 8, y, 18, FEATHER_H))
        return;
    for (t = 0; t < FEATHER_H; t++) {
        const int32_t qx = x + (lean * t) / FEATHER_H, m = t < FEATHER_H / 2 ? t : FEATHER_H - 1 - t, w = t < 3 ? 0 : (m + 2) / 2;
        for (i = -w - 1; i <= w + 1; i++)        /* dark first, a pixel wider: it must show on the white of her face too */
            px_blend(qx + i - SX, y + t - (i < 0 ? -i : i) / 2, K_TRACK, 10u);
        for (i = -w; i <= w; i++)
            px_blend(qx + i - SX, y + t - (i < 0 ? -i : i) / 2, K_INK, i == 0 ? 15u : (t & 1) ? 9u : 13u);
    }
}

/* the battery (plat_battery): 0 (low) .. 3 bars, 4 a USB host powers the FM-1, -1 not read yet. The
 * simulator's "battery N" (host.c) stands in for it */
#ifdef PLAT_HOST
static int8_t batt_host = -2;                  /* -2: plat_battery's */
static int batt(void) { return batt_host > -2 ? batt_host : plat_battery(); }
#else
#define batt() plat_battery()
#endif
#define K_LOW RGB(196, 84, 52)                 /* the battery at one bar or none: the one warm thing on the screen */
#define BATT_W 20                              /* the body 18 wide and its nub */
static void draw_batt(int32_t x, int b)
{
    static const int8_t BOLT[7][2] = {{10, 2}, {9, 2}, {8, 2}, {7, 5}, {9, 2}, {8, 2}, {7, 2}};
    uint16_t c = b <= 1 ? K_LOW : K_SOFT;
    int i;
    rect(x + 1, 9, 16, 1, c);
    rect(x + 1, 18, 16, 1, c);
    rect(x, 10, 1, 8, c);
    rect(x + 17, 10, 1, 8, c);
    rect(x + 18, 12, 2, 4, c);
    if (b == 4) {
        for (i = 0; i < 7; i++)
            rect(x + BOLT[i][0], 10 + i, BOLT[i][1], 1, K_INK);
        return;
    }
    for (i = 0; i < b && i < 3; i++)
        rect(x + 2 + i * 5, 11, 4, 6, b <= 1 ? K_LOW : K_INK);
}
static const char *batt_words(int b)
{
    static const char *const W[5] = {"Battery low", "Battery one third", "Battery two thirds", "Battery full",
                                     "On USB power"};
    return b >= 0 && b <= 4 ? W[b] : 0;
}

static const char *chord_now(char *nm)          /* the chord's name, as the screen shows it ("Em7") */
{
    int pcs[EN_NPAD + 1], k;
    pcs[0] = en_chord_root;
    for (k = 0; k < EN_NPAD; k++)
        pcs[k + 1] = en_chord[k] % 12;
    return en_chord_name(pcs, EN_NPAD + 1, nm);
}
static uint32_t chord_sig(void)
{
    char nm[16];
    return hash_s(2166136261u, chord_now(nm));
}

/* the header: the page or the piece and what goes with it, on a pane; on the right the battery, what is unsaved, the tempo (a play triangle while resting).
 * Setup: the audio load instead: smoothed, worst block %, missed, shed. A message takes its place for a moment, in any scene */
static int msg_up(void) { return scr.now < ui.msg_until && scr.scene != SC_CARD && scr.scene != SC_SAVE; }   /* (those two say it whole themselves) */
static void setup_read(char *c)
{
    uint32_t k = 4;
    c[0] = 'C', c[1] = 'P', c[2] = 'U', c[3] = ' ';
    itoa_u(scr.r_cpu, c + k);
    while (c[k]) k++;
    c[k++] = ' ';
    c[k++] = 'M';
    itoa_u(scr.r_max * 100u / (APP_BLOCK * 1000000u / 44100u), c + k);
    while (c[k]) k++;
    c[k++] = ' ';
    c[k++] = 'X';
    itoa_u(scr.r_x, c + k);
    while (c[k]) k++;
    c[k++] = ' ';
    c[k++] = 'S';
    itoa_u(scr.r_s, c + k);
}
/* what the header says of a page's sounds: Keys has two, the keys' and the pad's. 0: none */
#define HEADER_SOUND_ROOM(view) (231 - BATT_W - 6 - 9 - 22 - 6 - 10 - (9 + text_w(&FONT_B, VIEW_NAME[view]) + 5))   /* with the tempo, a dot, the battery and a rest's mark on its right */
static int page_sounds(int view, char *b, int leave)
{
    int i, n = 0;
    for (i = 0; i < NSOUND_KNOBS; i++)
        if (SOUND_KNOB[i].view == view && sound_said(i) && SOUND_KNOB[i].par != leave) {
            char t[16];
            int k = 0;
            en_param_text(SOUND_KNOB[i].par, proj.par[SOUND_KNOB[i].par], t);
            if (n)
                b[n++] = '/';
            if (SOUND_KNOB[i].par == P_ARPSKIP)
                b[n++] = 'S';                    /* (a bare number is not a word for what it is) */
            while (t[k])
                b[n++] = t[k++];
        }
    b[n] = 0;
    return n;
}
static int page_sound(int view, char *b)         /* in Arp the mode and the pattern are what the page can not say otherwise: a sound's long name gives way to them (it says itself when turned) */
{
    int n = page_sounds(view, b, -1);
    if (n && text_w(&FONT_XS, b) > HEADER_SOUND_ROOM(view))
        n = page_sounds(view, b, P_MELSND);
    return n;
}
static const char *header_word(int sc)
{
    return sc == SC_DEG ? "Degrees" : sc == SC_HOLD ? "HOLD" : sc == SC_PAGE ? VIEW_NAME[ui.view] : piece_name(proj.piece);
}
static void draw_header(int sc)
{
    char t[24], c[32];
    int32_t x = 231, xl;
    if (!in_view(0, 0, 240, HDR_H))
        return;
    if (msg_up()) {
        pane(3, 3, 234, 23, 7, 10u, has_header(sc));
        xl = text(9, 6, &FONT_B, ui.msg[0], K_INK);
        text(xl + 8, 6, &FONT_B, ui.msg[1], K_SOFT);
        return;
    }
    if (!has_header(sc))
        return;
    pane(3, 3, 234, 23, 7, 10u, 1);
    xl = text(9, 6, &FONT_B, header_word(sc), K_INK) + 5;
    if (batt() >= 0) {
        x -= BATT_W;
        draw_batt(x, batt());
        x -= 6;
    }
    if (ui.dirty) {
        disc(x - 3, 14, 2.0f, K_SOFT);
        x -= 9;
    }
    if (sc != SC_DEG) {
        if (en_ext) {
            t[0] = 'E', t[1] = 'X', t[2] = 'T', t[3] = 0;
        } else {
            itoa_u((uint32_t)proj.par[P_TEMPO], t);
        }
        x -= text_w(&FONT_B, t);
        text(x, 6, &FONT_B, t, en_playing ? K_INK : K_SOFT);
        x -= 6;
        if (!en_playing) {
            int32_t j;
            for (j = 0; j < 5; j++)
                rect(x - 7 + j, 10 + j, 1, 9 - 2 * j, K_SOFT);
            x -= 10;
        }
    }
    if (sc == SC_DEG) {
        text(xl, 8, &FONT_XS, "press a key", K_SOFT);
    } else if (sc == SC_PAGE && ui.view == V_SETUP) {
        setup_read(c);
        text(xl, 8, &FONT_XS, c, K_SOFT);
    } else if (sc == SC_HOME) {
        if (xl + text_w(&FONT_XS, EN_NOTE_NAME[proj.par[P_ROOT]]) <= x) {
            xl = text(xl, 8, &FONT_XS, EN_NOTE_NAME[proj.par[P_ROOT]], K_SOFT);
            if (xl + 3 + text_w(&FONT_XS, mode_name()) <= x)
                text(xl + 3, 8, &FONT_XS, mode_name(), K_SOFT);
        }
    } else {
        char nm[16], snd[24];
        if (sc == SC_PAGE && page_sound(ui.view, snd) && xl + text_w(&FONT_XS, snd) <= x)
            xl = text(xl, 8, &FONT_XS, snd, K_INK) + 6;          /* (the page's sound, then the chord in its usual colour) */
        chord_now(nm);
        if (xl + text_w(&FONT_XS, nm) <= x)
            text(xl, 8, &FONT_XS, nm, en_held ? K_ICE : K_SOFT);   /* (a page, held: the chord in ice) */
    }
}
static uint32_t header_sig(int sc)
{
    uint32_t h = hash(2166136261u, (uint32_t)sc);
    if (msg_up())
        return hash_s(hash_s(hash(h, 1u), ui.msg[0]), ui.msg[1]);
    if (!has_header(sc))
        return 0u;
    h = hash(h, proj.piece | (uint32_t)proj.par[P_ROOT] << 8 | (uint32_t)proj.pool << 12);
    h = hash(h, en_playing | ui.dirty << 4 | (uint32_t)en_ext << 6 | (uint32_t)proj.par[P_MODE] << 8);
    h = hash(h, (uint32_t)proj.par[P_TEMPO] | (uint32_t)(batt() + 1) << 24);
    if (sc == SC_PAGE && ui.view == V_SETUP)
        h = hash(hash(h, scr.r_cpu | scr.r_max << 8), scr.r_x | scr.r_s << 16);
    else if (sc == SC_PAGE || sc == SC_HOLD)
        h = hash(hash(h, chord_sig()), en_held | (uint32_t)proj.par[P_KEYSND] << 1 | (uint32_t)proj.par[P_PADSND] << 3 | (uint32_t)proj.par[P_BASSSND] << 5 | (uint32_t)proj.par[P_MELSND] << 7 | (uint32_t)proj.par[P_MELMODE] << 10 | (uint32_t)proj.par[P_ARPPAT] << 11 | (uint32_t)proj.par[P_ARPSKIP] << 14);
    return h;
}

/* the ring: the twelve notes around the face, the root at the top. A chord's note is big and white, the scale's a smaller blue bead, what is out of the scale a
 * speck; a note of the scale that chords may not stand on is hollow; the root has a line round it */
static const int16_t RING_X[12] = {122, 175, 214, 228, 214, 175, 122, 69, 30, 16, 30, 69};
static const int16_t RING_Y[12] = {36, 45, 69, 102, 135, 159, 168, 159, 135, 102, 69, 45};
static const int16_t DEG_X[12] = {120, 166, 200, 212, 200, 166, 120, 74, 40, 28, 40, 74};
static const int16_t DEG_Y[12] = {36, 48, 80, 124, 168, 200, 212, 200, 168, 124, 80, 48};
#define BEAD_R 11                      /* the most a bead takes around its centre */
#define DEG_R 14

static uint32_t chord_pcs(void)
{
    uint32_t chord = 0;
    int i;
    for (i = 0; i < EN_NPAD; i++)
        chord |= 1u << (en_chord[i] % 12u);
    return chord;
}
/* note i above the root, as a bead shows it: bit 0 in the scale, 1 a chord's, 2 chords stand on it not, 3 the root. Its size and look follow */
static uint32_t bead_state(int i, uint32_t chord)
{
    const int pc = (proj.par[P_ROOT] + i) % 12;
    return (proj.pool >> i & 1u) | (uint32_t)(chord >> pc & 1u) << 1 | (uint32_t)degree_off(pc) << 2 | (uint32_t)(i == 0) << 3;
}
static void draw_bead(int i, uint32_t st)
{
    const int32_t x = RING_X[i], y = RING_Y[i];
    if (!in_view(x - BEAD_R, y - BEAD_R, 2 * BEAD_R + 1, 2 * BEAD_R + 1))
        return;
    if (!(st & 1u)) {
        disc(x, y, 3.0f, K_DARK);
        disc(x, y, 1.0f, K_SOFT);
    } else if (st & 4u) {                      /* no chords on it: hollow */
        disc(x, y, 6.0f, K_DARK);
        circle(x, y, 5.0f, 1.0f, K_MID, 16u);
    } else if (st & 2u) {
        disc(x, y, 8.0f, K_DARK);
        disc(x, y, 6.0f, K_INK);
        disc(x - 2, y - 2, 2.0f, K_WHITE);
    } else {
        disc(x, y, 6.0f, K_DARK);
        disc(x, y, 4.0f, K_MID);
    }
    if (st & 8u)
        circle(x, y, 10.0f, 1.0f, K_INK, 16u);
}

/* what is playing, and how: top left the chord and the section (Auto; Hands stays on one: its style); bottom left the mode and the layers, each letter lit when it plays */
#define CHORD_X 3
#define CHORD_Y 29
#define CHORD_W 60
#define CHORD_H 34
#define LAYERS_X 3
#define LAYERS_Y 146
#define LAYERS_W 56
#define LAYERS_H 30
static void draw_chord(void)
{
    char nm[16];
    if (!in_view(CHORD_X, CHORD_Y, CHORD_W, CHORD_H))
        return;
    pane(CHORD_X, CHORD_Y, CHORD_W, CHORD_H, 5, 8u, 0);
    chord_now(nm);
    if (text_w(&FONT_B, nm) <= CHORD_W - 8)
        text(CHORD_X + 5, CHORD_Y + 1, &FONT_B, nm, K_INK);
    else
        text(CHORD_X + 5, CHORD_Y + 2, &FONT_XS, nm, K_INK);
    text(CHORD_X + 5, CHORD_Y + 17, &FONT_XS, en_mode == EN_AUTO ? EN_SECTION_NAME[en_section] : EN_STYLE_NAME[en_style], K_SOFT);
}
static uint32_t chord_panel_sig(void) { return hash(chord_sig(), (uint32_t)en_mode | (uint32_t)en_section << 2 | (uint32_t)en_style << 6); }

static const struct {
    char c[2];
    uint8_t layer;
} LAYER_MARK[EN_NLAYERS] = {{"K", EN_L_KEYS}, {"M", EN_L_MEL}, {"B", EN_L_BASS}, {"D", EN_L_DRUMS}, {"H", EN_L_HATS}};
/* a layer as the screen has it: 0 off, 1 on but the arranger has it absent now, 2 playing */
static int layer_shown(int l) { return layer_off(l) ? 0 : (en_layers_absent >> l & 1u) ? 1 : 2; }
static void draw_layers(void)
{
    int i;
    if (!in_view(LAYERS_X, LAYERS_Y, LAYERS_W, LAYERS_H))
        return;
    pane(LAYERS_X, LAYERS_Y, LAYERS_W, LAYERS_H, 5, 8u, 0);
    text(LAYERS_X + 5, LAYERS_Y, &FONT_XS, en_mode == EN_HANDS ? "HANDS" : "AUTO", K_INK);
    for (i = 0; i < EN_NLAYERS; i++) {
        const int s = layer_shown(LAYER_MARK[i].layer);
        const int32_t cx = LAYERS_X + 8 + i * 10;
        text_c(cx, LAYERS_Y + 13, &FONT_XS, LAYER_MARK[i].c, s == 2 ? K_INK : s == 1 ? K_SOFT : K_OFF);
        if (s == 2)
            rect(cx - 2, LAYERS_Y + 26, 4, 1, K_INK);
    }
}
static uint32_t layers_sig(void)
{
    uint32_t h = en_mode, i;
    for (i = 0; i < EN_NLAYERS; i++)
        h = h << 2 | (uint32_t)layer_shown(LAYER_MARK[i].layer);
    return h + 1u;
}

static void meters_read(float *lv)               /* a layer off shows empty */
{
    float bm = 0.0f;
    int i;
    for (i = 0; i < EN_NLOOP; i++)
        bm = fm_maxf(bm, en_loop_glow[i]);
    lv[0] = en_mute_keys ? 0.0f : en_pad_level * (float)proj.par[P_KEYS] * 0.01f;
    lv[1] = en_mute_mel ? 0.0f : bm * (float)proj.par[P_MOTIFS] * 0.01f;
    lv[2] = en_mute_bass ? 0.0f : en_bass_level * (float)proj.par[P_BASS] * 0.01f;
    lv[3] = en_mute_drums ? 0.0f : en_break_level * 4.0f;
}
#define METER_MAX 26
static int meter_h(float v) { return (int)(fm_clampf(v, 0.0f, 1.0f) * (float)METER_MAX + 0.5f); }
static void draw_meters(void)                    /* bottom right: the four layers */
{
    float lv[4];
    int i;
    if (!in_view(MTR_X, MTR_Y, MTR_W, MTR_H))
        return;
    meters_read(lv);
    pane(MTR_X, MTR_Y, MTR_W, MTR_H, 5, 8u, 0);
    for (i = 0; i < 4; i++) {
        const int32_t x = MTR_X + 2 + i * 7, h = meter_h(lv[i]);
        rect(x, 174 - 1, 5, 2, K_TRACK);
        if (h > 0)
            rect(x, 174 - h, 5, h, K_INK);
    }
}
static uint32_t meters_sig(void)
{
    float lv[4];
    uint32_t h = 0u;
    int i;
    meters_read(lv);
    for (i = 0; i < 4; i++)
        h = h << 8 | (uint32_t)meter_h(lv[i]);
    return h + 1u;
}

/* a button's card (help_tick): its name, then what it does. A line starting with a space goes on from the one before (a screen reader joins them: sp_line) */
#define HELP_LINES 4
static const char *const BTN_LABEL[NB] = {"FX", "SEL", "ENV", "LFO", "EDIT", "GLO", "HOME", "SAVE", "ARP", "SEQ",
                                          "PLAY", "REC", "OCT-", "OCT+"};
static int help_lines(int b, const char *l[HELP_LINES])
{
    int n = 0;
#define L(t) (l[n++] = (t))
    switch (b) {
    case B_HOME: L("TEMPO, ENERGY, EVOLVE, STYLE"); L("HOLD + TURN SELECT: TEMPO"); L("SEL + KEY: CHORDS ON IT, OR NOT"); L("SEL + HOME: HANDS OR AUTO"); break;
    case B_ENV: L("THE KEYS AND THE PAD"); L("LEVEL, BLOOM, BRIGHT, NOTES"); L("SEL + ENV: ON OR OFF"); L("SEL + KNOB 1, KNOB 2: THE SOUNDS"); break;
    case B_ARP: L("THE MELODY: LEVEL, RATE, TONE, OCT"); L("SEL + ARP: ON OR OFF"); L("SEL + KNOB 1: THE SOUND"); L("SEL + KNOB 2-4: ARP, PATTERN, SKIP"); break;
    case B_LFO: L("THE BASS"); L("LEVEL, MOVE, GLIDE, OCTAVE"); L("SEL + LFO: ON OR OFF"); L("SEL + KNOB 1: THE SOUND"); break;
    case B_SEQ: L("THE DRUMS"); L("LEVEL, BREAKS, HATS, KIT"); L("SEL + SEQ: KICK AND SNARE"); L(" ON OR OFF"); break;
    case B_FX: L("THE SPACE"); L("ECHO, ECHO TIME, REVERB,"); L(" SIZE"); L("SEL + FX: HATS ON OR OFF"); break;
    case B_GLO: L("SETUP: ROOT, TUNE, MIDI OUT, LIGHTS"); L("CPU LOAD, M WORST BLOCK,"); L(" X MISSED, S SHED"); L("AGAIN: THE READING AFRESH"); break;
    case B_EDIT: L("THE NEXT SECTION,"); L(" ON THE NEXT BAR"); L("+ SELECT: A BREAK,"); L(" ON THE NEXT BAR"); break;
    case B_REC: L("HOLD: THE CHORD STAYS,"); L(" NO NEW SECTION"); L("THE REST PLAYS ON"); L("AGAIN: LET GO"); break;
    case B_OCTDN: L("THE MELODY AN OCTAVE DOWN"); L("WITH OCT+, 5 S: UPDATE MODE"); break;
    case B_OCTUP: L("THE MELODY AN OCTAVE UP"); L("WITH OCT-, 5 S: UPDATE MODE"); break;
    case B_SAVE: L("SAVES ALL EIGHT PIECES"); L("KEEP HOLDING: THE PIECE"); L(" BACK TO ITS RECIPE"); break;
    default: break;
    }
#undef L
    return n;
}
static void draw_help(void)
{
    const char *l[HELP_LINES];
    const int n = help_lines(ui.help - 1, l);
    int k;
    pane(16, 42, 208, 117, 8, 4u, 0);
    text_c(120, 47, &FONT_B, BTN_LABEL[ui.help - 1], K_INK);
    rect(26, 66, 188, 1, K_SOFT);
    for (k = 0; k < n; k++)
        text_c(120, 72 + k * 20, &FONT_XS, l[k] + (l[k][0] == ' '), K_INK);
}

/* the knobs: a cell each on the pane: its name, a ring, the value. The one being turned is lit. A cell redrawn alone reads nothing from the flash */
#define KCELL_W 60
static void draw_knob(int i)
{
    const int k = VIEW_KNOB[ui.view][i], hot = scr.now < ui.touch_until && ui.touched == i;
    const int32_t x = i * KCELL_W, cx = x + 30;
    char t[16];
    int lo, hi, v;
    if (k == K_NONE || !in_view(x, PANEL_Y, KCELL_W, PANEL_H))
        return;
    if (hot)
        rect(x + 3, PANEL_Y + 2, 54, PANEL_H - 3, K_HOT);
    knob_range(k, &lo, &hi);
    v = knob_get(k);
    caps(t, KNOB_LABEL[ui.view][i], sizeof t);
    text_c(cx, PANEL_Y + 2, &FONT_XS, t, hot ? K_INK : K_SOFT);
    arc(cx, PANEL_Y + 29, 11, hi > lo ? (float)(v - lo) / (float)(hi - lo) : 0.0f, K_TRACK, K_INK, lo < 0);
    knob_text(k, t);
    text_c(cx, PANEL_Y + 43, &FONT_B, t, K_INK);
}
static void draw_knobs(void)
{
    int i;
    if (!in_view(0, PANEL_Y, 240, PANEL_H))
        return;
    rect(0, PANEL_Y, 240, 1, K_INK);
    for (i = 0; i < 4; i++)
        draw_knob(i);
}
static uint32_t knob_sig(int i)
{
    return (uint32_t)(knob_get(VIEW_KNOB[ui.view][i]) + 1024) << 2 | (uint32_t)proj.par[P_MELMODE] << 1 | (uint32_t)(scr.now < ui.touch_until && ui.touched == i);
}

/* ---- the face: the closed cell, and patches of the others around the eyes and the mouth. Eyes closed while it listens (no drums), half open with drums, open on a
 * full beat or for a while after a new section; a blink now and then, on a bar line; a smile when a layer comes in or SAVE saves. Eyes change by way of the half-open cell */
enum { EYE_CLOSED, EYE_HALF, EYE_OPEN };
static int eye_wanted(uint32_t now)
{
    const int drums = !en_mute_drums && !(en_layers_absent >> EN_L_DRUMS & 1u) && proj.par[P_DRUMS] > 0;
    const uint32_t t = now - scr.blink_t;
    int w;
    if (scr.scene != SC_HOME || !drums)
        return EYE_CLOSED;
    w = en_drum_level == 3u || (int32_t)(scr.open_until - now) > 0 ? EYE_OPEN : EYE_HALF;   /* (3 the full beat; 4, half time, ranks under the light one: amb.c LV_RANK) */
    if (scr.blinking) {
        if (t < 2u * EYE_STEP)
            return EYE_CLOSED;
        scr.blinking = 0u;
    }
    return w;
}
static void face_tick(uint32_t now)
{
    const uint32_t bar = en_bars / 16u;
    int w;
    if (en_section != scr.sec_seen) {
        if (scr.scene != SC_TITLE)               /* (the first it has at the start is no news) */
            scr.open_until = now + OPEN_MS;
        scr.sec_seen = en_section;
    }
    if (bar != scr.bar_seen) {
        scr.bar_seen = bar;
        if (scr.scene == SC_HOME && scr.eye != EYE_CLOSED && bar - scr.blink_bar >= BLINK_BARS) {
            scr.blink_bar = bar;
            scr.blink_t = now;
            scr.blinking = 1u;
#ifdef PLAT_HOST
            scr.blinks++;
#endif
        }
    }
    w = eye_wanted(now);
    if (w != scr.eye && now - scr.eye_t >= EYE_STEP) {
        scr.eye = (uint8_t)(w > scr.eye ? scr.eye + 1 : scr.eye - 1);   /* one cell at a time */
        scr.eye_t = now;
    }
}
static int mouth_smiling(int sc, uint32_t now) { return sc == SC_SAVE || (sc == SC_HOME && (int32_t)(scr.smile_until - now) > 0); }

/* ---- what moves on Home, in code: now and then a star in the hair; a feather falling on a new chord */
static const uint8_t STAR_SPOT[5][2] = {{200, 40}, {224, 84}, {150, 152}, {42, 112}, {96, 34}};
static const uint8_t SAVE_STAR[5][3] = {{30, 64, 10}, {208, 46, 8}, {18, 126, 6}, {216, 132, 12}, {118, 40, 7}};
static const uint8_t HOLD_STAR[3][3] = {{42, 60, 6}, {198, 70, 7}, {122, 150, 5}};
static int32_t star_size(uint32_t now)           /* 0: none */
{
    static const uint8_t S[5] = {2, 4, 6, 4, 2};
    const uint32_t t = now - scr.star_t;
    return scr.star_on && t < STAR_MS ? S[t / (STAR_MS / 5u)] : 0;
}
static void moving_tick(uint32_t now)
{
    const uint32_t bar = en_bars / 16u, c = chord_sig();
    if (scr.scene == SC_HOME && en_playing) {
        if (c != scr.chord_seen && !scr.feather_on) {
            scr.feather_on = 1u;
            scr.feather_t = now;
            scr.feather_x = (uint8_t)(70u + c % 110u);
        }
        if (bar != scr.star_bar && bar % 4u == 0u) {
            scr.star_on = 1u;
            scr.star_t = now;
            scr.star_i = (uint8_t)((scr.star_i + 1u) % 5u);
        }
    }
    scr.star_bar = bar;
    scr.chord_seen = c;
    if (scr.feather_on && (scr.scene != SC_HOME || now - scr.feather_t >= FEATHER_MS))
        scr.feather_on = 0u;
    if (scr.star_on && (scr.scene != SC_HOME || now - scr.star_t >= STAR_MS))
        scr.star_on = 0u;
}
static void feather_at(uint32_t now, int32_t *x, int32_t *y, int *lean)
{
    const uint32_t step = (now - scr.feather_t) / 125u;  /* it moves eight times a second: each a small rectangle */
    const uint32_t ph = step % 12u;
    const int32_t sway = (int32_t)(ph < 6u ? ph : 12u - ph) - 3;
    *x = scr.feather_x + sway * 2;
    *y = 34 + (int32_t)(step * 110u / 24u);
    *lean = sway;
}

/* ---- the degrees, on SEL held: the ring large, with the notes' names: chords may stand on those lit; struck through, not; the tonic has a line round it */
static void deg_names(char *b, int n)            /* the notes chords may stand on, from the root up */
{
    int i, k = 0;
    for (i = 0; i < 12; i++) {
        const int pc = (proj.par[P_ROOT] + i) % 12;
        if ((proj.pool >> i & 1u) && !degree_off(pc)) {
            const char *s = EN_NOTE_NAME[pc];
            if (k && k < n - 1)
                b[k++] = ' ';
            while (*s && k < n - 1)
                b[k++] = *s++;
        }
    }
    b[k] = 0;
}
static void draw_deg_bead(int i)
{
    const int32_t x = DEG_X[i], y = DEG_Y[i];
    const int pc = (proj.par[P_ROOT] + i) % 12;
    const char *nm = EN_NOTE_NAME[pc];
    int32_t j;
    if (!in_view(x - DEG_R, y - DEG_R, 2 * DEG_R + 1, 2 * DEG_R + 1))
        return;
    if (!(proj.pool >> i & 1u)) {
        disc(x, y, 1.0f, K_SOFT);
    } else if (degree_off(pc)) {
        disc(x, y, 10.0f, K_DARK);
        circle(x, y, 10.0f, 1.0f, K_MID, 16u);
        text_c(x, y - 8, &FONT_XS, nm, K_MID);
        for (j = -6; j <= 6; j++)
            px_blend(x + j - SX, y - j, K_MID, 16u);
    } else {
        disc(x, y, 11.0f, K_DARK);
        disc(x, y, 10.0f, K_INK);
        text_c(x, y - 8, &FONT_XS, nm, K_DARK);
    }
    if (i == 0)
        circle(x, y, 13.0f, 1.0f, K_INK, 16u);
}
#define DEGP_X 72
#define DEGP_Y 102
#define DEGP_W 96
#define DEGP_H 46
static void draw_deg_panel(void)
{
    char b[48];
    if (!in_view(DEGP_X, DEGP_Y, DEGP_W, DEGP_H))
        return;
    pane(DEGP_X, DEGP_Y, DEGP_W, DEGP_H, 8, 6u, 0);
    text_c(120, DEGP_Y + 4, &FONT_XS, "chords on", K_SOFT);
    deg_names(b, sizeof b);
    if (text_w(&FONT_B, b) <= DEGP_W - 8) {
        text_c(120, DEGP_Y + 20, &FONT_B, b, K_INK);
    } else {                                     /* a long scale: two lines, cut at a space */
        int k = 0, cut = 0;
        while (b[k]) {
            if (b[k] == ' ') {
                b[k] = 0;
                if (text_w(&FONT_XS, b) <= DEGP_W - 8)
                    cut = k;
                b[k] = ' ';
            }
            k++;
        }
        b[cut] = 0;
        text_c(120, DEGP_Y + 17, &FONT_XS, b, K_INK);
        text_c(120, DEGP_Y + 29, &FONT_XS, b + cut + 1, K_INK);
    }
}

/* the piece's card: under its name, its mood (the spec's) */
static const char *const PIECE_LINE[NPIECES] = {"The wait itself", "Sunny, glossy", "Open, the fastest", "More room, a sparse beat",
                                                "Wide pads, slow light", "The warmest, on the bass", "The quietest, to rest", "Sunset, in minor"};
static void draw_card(void)
{
    char t[32];
    uint32_t k = 0;
    const char *s;
    if (!in_view(0, 0, 240, 240))
        return;
    rect(0, 158, 240, 1, K_INK);
    text(10, 160, &FONT_L, piece_name(proj.piece), K_INK);
    text(10, 197, &FONT_XS, PIECE_LINE[proj.piece % NPIECES], K_SOFT);
#define C(p) for (s = (p); *s && k < sizeof t - 1u; s++) t[k++] = *s
    C(EN_NOTE_NAME[proj.par[P_ROOT]]);
    C(" ");
    C(mode_name());
    C("  /  ");
    C(EN_STYLE_NAME[proj.par[P_STYLE]]);
    C("  /  ");
#undef C
    itoa_u((uint32_t)proj.par[P_TEMPO], t + k);
    text(10, 215, &FONT_XS, t, K_INK);
    t[0] = '0';
    itoa_u((uint32_t)proj.piece + 1u, t + 1);
    k = 2;
    t[k++] = ' ', t[k++] = '/', t[k++] = ' ', t[k++] = '0';
    itoa_u(NPIECES, t + k);
    text_r(231, 6, &FONT_B, t, K_INK);
}
static uint32_t card_sig(void)
{
    return hash(hash(hash(2166136261u, proj.piece), (uint32_t)proj.par[P_ROOT] | (uint32_t)proj.pool << 8), (uint32_t)proj.par[P_MODE] | (uint32_t)proj.par[P_TEMPO] << 8 | (uint32_t)proj.par[P_STYLE] << 20);
}
static void draw_break(void)
{
    char t[12];
    text(10, 192, &FONT_L, "BREAK", K_INK);
    itoa_u(en_break_bars, t);
    {
        uint32_t k = 0;
        const char *s = en_break_bars == 1u ? " BAR" : " BARS";
        while (t[k]) k++;
        while (*s) t[k++] = *s++;
        t[k] = 0;
    }
    text_r(231, 206, &FONT_B, t, K_SOFT);
}
static int save_star_size(int i, uint32_t now)   /* each star grows and goes in its turn */
{
    const uint32_t ph = (now / 120u + (uint32_t)i * 2u) % 6u;
    return (int)SAVE_STAR[i][2] * (int)(ph < 3u ? ph + 2u : 7u - ph) / 4;
}
static void draw_rest(void)
{
    if (scr.now < ui.touch_until) {
        draw_knobs();
        return;
    }
    if (!in_view(0, PANEL_Y, 240, PANEL_H))
        return;
    rect(0, PANEL_Y, 240, 1, K_INK);
    text_c(120, 182, &FONT_L, "REST", K_INK);
    text_c(120, 218, &FONT_XS, "PLAY TO WAKE", K_SOFT);
}
static void frost(void)                          /* the corners under the header, frozen; little of it */
{
    int32_t k, j;
    for (k = 0; k < 18; k += 6)
        for (j = 0; j < 24 - k; j++) {
            px_blend(j - SX, 28 + (24 - k - j), K_ICE, 7u);
            px_blend(239 - j - SX, 28 + (24 - k - j), K_ICE, 7u);
        }
}

/* everything on the picture, cut to the canvas. Whatever stands in the rectangle kept under the meters must not move: the feather stays left of it */
static void overlay(int sc)
{
    const uint32_t now = scr.now, chord = sc == SC_HOME || sc == SC_HOLD ? chord_pcs() : 0u;
    int i;
    switch (sc) {
    case SC_HOME: case SC_HOLD:
        if (sc == SC_HOLD) {
            frost();
            for (i = 0; i < 3; i++)
                star(HOLD_STAR[i][0], HOLD_STAR[i][1], HOLD_STAR[i][2], 14u);
        } else {
            if (star_size(now))
                star(STAR_SPOT[scr.star_i][0], STAR_SPOT[scr.star_i][1], star_size(now), 15u);
            if (scr.feather_on) {
                int32_t x, y;
                int lean;
                feather_at(now, &x, &y, &lean);
                feather(x, y, lean);
            }
        }
        for (i = 0; i < 12; i++)
            draw_bead(i, bead_state(i, chord));
        draw_chord();
        draw_layers();
        draw_meters();
        if (sc == SC_HOLD && in_view(86, 146, 73, 25)) {
            pane(86, 146, 73, 25, 5, 5u, 0);
            text_c(122, 150, &FONT_B, "FROZEN", K_INK);
        }
        draw_knobs();
        break;
    case SC_PAGE:
        draw_knobs();
        break;
    case SC_REST:
        draw_rest();
        break;
    case SC_DEG:
        for (i = 0; i < 12; i++)
            draw_deg_bead(i);
        draw_deg_panel();
        break;
    case SC_HELP:
        draw_help();
        break;
    case SC_TITLE:
        text_c(120, 166, &FONT_L, "AQ-1", K_INK);
        text_c(120, 204, &FONT_XS, "NOW LOADING", K_SOFT);
        break;
    case SC_CARD:
        draw_card();
        break;
    case SC_BREAK:
        draw_break();
        break;
    case SC_SAVE:
        for (i = 0; i < 5; i++)
            star(SAVE_STAR[i][0], SAVE_STAR[i][1], save_star_size(i, now), 15u);
        rect(0, 184, 240, 1, K_INK);
        rect(0, 224, 240, 1, K_INK);
        text_c(120, 192, &FONT_M, "SAVE POINT", K_INK);
        break;
    default: break;
    }
    draw_header(sc);
}

/* the rows kept in the spare part of the canvas: their place, or 0 if this piece of a row is not kept */
static uint16_t *back_row(int32_t y, int32_t x, int32_t w)
{
    if (y < HDR_H)
        return BACK_HDR + (uint32_t)y * 240u + (uint32_t)x;
    if (y >= PANEL_Y)
        return BACK_KNB + (uint32_t)(y - PANEL_Y) * 240u + (uint32_t)x;
    if (y >= MTR_Y && y < MTR_Y + MTR_H && x >= MTR_X && x + w <= MTR_X + MTR_W)
        return BACK_MTR + (uint32_t)(y - MTR_Y) * MTR_W + (uint32_t)(x - MTR_X);
    return 0;
}
static void back_store(int32_t y, const uint16_t *row)   /* (row: a whole width of the screen) */
{
    if (y < HDR_H || y >= PANEL_Y)
        memcpy(back_row(y, 0, 240), row, 240u * sizeof(uint16_t));
    else if (y >= MTR_Y && y < MTR_Y + MTR_H)
        memcpy(back_row(y, MTR_X, MTR_W), row + MTR_X, MTR_W * sizeof(uint16_t));
}

/* the rectangle, on the canvas: the picture, or what is kept of it, and everything on it. The canvas's rows are set by hand: cv_begin would fill them first */
static void paint_canvas(int sc, int32_t x, int32_t y, int32_t w, int32_t h)
{
    const art_t *a = &ART[scene_art(sc)];
    const uint16_t(*lut)[8] = ART_LUT[scene_look(sc)];
    const int patches = a == &ART[ART_FACE];
    int32_t r;
    lcd_sync();                                  /* the last blit may still read cv_px */
    cv_w = (uint32_t)w;
    cv_h = (uint32_t)h;
    cv_oy = -y;
    cv_y0 = 0;
    cv_y1 = h;
    SX = x;
    for (r = y; r < y + h; r++) {
        uint16_t *dst = cv_px + (uint32_t)(r - y) * (uint32_t)w;
        const uint16_t *kept = scr.back_ok ? back_row(r, x, w) : 0;
        int32_t g0, g1;
        uint32_t keep, n = 0u;
        if (kept) {
            memcpy(dst, kept, (uint32_t)w * sizeof(uint16_t));
            continue;
        }
        n += art_row(a, (uint32_t)r, dst, x, x + w, lut[r & 1]);
        if (patches) {
            const art_t *e = scr.eye == EYE_HALF ? &ART[ART_EYE_HALF] : scr.eye == EYE_OPEN ? &ART[ART_EYE_OPEN] : 0;
            const art_t *m = mouth_smiling(sc, scr.now) ? &ART[ART_MOUTH_SMILE] : 0;
            if (e && r >= e->y && r < e->y + e->h && x < e->x + e->w && x + w > e->x)
                n += art_row(e, (uint32_t)(r - e->y), dst, x, x + w, lut[r & 1]);
            if (m && r >= m->y && r < m->y + m->h && x < m->x + m->w && x + w > m->x)
                n += art_row(m, (uint32_t)(r - m->y), dst, x, x + w, lut[r & 1]);
        }
        if (base_pane(sc, r, &g0, &g1, &keep)) {
            g0 = g0 < x ? x : g0;
            g1 = g1 > x + w ? x + w : g1;
            if (g1 > g0)
                glass_px(dst + (g0 - x), g1 - g0, keep);
        }
        if (w == 240)
            back_store(r, dst);
#ifdef PLAT_HOST
        scr.f_rows++;
        scr.f_bytes += n;
        scr.t_bytes += n;
#else
        (void)n;
#endif
    }
    overlay(sc);
}
static void paint(int sc, int32_t x, int32_t y, int32_t w, int32_t h)
{
    paint_canvas(sc, x, y, w, h);
    cv_blit((uint32_t)x, (uint32_t)y);
#ifdef PLAT_HOST
    scr.f_px += (uint32_t)(w * h);
#endif
}

/* ---- what changed: each thing on the screen says where it stands and a number for how it looks. A change marks both places; marked places are painted as the
 * frame's allowance lets them. While a whole picture is going on they wait, and are painted after it */
static void el_mark(el_t *e, int32_t x0, int32_t y0, int32_t x1, int32_t y1)
{
    if (x1 <= x0 || y1 <= y0)
        return;
    if (e->dy1 > e->dy0) {
        x0 = x0 < e->dx0 ? x0 : e->dx0;
        y0 = y0 < e->dy0 ? y0 : e->dy0;
        x1 = x1 > e->dx1 ? x1 : e->dx1;
        y1 = y1 > e->dy1 ? y1 : e->dy1;
    }
    e->dx0 = (uint8_t)x0, e->dy0 = (uint8_t)y0, e->dx1 = (uint8_t)x1, e->dy1 = (uint8_t)y1;
}
static void el_set(int id, int32_t x, int32_t y, int32_t w, int32_t h, uint32_t sig)
{
    el_t *e = &el[id];
    x = x < 0 ? 0 : x;
    y = y < 0 ? 0 : y;
    w = x + w > 240 ? 240 - x : w;
    h = y + h > 240 ? 240 - y : h;
    if (!scr.fresh && (e->sig != sig || e->x != x || e->y != y || e->w != w || e->h != h)) {
        el_mark(e, e->x, e->y, e->x + e->w, e->y + e->h);
        el_mark(e, x, y, x + w, y + h);
    }
    e->sig = sig;
    e->x = (uint8_t)x, e->y = (uint8_t)y, e->w = (uint8_t)w, e->h = (uint8_t)h;
}
static void elements(int sc)
{
    const uint32_t now = scr.now;
    int i;
    el_set(E_HDR, 0, 0, 240, HDR_H, header_sig(sc));
    if (has_knobs(sc)) {
        const int up = sc != SC_REST || now < ui.touch_until;
        if (sc == SC_REST)
            el_set(E_TEXT, 0, PANEL_Y, 240, PANEL_H, 1u + (uint32_t)up);
        for (i = 0; i < 4; i++)
            el_set(E_K0 + i, i * KCELL_W, PANEL_Y, KCELL_W, PANEL_H, up ? knob_sig(i) : 0u);
    }
    if (sc == SC_HOME || sc == SC_HOLD) {
        const uint32_t chord = chord_pcs();
        for (i = 0; i < 12; i++)
            el_set(E_BEAD0 + i, RING_X[i] - BEAD_R, RING_Y[i] - BEAD_R, 2 * BEAD_R + 1, 2 * BEAD_R + 1, bead_state(i, chord) + 1u);
        el_set(E_CHORD, CHORD_X, CHORD_Y, CHORD_W, CHORD_H, chord_panel_sig());
        el_set(E_LAYERS, LAYERS_X, LAYERS_Y, LAYERS_W, LAYERS_H, layers_sig());
        if (!(ui.frame % 3u) || scr.settling)    /* the meters: 20 frames a second is plenty */
            el_set(E_METER, MTR_X, MTR_Y, MTR_W, MTR_H, meters_sig());
    }
    if (sc == SC_HOME) {
        const art_t *eye = &ART[ART_EYE_HALF], *mouth = &ART[ART_MOUTH_SMILE];
        const int32_t st = star_size(now);
        el_set(E_EYES, eye->x, eye->y, eye->w, eye->h, scr.eye + 1u);
        el_set(E_MOUTH, mouth->x, mouth->y, mouth->w, mouth->h, (uint32_t)mouth_smiling(sc, now) + 1u);
        if (scr.feather_on) {
            int32_t x, y;
            int lean;
            feather_at(now, &x, &y, &lean);
            el_set(E_FEATHER, x - 8, y - 4, 18, FEATHER_H + 4, (uint32_t)(x | y << 8 | (uint32_t)(lean + 8) << 16));
        } else {
            el_set(E_FEATHER, 0, 0, 0, 0, 0u);
        }
        if (st)
            el_set(E_STAR0, STAR_SPOT[scr.star_i][0] - 7, STAR_SPOT[scr.star_i][1] - 7, 15, 15, (uint32_t)st | (uint32_t)scr.star_i << 8);
        else
            el_set(E_STAR0, 0, 0, 0, 0, 0u);
    }
    if (sc == SC_SAVE)
        for (i = 0; i < 5; i++) {
            const int32_t R = SAVE_STAR[i][2] * 3 / 2 + 1;
            el_set(E_STAR0 + i, SAVE_STAR[i][0] - R, SAVE_STAR[i][1] - R, 2 * R + 1, 2 * R + 1, (uint32_t)save_star_size(i, now) + 1u);
        }
    if (sc == SC_DEG) {
        for (i = 0; i < 12; i++)
            el_set(E_BEAD0 + i, DEG_X[i] - DEG_R, DEG_Y[i] - DEG_R, 2 * DEG_R + 1, 2 * DEG_R + 1,
                   (uint32_t)(proj.pool >> i & 1u) | (uint32_t)degree_off((proj.par[P_ROOT] + i) % 12) << 1 | (uint32_t)(proj.par[P_ROOT] << 4));
        el_set(E_CHORD, DEGP_X, DEGP_Y, DEGP_W, DEGP_H, hash(hash(2166136261u, proj.pool), degrees_shown() | (uint32_t)proj.par[P_ROOT] << 12));
    }
    if (sc == SC_CARD)
        el_set(E_TEXT, 0, 0, 240, 28, card_sig()), el_set(E_TEXT2, 0, 158, 240, 82, card_sig());
    if (sc == SC_BREAK)
        el_set(E_TEXT, 150, 200, 90, 30, en_break_bars);
}

static int scr_pending(void)
{
    uint32_t i;
    if (scr.wipe_left)
        return 1;
    for (i = 0; i < E_N; i++)
        if (el[i].dy1 > el[i].dy0)
            return 1;
    return 0;
}

static void scr_frame(void)
{
    const uint32_t now = plat_ms();
    uint32_t px = PX_FRAME, rows = WIPE_ROWS, i;
    int sc;
    scr.now = now;
    sc = scene_pick(now);
#ifdef PLAT_HOST
    {
        const int quiet = en_break_bars && (!en_playing || en_mute_drums || !proj.par[P_DRUMS]);
        scr.brk_shown += sc == SC_BREAK;
        scr.brk_quiet += quiet;
        scr.brk_wrong += quiet && sc == SC_BREAK;
    }
#endif
#ifdef PLAT_HOST
    if (!scr.settling)
        scr.f_rows = scr.f_bytes = scr.f_px = 0u;
#endif
    if (now - scr.r_t >= 250u) {                 /* Setup's reading, four times a second: it is read, not watched, and this way it costs little itself */
        scr.r_t = now;
        scr.r_cpu = plat_cpu_pct();
        scr.r_max = app_render_max_us;
        scr.r_x = plat_xruns();
        scr.r_s = app_sheds;
    }
    {
        const uint32_t key = (uint32_t)sc | (uint32_t)ui.view << 8 | (uint32_t)ui.help << 16;
        if (key != scr.key) {                    /* a new picture: from where the last band stopped, all the way round */
            scr.key = key;
            scr.scene = (uint8_t)sc;
            scr.wipe_left = 240u;
            scr.back_ok = 0u;
            scr.fresh = 1u;
            scr.eye_t = now;
            if (sc != SC_HOME)                   /* (the face is in its picture: its eyes do not change under the bands) */
                scr.eye = EYE_CLOSED;
            memset(el, 0, sizeof el);
#ifdef PLAT_HOST
            scr.wipes++;
#endif
        }
    }
    face_tick(now);
    moving_tick(now);
    elements(sc);
    scr.fresh = 0u;
    if (scr.wipe_left) {
        uint32_t n = scr.wipe_left < WIPE_ROWS ? scr.wipe_left : WIPE_ROWS;
        if (n > 240u - scr.wipe_y)
            n = 240u - scr.wipe_y;
        paint(sc, 0, (int32_t)scr.wipe_y, 240, (int32_t)n);
        scr.wipe_y = (uint16_t)((scr.wipe_y + n) % 240u);
        scr.wipe_left = (uint16_t)(scr.wipe_left - n);
        if (!scr.wipe_left)
            scr.back_ok = 1u;
    } else {
        for (i = 0; i < E_N && px; i++) {
            el_t *e = &el[i];
            while (e->dy1 > e->dy0 && px) {
                const uint32_t w = (uint32_t)(e->dx1 - e->dx0);
                const int kept = scr.back_ok && back_row(e->dy0, e->dx0, (int32_t)w) && back_row(e->dy1 - 1, e->dx0, (int32_t)w) && (e->dy1 <= HDR_H || e->dy0 >= PANEL_Y || w <= MTR_W);
                uint32_t n = (uint32_t)(e->dy1 - e->dy0);
                if (n > px / w)
                    n = px / w;
                if (!kept && n > rows)
                    n = rows;
                if (!n) {
                    px = kept ? 0u : px;
                    break;
                }
                paint(sc, e->dx0, e->dy0, (int32_t)w, (int32_t)n);
                e->dy0 = (uint8_t)(e->dy0 + n);
                px -= n * w;
                if (!kept)
                    rows -= n;
            }
            if (!rows && !px)
                break;
        }
    }
#ifdef PLAT_HOST
    if (!scr.settling) {
        scr.p_rows = scr.f_rows > scr.p_rows ? scr.f_rows : scr.p_rows;
        scr.p_bytes = scr.f_bytes > scr.p_bytes ? scr.f_bytes : scr.p_bytes;
        scr.p_px = scr.f_px > scr.p_px ? scr.f_px : scr.p_px;
    }
#endif
}

#ifdef PLAT_HOST
/* the simulator's: everything marked painted, and then the screen against the same scene painted whole from the flash: the pixels that differ. If the small
 * rectangles, what is kept and their order ever miss anything, it shows here */
static int scr_stale(void)
{
    int32_t y, i, bad = 0, bx0 = 240, by0 = 240, bx1 = -1, by1 = -1;
    int g;
    scr.settling = 1u;
    for (g = 0; g < 400 && (g < 1 || scr_pending()); g++)
        scr_frame();
    scr.settling = 0u;
    if (scr_pending())
        return -1;
    {
        const uint8_t keep = scr.back_ok;
        scr.back_ok = 0u;
        for (y = 0; y < 240; y += WIPE_ROWS) {
            const int32_t h = 240 - y < WIPE_ROWS ? 240 - y : WIPE_ROWS;
            paint_canvas(scr.scene, 0, y, 240, h);
            for (i = 0; i < 240 * h; i++)
                if (fb[y * 240 + i] != cv_px[i]) {
                    const int32_t px = i % 240, py = y + i / 240;
                    bad++;
                    bx0 = px < bx0 ? px : bx0, bx1 = px > bx1 ? px : bx1;
                    by0 = py < by0 ? py : by0, by1 = py > by1 ? py : by1;
                }
        }
        scr.back_ok = keep;
    }
    if (bad)
        printf("stale: scene %d, %d pixels in x %d..%d y %d..%d\n", scr.scene, (int)bad, (int)bx0, (int)bx1, (int)by0, (int)by1);
    return bad;
}
#endif

/* the keys' lights: the scale; with SEL held, those of it that chords may stand on */
static uint32_t keys_lit(void)
{
    const uint32_t ap = pool_abs();
    const int sel = (int)(ui.btn >> B_SEL & 1u);
    uint32_t k = 0, i;
    if (proj.leds)
        for (i = 0; i < NKEYS; i++) {
            const int pc = (int)((5u + i) % 12u);
            if (ap >> pc & 1u && !(sel && degree_off(pc)))
                k |= 1u << i;
        }
    return k;
}

/* the page buttons of the layers that are off: no glow (the page you are on stays lit) */
static uint32_t leds_dark(void)
{
    uint32_t d = 0u;
    int i;
    for (i = 0; i < EN_NLAYERS; i++)
        if (layer_off(LAYER_BTN[i].layer))
            d |= 1u << LAYER_BTN[i].btn;
    return d & ~(1u << VIEW_BTN[ui.view]);
}

static void leds(void)
{
    uint32_t b = 0, k = keys_lit(), dark = leds_dark();
    b |= 1u << VIEW_BTN[ui.view];
    if (en_playing)
        b |= 1u << B_PLAY;
    if (en_held)
        b |= 1u << B_REC;
    plat_glow(proj.leds == 1);
    if (!dark || proj.leds != 1) {
        plat_leds(b, k);
    } else {                                     /* plat_leds' own, less those; PLAY on its green LED */
        const uint32_t play = 1u << B_PLAY, dim = ((1u << NB) - 1u) & ~b & ~dark;
        plat_lights((b & ~play) | (b & play ? LED_PLAY_GREEN : 0u), k, (dim & ~play) | (dim & play ? LED_PLAY_GREEN : 0u), 0, 2);
    }
}

static int autosave_due(uint32_t now)
{
    return ui.dirty && !sv.failed && !sv.hushed && !en_playing && !ui.btn && !ui.keys && now - ui.act_t >= AUTOSAVE_QUIET;
}

static void save_write(uint32_t now)
{
    sv.ok = project_save() == 0;
    if (sv.ok) {
        ui.dirty = 0;
        ui.saved_t = now;
        if (sv.manual) {
            say("SAVED", 0);
            ui.save_t = now;
        }
    } else {
        sv.failed = 1u;
        warn("NOT SAVED", "FLASH ERROR");
    }
    en_duck(0);
    sv.t = plat_ms();                  /* (the write took its time) */
    sv.st = SV_RETURN;
}

static void save_tick(void)
{
    const uint32_t now = plat_ms();
    uint32_t i;
    switch (sv.st) {
    case SV_IDLE:
        if (sv.n) {                    /* what was pressed, in order, one a frame: PLAY and REC go by what the engine says, and it has a block to say it. A SAVE among them starts another, and the rest wait again */
            const int b = sv.q[0];
            sv.n--;
            for (i = 0; i < sv.n; i++)
                sv.q[i] = sv.q[i + 1u];
            button_now(b & ~(int)Q_SEL, b & Q_SEL ? 1 : 0);
        } else if (autosave_due(now)) {
            save_begin(0);
        }
        break;
    case SV_FADE:
        if (!sv.manual && !autosave_due(now)) {
            en_duck(0);
            sv.st = SV_IDLE;
        } else if (en_ducked || now - sv.t > SAVE_LATE) {
#ifdef PLAT_HOST
            host_write_peak = host_blk_peak[0] > host_blk_peak[1] ? host_blk_peak[0] : host_blk_peak[1];
#endif
            save_write(now);
        }
        break;
    default:                           /* SV_RETURN: its notes when the sound is back */
        if (now - sv.t >= SAVE_BACK) {
            if (sv.manual && sv.ok)
                en_sys(EN_SYS_SAVE);
            sv.st = SV_IDLE;
        }
        break;
    }
}

void ui_panic(void)                    /* the engine ends the duck itself: here, nothing goes on writing or waiting */
{
    memset(&sv, 0, sizeof sv);
    sv.hushed = 1u;
}

/* ------------------------------------------------------ screen reader --- */
/* The screen as text for a screen reader on the computer (announce.c; fm1-reader): a page once it settles,
 * with its knobs; a knob's value as it turns; a help card as it comes up; the chord as it moves (queued: the
 * garden moves it by itself, every 20 s at the quickest); growing and resting from MIDI, and a MIDI clock
 * arriving or going; what a button did (say). WHERE (the reader asks, or it has just started listening) is
 * the whole screen in two: the page, its knobs and the header, then the garden (the chord, the scale).
 * Nothing is built while no reader listens. */
static struct {
    uint8_t view, help, ext, started, scene;
    int16_t val[4];
    uint32_t chord;                            /* the chord's name, hashed */
} sp;
static char sp_buf[ANN_MAX + 1];
static uint32_t sp_n;
/* the chord names (en_chord_name's, after the root) in words: "m" and "M7" differ only in capitals, which
 * the words file ignores */
static const char *const CHORD_Q[][2] = {
    {"", "major"}, {"m", "minor"}, {"7", "7"}, {"M7", "major 7"}, {"m7", "minor 7"}, {"6", "6"},
    {"m6", "minor 6"}, {"add9", "add 9"}, {"madd9", "minor add 9"}, {"aug", "augmented"}, {"dim", "diminished"},
    {"dim7", "diminished 7"}, {"m7b5", "half diminished"}, {"mM7", "minor major 7"}, {"sus2", "suspended 2"},
    {"sus4", "suspended 4"}, {"7sus4", "7 suspended 4"}, {"5", "5"}, {"M9", "major 9"}, {"9", "9"}, {"m9", "minor 9"},
    {"m11", "minor 11"}, {"6/9", "6 9"}, {"m6/9", "minor 6 9"}, {"9sus", "9 suspended"}, {"13sus", "13 suspended"},
};
#define NCHORD_Q (sizeof CHORD_Q / sizeof CHORD_Q[0])

static void sp_cat(const char *s)
{
    while (*s && sp_n < ANN_MAX)
        sp_buf[sp_n++] = *s++;
    sp_buf[sp_n] = 0;
}
static void sp_int(uint32_t v)
{
    char t[12];
    itoa_u(v, t);
    sp_cat(t);
}
static int sp_same(const char *a, const char *b)
{
    while (*a && *a == *b)
        a++, b++;
    return *a == *b;
}

static void sp_value(int k)                    /* a value as it is said: as shown */
{
    char t[16];
    knob_text(k, t);
    sp_cat(t);
}
static void sp_knob(int view, int i)           /* "Bloom 60": the name the page gives it, then its value */
{
    sp_cat(KNOB_LABEL[view][i]);
    sp_cat(" ");
    sp_value(VIEW_KNOB[view][i]);
}

static void sp_chord_q(int root, const char *q)   /* "E minor 7" */
{
    uint32_t i;
    sp_cat(EN_NOTE_NAME[root % 12]);
    sp_cat(" ");
    for (i = 0; i < NCHORD_Q; i++)
        if (sp_same(q, CHORD_Q[i][0])) {
            sp_cat(CHORD_Q[i][1]);
            return;
        }
    sp_cat(q);
}
static void sp_chord(void)                     /* the chord in the middle of the garden */
{
    char nm[16];
    const char *q = chord_now(nm), *r = EN_NOTE_NAME[en_chord_root % 12u];
    while (*r && *q == *r)
        q++, r++;
    sp_chord_q(en_chord_root % 12u, q);
}

/* the page: its name and its knobs; all: the header too (the piece, its key, the tempo, growing or resting) */
static void sp_page(int all)
{
    int i;
    sp_n = 0;
    sp_buf[0] = 0;
    sp_cat(VIEW_NAME[ui.view]);
    sp_cat(" page");
    for (i = 0; i < 4; i++) {
        sp_cat(i ? ", " : ": ");
        sp_knob(ui.view, i);
    }
    for (i = 0; i < NSOUND_KNOBS; i++)           /* and its sounds */
        if (SOUND_KNOB[i].view == ui.view && sound_said(i)) {
            sp_cat(SOUND_KNOB[i].spoken);
            sp_value(SOUND_KNOB[i].par);
        }
    if (!all)
        return;
    sp_cat(". Piece ");
    sp_cat(piece_name(proj.piece));
    sp_cat(", ");
    sp_cat(EN_NOTE_NAME[proj.par[P_ROOT]]);
    sp_cat(" ");
    sp_cat(mode_name());
    if (en_ext) {
        sp_cat(", tempo EXT");
    } else if (ui.view != V_GARDEN) {          /* (the Garden's first knob says it) */
        sp_cat(", tempo ");
        sp_int((uint32_t)proj.par[P_TEMPO]);
    }
    sp_cat(sp_tp.playing ? ", playing" : ", resting");
    if (ui.dirty)
        sp_cat(", not saved yet");
    if (ui.view == V_SETUP) {
        sp_cat(". CPU ");
        sp_int(plat_cpu_pct());
        sp_cat("%");
    }
    if (batt_words(batt())) {
        sp_cat(". ");
        sp_cat(batt_words(batt()));
    }
}

/* the garden: the chord and how far to the next, the scale from the root up ("F and F sharp": one letter's
 * two notes joined, so they are heard as two), the break's level */
static void sp_garden(void)
{
    uint32_t ap = pool_abs();
    int i, last = -1;
    sp_n = 0;
    sp_buf[0] = 0;
    sp_cat("Chord ");
    sp_chord();
    if (en_held) {
        sp_cat(", held");
    } else {
        sp_cat(", next chord ");
        sp_int((uint32_t)(fm_clampf(en_drift, 0.0f, 1.0f) * 100.0f + 0.5f));
        sp_cat("% of the way");
    }
    sp_cat(". Scale");
    for (i = 0; i < 12; i++) {
        int pc = (proj.par[P_ROOT] + i) % 12;
        if (ap >> pc & 1u) {
            sp_cat(last < 0 ? " " : EN_NOTE_NAME[last][0] == EN_NOTE_NAME[pc][0] ? " and " : ", ");
            sp_cat(EN_NOTE_NAME[pc]);
            last = pc;
        }
    }
    last = -1;
    for (i = 0; i < 12; i++) {                   /* and those of it that chords do not stand on */
        const int pc = (proj.par[P_ROOT] + i) % 12;
        if (degree_off(pc)) {
            sp_cat(last < 0 ? ". No chords on " : ", ");
            sp_cat(EN_NOTE_NAME[pc]);
            last = pc;
        }
    }
}

/* the arrangement as the screen has it: the mode, the section or the style, the layers on */
static void sp_arrange(void)
{
    static const char *const W[EN_NLAYERS] = {"keys", "melody", "bass", "drums", "hats"};
    int i, n = 0;
    sp_n = 0;
    sp_buf[0] = 0;
    sp_cat(en_mode == EN_HANDS ? "Hands, style " : "Auto, section ");
    sp_cat(en_mode == EN_HANDS ? EN_STYLE_NAME[en_style] : EN_SECTION_NAME[en_section]);
    for (i = 0; i < EN_NLAYERS; i++)
        if (layer_shown(LAYER_MARK[i].layer) == 2) {
            sp_cat(n++ ? ", " : ". Playing: ");
            sp_cat(W[LAYER_MARK[i].layer]);
        }
    if (!n)
        sp_cat(". Nothing playing");
}
/* the degrees' view: the notes chords stand on */
static void sp_degrees(void)
{
    char b[48];
    deg_names(b, sizeof b);
    sp_n = 0;
    sp_buf[0] = 0;
    sp_cat("Degrees: chords on ");
    sp_cat(b);
}
static void sp_break(void)
{
    sp_n = 0;
    sp_buf[0] = 0;
    sp_cat("BREAK ");
    sp_int(en_break_bars);
    sp_cat(en_break_bars == 1u ? " BAR" : " BARS");
}

/* a help card: its button, then its lines ("ENV button. THE KEYS. LEVEL, BLOOM, BRIGHT, NOTES") */
static void sp_line(const char *s)
{
    char p = sp_n ? sp_buf[sp_n - 1] : 0;
    if (*s == ' ')                             /* it goes on from the line before */
        s++;
    else if (p != ',' && p != ':')
        sp_cat(".");
    sp_cat(" ");
    sp_cat(s);
}
static void sp_help(void)
{
    const char *l[HELP_LINES];
    int n = help_lines(ui.help - 1, l), k;
    sp_n = 0;
    sp_buf[0] = 0;
    sp_cat(BTN_LABEL[ui.help - 1]);
    sp_cat(" button");
    for (k = 0; k < n; k++)
        sp_line(l[k]);
}

static void sp_piece_text(void)                /* "PIECE Departures, C# Ionian, tempo 60" */
{
    sp_n = 0;
    sp_buf[0] = 0;
    sp_cat("PIECE ");
    sp_cat(piece_name(proj.piece));
    sp_cat(", ");
    sp_cat(EN_NOTE_NAME[proj.par[P_ROOT]]);
    sp_cat(" ");
    sp_cat(mode_name());
    sp_cat(", tempo ");
    sp_int((uint32_t)proj.par[P_TEMPO]);
}
static void sp_piece(void)
{
    if (!ann_on())
        return;
    sp_piece_text();
    ann_say(sp_buf, 0, ANN_NOW);
    sp_said = 1;
}

static void sp_snap(void)
{
    int i;
    for (i = 0; i < 4; i++)
        sp.val[i] = (int16_t)knob_get(VIEW_KNOB[ui.view][i]);
    sp.view = ui.view;
    sp.help = ui.help;
    sp.ext = en_ext;
    sp.scene = scr.scene;
    sp.chord = chord_sig();
}

static void speak(void)
{
    uint32_t now = plat_ms(), c;
    int where = ann_where(), i, changed = -1;
    if (!ann_on()) {
        sp.started = 0;
        sp_said = 0;
        return;
    }
    if (!sp.started || where) {                       /* everything on the screen, in two */
        sp.started = 1;
        sp_snap();
        if (ui.help) {
            sp_help();
            ann_say(sp_buf, 0, ANN_NOW);
            sp_page(1);
        } else {
            sp_page(1);
            ann_say(sp_buf, 0, ANN_NOW);
            sp_garden();
        }
        ann_say(sp_buf, 0, 0);
        if (!ui.help) {
            sp_arrange();
            ann_say(sp_buf, 0, 0);
        }
        sp_said = 0;
        return;
    }
    if (sp_said) {                                    /* a button said what it did (and what it changed) */
        sp_said = 0;
        sp_snap();
        return;
    }
    if (en_playing != sp_tp.playing && now - sp_tp.play_t > 150u) {   /* MIDI start / stop */
        sp_tp.playing = en_playing;
        ann_say(en_playing ? "PLAYING" : "RESTING", 0, 0);
    }
    if (en_ext != sp.ext) {                           /* a MIDI clock arrived, or stopped */
        sp.ext = en_ext;
        sp_n = 0;
        sp_cat("Tempo ");
        if (en_ext)
            sp_cat("EXT");
        else
            sp_int((uint32_t)proj.par[P_TEMPO]);
        ann_say(sp_buf, 0, 0);
    }
    if (scr.scene != sp.scene) {                      /* the views a page does not say: the degrees, a break */
        sp.scene = scr.scene;
        if (scr.scene == SC_DEG) {
            sp_degrees();
            ann_say(sp_buf, 0, 0);
        } else if (scr.scene == SC_BREAK) {
            sp_break();
            ann_say(sp_buf, 0, 0);
        }
    }
    if (ui.help != sp.help) {                         /* a help card: at once */
        sp.help = ui.help;
        if (ui.help) {
            sp_help();
            ann_say(sp_buf, 0, ANN_NOW);
            return;
        }
    }
    if (ui.view != sp.view) {                         /* a new page: said when it settles */
        sp_snap();
        sp_page(0);
        ann_later(sp_buf, ANN_NOW, 200u, now);
        return;
    }
    for (i = 0; i < 4; i++) {                         /* a knob turning */
        int v = knob_get(VIEW_KNOB[ui.view][i]);
        if (v != sp.val[i]) {
            sp.val[i] = (int16_t)v;
            if (changed < 0)
                changed = i;
        }
    }
    if (changed >= 0) {
        sp_n = 0;
        sp_knob(ui.view, changed);
        ann_later(sp_buf, ANN_NOW, 120u, now);
    }
    c = chord_sig();
    if (c != sp.chord) {                              /* the chord moved: queued, after what is being said */
        sp.chord = c;
        sp_n = 0;
        sp_cat("Chord ");
        sp_chord();
        ann_say(sp_buf, 0, 0);
    }
}

#ifdef PLAT_HOST
/* every page, knob value, help card, chord and message as a screen reader gets them (the "speakdump"
 * command): KIND <tab> the text sent (fm1-reader/tools/audit.mjs puts it in words with web/words/ and checks it) */
static void sp_print(const char *kind, const char *t) { printf("%s\t%s\n", kind, t); }
static int unfit_one(const felucca_font_t *f, const char *s, int32_t room, const char *what)
{
    if (text_w(f, s) <= room)
        return 0;
    printf("unfit: %s \"%s\": %d in %d\n", what, s, (int)text_w(f, s), (int)room);
    return 1;
}
static int unfit_msg(const char *a, const char *b)   /* a message: in the header, its two parts 8 apart */
{
    char t[48];
    int w = text_w(&FONT_B, a) + (b ? 8 + text_w(&FONT_B, b) : 0);
    if (w <= 224)                                     /* inside the header's pane */
        return 0;
    msg_text(a, b, t);
    printf("unfit: message \"%s\": %d in 224\n", t, w);
    return 1;
}
#define KNOB_ROOM 58                                  /* a cell is 60 wide */
#define CARD_ROOM 200                                 /* a card's text: its panel is 208 */
static int ui_unfit(void)                             /* the host's "unfit": what does not fit its place on the screen */
{
    const project_t keep = proj;
    int n = 0, v, i, x, lo, hi;
    char t[16];
    for (v = 0; v < NVIEWS; v++) {
        n += unfit_one(&FONT_XS, VIEW_NAME[v], 60, "page");
        for (i = 0; i < 4; i++) {
            const int k = VIEW_KNOB[v][i];
            caps(t, KNOB_LABEL[v][i], sizeof t);
            n += unfit_one(&FONT_XS, t, KNOB_ROOM, "label");
            knob_range(k, &lo, &hi);
            for (x = lo; x <= hi; x++) {
                if (k < P_NPARAMS)
                    proj.par[k] = (int16_t)x;
                else
                    proj.leds = (uint8_t)x;
                knob_text(k, t);
                n += unfit_one(&FONT_B, t, KNOB_ROOM, "value");
            }
            proj = keep;
        }
    }
    for (i = 0; i < NB; i++) {
        const char *l[HELP_LINES];
        const int c = help_lines(i, l);
        for (x = 0; x < c; x++)
            n += unfit_one(&FONT_XS, l[x] + (l[x][0] == ' '), CARD_ROOM, "help");
    }
    for (x = 0; x < NPIECES; x++) {                   /* the card: the name in the large face, its line */
        n += unfit_one(&FONT_L, piece_name(x), 220, "piece");
        n += unfit_one(&FONT_XS, PIECE_LINE[x], 220, "piece's line");
        n += unfit_one(&FONT_B, piece_name(x), 150, "header's piece");
    }
    for (x = 0; x < EN_NSECTIONS; x++)                /* in the pane of the chord, under it */
        n += unfit_one(&FONT_XS, EN_SECTION_NAME[x], CHORD_W - 6, "section");
    for (x = 0; x < EN_NSTYLES; x++)
        n += unfit_one(&FONT_XS, EN_STYLE_NAME[x], CHORD_W - 6, "style");
    {                                                 /* Setup's reading at its longest, with the page's name and what is on the right */
        char c[32];
        const uint32_t k[4] = {scr.r_cpu, scr.r_max, scr.r_x, scr.r_s};
        scr.r_cpu = 100u, scr.r_max = 999u * (APP_BLOCK * 1000000u / 44100u) / 100u, scr.r_x = 999u, scr.r_s = 999u;
        setup_read(c);
        n += unfit_one(&FONT_XS, c, 231 - BATT_W - 6 - 9 - 22 - 6 - (9 + text_w(&FONT_B, "Setup") + 5), "Setup's reading");
        scr.r_cpu = k[0], scr.r_max = k[1], scr.r_x = k[2], scr.r_s = k[3];
    }
    for (x = -2; x <= 2; x++) {
        en_param_text(P_MOCT, x, t);
        n += unfit_msg("MELODY OCTAVE", t);
    }
    for (i = 0; i < NSOUND_KNOBS; i++) {              /* a sound: its message; its place in the header, with the page's name, the longest chord that is, and what is on the right */
        const int p = SOUND_KNOB[i].par;
        for (x = 0; x <= en_param_info(p)->hi; x++) {
            en_param_text(p, x, t);
            n += unfit_msg(SOUND_KNOB[i].say, t);
        }
    }
    for (v = 0; v < NVIEWS; v++) {
        char c[24];
        const project_t k = proj;
        for (x = 0; x < 7 * 2 * EN_ARP_NPATTERNS * 3; x++) {
            int16_t *q[7] = {&proj.par[P_KEYSND], &proj.par[P_PADSND], &proj.par[P_BASSSND], &proj.par[P_MELSND], &proj.par[P_MELMODE], &proj.par[P_ARPPAT], &proj.par[P_ARPSKIP]};
            *q[0] = (int16_t)(x % 4), *q[1] = (int16_t)(x / 4 % 4), *q[2] = (int16_t)(x % 4), *q[3] = (int16_t)(x % 7), *q[4] = (int16_t)(x / 7 % 2), *q[5] = (int16_t)(x / 14 % EN_ARP_NPATTERNS), *q[6] = (int16_t)(x / (14 * EN_ARP_NPATTERNS) * 50);
            if (page_sound(v, c))
                n += unfit_one(&FONT_XS, c, HEADER_SOUND_ROOM(v), "the header's sound");
        }
        proj = k;
    }
    n += unfit_msg("THE ROOT STAYS", "(SETUP)");
    n += unfit_msg("3 NOTES", "AT LEAST");
    n += unfit_msg("2 DEGREES", "AT LEAST");
    n += unfit_msg("NOT IN", "THE SCALE");
    n += unfit_msg("NEXT SECTION", "ON THE BAR");
    n += unfit_msg("NEXT SECTION", "WHEN IT PLAYS");
    n += unfit_msg("BREAK", "ON THE BAR");
    n += unfit_msg("NO DRUMS", "NO BREAK");
    for (x = 0; x < EN_NLAYERS; x++)
        n += unfit_msg(LAYER_BTN[x].name, "OFF");
    for (x = 0; x < 12; x++)
        n += unfit_msg("NO CHORDS ON", EN_NOTE_NAME[x]);
    return n;
}
static void speak_dump(void)
{
    static const char *const MSG[][2] = {{"UPDATE MODE IN", "3.."}, {"UPDATE MODE", "CANCELLED"},
                                         {"NEXT SECTION", "ON THE BAR"}, {"NEXT SECTION", "WHEN IT PLAYS"}, {"BREAK", "ON THE BAR"},
                                         {"RESTING", 0}, {"PLAYING", 0}, {"HOLD", "ON"}, {"HOLD", "OFF"}, {"SAVED", 0},
                                         {"KEYS", "ON"}, {"KEYS", "OFF"}, {"MELODY", "ON"}, {"MELODY", "OFF"}, {"BASS", "ON"}, {"BASS", "OFF"},
                                         {"DRUMS", "ON"}, {"DRUMS", "OFF"}, {"HATS", "ON"}, {"HATS", "OFF"}, {"HANDS", 0}, {"AUTO", 0}};
    static const char *const WARN[][2] = {{"THE ROOT STAYS", "(SETUP)"}, {"3 NOTES", "AT LEAST"},
                                          {"NOT SAVED", "FLASH ERROR"}, {"2 DEGREES", "AT LEAST"}, {"NOT IN", "THE SCALE"},
                                          {"NO DRUMS", "NO BREAK"}};
    project_t keep = proj;
    uint8_t keep_view = ui.view, keep_help = ui.help, keep_dirty = ui.dirty, keep_play = sp_tp.playing;
    uint8_t keep_ext = en_ext, keep_held = en_held;
    int v, i, x, lo, hi;
    char t[48];
    for (v = 0; v < NVIEWS; v++) {
        ui.view = (uint8_t)v;
        sp_page(0);
        sp_print("SCREEN", sp_buf);
        for (i = 0; i < 4; i++) {
            int k = VIEW_KNOB[v][i];
            knob_range(k, &lo, &hi);
            for (x = lo; x <= hi; x++) {
                if (k < P_NPARAMS)
                    proj.par[k] = (int16_t)x;
                else
                    proj.leds = (uint8_t)x;
                sp_n = 0;
                sp_knob(v, i);
                sp_print("KNOB", sp_buf);
            }
            proj = keep;
        }
    }
    for (i = 0; i < NSOUND_KNOBS; i++)                /* the sounds, as the page says them; as a turn says them */
        for (x = 0; x <= en_param_info(SOUND_KNOB[i].par)->hi; x++) {
            char o[16];
            proj.par[SOUND_KNOB[i].par] = (int16_t)x;
            ui.view = SOUND_KNOB[i].view;
            sp_page(0);
            sp_print("SCREEN", sp_buf);
            en_param_text(SOUND_KNOB[i].par, x, o);
            msg_text(SOUND_KNOB[i].say, o, t);
            sp_print("MSG", t);
            proj = keep;
        }
    ui.view = keep_view;
    for (x = 0; x < 8; x++) {                         /* WHERE: growing, resting, unsaved, a clock, held, custom */
        ui.view = (uint8_t)(x * 3 % NVIEWS);
        sp_tp.playing = (uint8_t)(x & 1);
        ui.dirty = (uint8_t)(x >> 1 & 1);
        en_ext = (uint8_t)(x == 3);
        en_held = (uint8_t)(x == 5);
        proj.pool = x == 6 ? 0xFFFu : x == 7 ? 0x91u : keep.pool;
        proj.mask = x == 4 ? (uint16_t)(EN_MASK_ALL & ~(1u << 4 | 1u << 9)) : (uint16_t)EN_MASK_ALL;
        proj.par[P_ROOT] = (int16_t)(x * 5 % 12);
        sp_page(1);
        sp_print("WHERE", sp_buf);
        sp_garden();
        sp_print("WHERE", sp_buf);
        sp_arrange();
        sp_print("WHERE", sp_buf);
    }
    proj = keep;
    ui.dirty = keep_dirty;
    sp_tp.playing = keep_play;
    en_ext = keep_ext;
    en_held = keep_held;
    ui.view = keep_view;
    sp_degrees();
    sp_print("MSG", sp_buf);
    for (x = 1; x <= 4; x++) {
        const uint8_t b = en_break_bars;
        en_break_bars = (uint8_t)x;
        sp_break();
        sp_print("MSG", sp_buf);
        en_break_bars = b;
    }
    for (i = 0; i < NB; i++) {
        const char *l[HELP_LINES];
        if (!help_lines(i, l))
            continue;
        ui.help = (uint8_t)(i + 1);
        sp_help();
        sp_print("HELP", sp_buf);
    }
    ui.help = keep_help;
    for (x = 0; x < 12; x++)
        for (i = 0; i < (int)NCHORD_Q; i++) {
            sp_n = 0;
            sp_cat("Chord ");
            sp_chord_q(x, CHORD_Q[i][0]);
            sp_print("CHORD", sp_buf);
        }
    for (i = 0; i < (int)(sizeof MSG / sizeof MSG[0]); i++) {
        msg_text(MSG[i][0], MSG[i][1], t);
        sp_print("MSG", t);
    }
    for (i = 0; i < (int)(sizeof WARN / sizeof WARN[0]); i++) {
        msg_text(WARN[i][0], WARN[i][1], t);
        sp_print("WARN", t);
    }
    for (x = 0; x < 12; x++) {
        msg_text("ADDED", EN_NOTE_NAME[x], t);
        sp_print("MSG", t);
        msg_text("REMOVED", EN_NOTE_NAME[x], t);
        sp_print("MSG", t);
        msg_text("CHORDS ON", EN_NOTE_NAME[x], t);
        sp_print("MSG", t);
        msg_text("NO CHORDS ON", EN_NOTE_NAME[x], t);
        sp_print("MSG", t);
    }
    for (x = 0; x < EN_NMODES; x++) {
        msg_text("MODE", EN_MODE_NAME[x], t);
        sp_print("MSG", t);
    }
    for (x = -2; x <= 2; x++) {
        char o[8];
        en_param_text(P_MOCT, x, o);
        msg_text("MELODY OCTAVE", o, t);
        sp_print("MSG", t);
    }
    for (x = 150; x <= 180; x += 15) {
        char o[8];
        itoa_u((uint32_t)x, o);
        msg_text("TEMPO", o, t);
        sp_print("MSG", t);
    }
    for (x = 0; x < NPIECES; x++) {
        project_piece(x);
        sp_piece_text();
        sp_print("MSG", sp_buf);
        msg_text("RESET", piece_name(x), t);
        sp_print("MSG", t);
    }
    proj = keep;
    sp_print("MSG", "Tempo EXT");
    sp_print("MSG", "Tempo 170");
    for (x = 0; x <= 4; x++)                          /* the battery, at the end of WHERE's first part */
        sp_print("WHERE", batt_words(x));
}
#endif

void ui_init(void)
{
    const uint8_t dirty = ui.dirty;              /* an update cancelled comes here with the project as it was: what was not saved still is not. (A power-on: zero, like all the RAM) */
    memset(&ui, 0, sizeof ui);
    memset(&sv, 0, sizeof sv);
    ui.dirty = dirty;
    ui.act_t = plat_ms();                        /* (this start is the last touch: no autosave before it has grown, while the engine still says it is not playing) */
    ui.touched = -1;
    memset(&scr, 0, sizeof scr);
    memset(el, 0, sizeof el);
    scr.key = 0xFFFFFFFFu;
    scr.title_until = plat_ms() + TITLE_MS;
    scr.act_boot = ui.act_t;
    lcd_fill(0, 0, 240, 240, K_DARK);
#if defined PLAT_HOST && !defined PLAT_WEB
    host_pieces = 0u;
#endif
    en_seed(seed_now());
    en_duck(0);                                  /* (whatever was under way when this was called) */
    en_restore();                                /* it grows: from the start at power-on, from where it was after a panic */
#if defined PLAT_HOST && !defined PLAT_WEB
    if (!getenv("AQ1_NO_BOOT_NOTES"))            /* the simulator's, to compare a render with one from before they were */
#endif
    en_sys(EN_SYS_BOOT);                         /* (after a panic nothing sings: that is an update come back, not a start) */
    sp_tp.playing = 1;
    sp_tp.play_t = plat_ms();
}

void ui_frame(void)
{
    input();
    save_tick();
    scr_frame();
    leds();
    speak();
    ann_tick(plat_ms());
    ui.frame++;
}

void ui_input_only(void) { input(); }
