/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal */
/* AQ-1 in fm1-base's host simulator (host/plat_host.c, which includes app.c and so this): AMB-1's checks for
 * "expect", its command, a reboot and the screen reader's dump.
 *
 * AMB-1's checks: playing held silent midionN keysdown bellson bassdown keyson breakbars barpos stepofbar bars chordhash view piece pool dirty sixteenths ext pad_level bass_level chordroot chordN loopnotes
 * parN battery section style level mask enmask keyslit keysmid handset kplay melother help ringN mode arrange enagree brkshown brkquiet brkwrong absent sysnotes sysnote ducked saving writepeak mutekeys mutemel mutebass mutedrums mutehats gainN meterN dimN litN screen eye smile blinks wipes wiping drawrows drawbytes drawpx artbytes stale (fm1-base's own: store_writes clocks_out midi_out peak_db_max peak_db_min); unfit: the labels,
 * values, help lines and messages wider than their places. Its commands:
 * storeamb (what AMB-1 leaves in the store: its magic and format, nothing of ours); storeold (the project as it is, whole, but
 * for the format before this one); storeshort (a save as the format before this one was laid out: its magic, its format, its length, a parameter less to a piece); storebad N P V (the project as it is, whole,
 * in this format, but piece N's parameter P is V; N -1: the piece playing's); storebadother N V (likewise, its melody's mode in the other arrangement); battery N (the battery read as N,
 * plat.h's plat_battery: -1..4; "battery off" the simulator's own, full); seed N (the musical seed, from the next choice on;
 * until told it is EN_HARM_SEED); melhear T (a statement in this timbre at the next chance: an audition); setpar P V (parameter P to V, as a knob would); style NAME; energy N; breaks N; keysgain G (the keys alone down, against the pad: a listening file's, the panel has no such thing); trace FILE
 * (a line for each bar: step section level character break's bars; a line for each chord: "chord" step name bars character key tones bass, the last two above the key)
 * and trace off; drawreset (the screen's counters); panic and uiinit (what the platform does around an update); overload (blocks too heavy, twice); flood N (more values than the engine's queue holds, all at once). */
#include <stdio.h>
#include <strings.h>
static FILE *trace_f;
static void trace_bar(uint32_t step, int section, int level, int character, int break_bars)
{
    if (trace_f)
        fprintf(trace_f, "%u %d %d %d %d\n", step, section, level, character, break_bars);
}
static void trace_chord(uint32_t step, const en_gchord_t *g, int character)
{
    if (trace_f) {
        char nm[24];
        fprintf(trace_f, "chord %u %s %d %d %d %03x %d\n", step, en_gchord_name(g, nm), g->bars, character, g->ch.tonic, g->ch.tones, g->ch.bass);
    }
}
static void trace_mel(uint32_t step, const en_phrase_t *p)
{
    if (trace_f) {
        int i;
        fprintf(trace_f, "mel %u %s %c%s %04x %d", step, EN_TIMBRE_NAME[p->timbre], "SARC"[p->role & 3], p->recalled ? "*" : "", p->sig, p->rest);
        for (i = 0; i < p->n; i++)
            fprintf(trace_f, " %d:%d:%d:%d:%d", p->note[i].step, p->note[i].note, p->note[i].len, p->note[i].vel, p->note[i].echo);
        fprintf(trace_f, "\n");
    }
}
static void host_par(int p, int v)
{
    if ((unsigned)p < P_NPARAMS) {
        proj.par[p] = (int16_t)v;
        en_set(p, v);
    }
}

int app_expect(const char *what, int *got)
{
    int g;
    if (!strcmp(what, "playing"))
        g = en_playing;
    else if (!strcmp(what, "mode"))               /* the engine's arrangement, and the project's */
        g = en_mode;
    else if (!strcmp(what, "arrange"))
        g = proj.arrange;
    else if (!strcmp(what, "enagree")) {          /* what the project and the engine disagree on: parameters, the scale, the degrees, the arrangement */
        int i;
        g = (proj.pool != en_test_pool()) + (proj.mask != en_test_degrees()) + (proj.arrange != en_mode);
        for (i = 0; i < P_NPARAMS; i++)
            g += proj.par[i] != en_test_par(i);
    }
    else if (!strcmp(what, "sysnotes"))           /* the system's voice: its notes since the engine started, the last */
        g = (int)en_sys_notes;
    else if (!strcmp(what, "sysnote"))
        g = en_sys_note;
    else if (!strcmp(what, "ducked"))             /* the volume's duck: at zero */
        g = en_ducked;
    else if (!strcmp(what, "saving"))             /* a save: 0 none, 1 fading, 2 coming back */
        g = sv.st;
    else if (!strcmp(what, "writepeak"))          /* the most in the two blocks before the last write; -1 none yet */
        g = host_write_peak;
    else if (!strcmp(what, "absent"))             /* Auto: the layers the arranger has absent on the next bar, as bits */
        g = en_layers_absent;
    else if (!strcmp(what, "mutekeys"))
        g = en_mute_keys;
    else if (!strcmp(what, "mutemel"))
        g = en_mute_mel;
    else if (!strcmp(what, "mutebass"))
        g = en_mute_bass;
    else if (!strcmp(what, "mutedrums"))
        g = en_mute_drums;
    else if (!strcmp(what, "mutehats"))
        g = en_mute_hats;
    else if (!strncmp(what, "gain", 4))           /* gainN: layer N's gain, % */
        g = (int)(en_test_mute_gain(atoi(what + 4)) * 100.0f);
    else if (!strncmp(what, "meter", 5)) {        /* meterN: the meter N's reading, % */
        float lv[4];
        meters_read(lv);
        g = (int)(lv[atoi(what + 5) & 3] * 100.0f);
    } else if (!strncmp(what, "dim", 3))          /* dimN, litN: button N (plat.h B_*) glowing, lit */
        g = (int)(dim_btn >> atoi(what + 3) & 1u);
    else if (!strncmp(what, "lit", 3))
        g = (int)(lit_btn >> atoi(what + 3) & 1u);
    else if (!strcmp(what, "screen"))             /* the scene up (ui.c SC_*): 0 title 1 home 2 a page 3 break 4 card 5 save 6 rest 7 hold 8 degrees 9 help */
        g = scr.scene;
    else if (!strcmp(what, "eye"))                /* the face: 0 closed, 1 half, 2 open */
        g = scr.eye;
    else if (!strcmp(what, "smile"))
        g = mouth_smiling(scr.scene, scr.now);
    else if (!strcmp(what, "blinks"))             /* blinks so far */
        g = (int)scr.blinks;
    else if (!strcmp(what, "readsheds"))          /* Setup's reading as shown: its S */
        g = (int)scr.r_s;
    else if (!strcmp(what, "brkshown"))           /* frames since drawreset: of the break's frame */
        g = (int)scr.brk_shown;
    else if (!strcmp(what, "brkquiet"))           /* of a break nobody hears: resting, the drums muted or at level 0 */
        g = (int)scr.brk_quiet;
    else if (!strcmp(what, "brkwrong"))           /* of the frame over one of those */
        g = (int)scr.brk_wrong;
    else if (!strcmp(what, "wipes"))              /* whole pictures so far */
        g = (int)scr.wipes;
    else if (!strcmp(what, "wiping"))             /* rows of a picture to go */
        g = scr.wipe_left;
    else if (!strcmp(what, "drawrows"))           /* since drawreset, the most in one frame: rows decoded from the flash, its bytes, pixels sent */
        g = (int)scr.p_rows;
    else if (!strcmp(what, "drawbytes"))
        g = (int)scr.p_bytes;
    else if (!strcmp(what, "drawpx"))
        g = (int)scr.p_px;
    else if (!strcmp(what, "artbytes"))           /* since drawreset, all the bytes of art read */
        g = (int)scr.t_bytes;
    else if (!strcmp(what, "stale"))              /* the screen against the same scene painted whole: the pixels that differ */
        g = scr_stale();
    else if (!strcmp(what, "held"))
        g = en_held;
    else if (!strcmp(what, "silent"))             /* since a panic */
        g = en_test_silent();
    else if (!strncmp(what, "midion", 6))         /* midionN: the notes MIDI has said are down on channel N */
        g = midi_gate_ch((uint32_t)(atoi(what + 6) - 1)) >= 0 ? midi_owed.n[midi_gate_ch((uint32_t)(atoi(what + 6) - 1))] : 0;
    else if (!strcmp(what, "padsent"))
        g = en_test_pad_sent();
    else if (!strcmp(what, "keysdown"))           /* what the engine thinks they are: the keys, the motifs, the bass */
        g = en_test_keys_down();
    else if (!strcmp(what, "bellson"))
        g = en_test_bells_on();
    else if (!strcmp(what, "bassdown"))
        g = en_test_bass_down();
    else if (!strcmp(what, "keyson"))             /* key voices sounding, or fading */
        g = en_test_keys_on();
    else if (!strcmp(what, "breakbars"))          /* this bar is a break of so many bars */
        g = en_break_bars;
    else if (!strcmp(what, "barpos"))             /* the next step's place in its phrase, in bars; on the bar: step / 16 */
        g = (int)(en_test_song() / 16u % EN_ARR_PHRASE);
    else if (!strcmp(what, "stepofbar"))
        g = (int)(en_test_song() % 16u);
    else if (!strcmp(what, "bars"))               /* the song's next step, in bars */
        g = (int)(en_test_song() / 16u);
    else if (!strcmp(what, "chordhash"))          /* the chord, as a number */
        g = en_chord[0] | en_chord[1] << 7 | en_chord[2] << 14 | en_chord[3] << 21;
    else if (!strcmp(what, "view"))
        g = ui.view;
    else if (!strcmp(what, "piece"))
        g = proj.piece;
    else if (!strcmp(what, "pool"))               /* the scale, relative to the root (hex ok) */
        g = proj.pool;
    else if (!strcmp(what, "battery"))
        g = batt();
    else if (!strcmp(what, "dirty"))
        g = ui.dirty;
    else if (!strcmp(what, "hits"))
        g = (int)en_hits;
    else if (!strcmp(what, "section"))
        g = en_section;
    else if (!strcmp(what, "style"))
        g = en_style;
    else if (!strcmp(what, "level"))
        g = en_drum_level;
    else if (!strcmp(what, "seed"))               /* the seed the engine is following */
        g = (int)en_test_seed();
    else if (!strcmp(what, "sixteenths"))
        g = (int)en_bars;
    else if (!strcmp(what, "ext"))
        g = en_ext;
    else if (!strcmp(what, "pad_level"))
        g = (int)(en_pad_level * 100.0f);
    else if (!strcmp(what, "bass_level"))
        g = (int)(en_bass_level * 100.0f);
    else if (!strcmp(what, "chordroot"))
        g = en_chord_root;
    else if (!strncmp(what, "chord", 5))          /* chordN: pad voice N's note */
        g = en_chord[atoi(what + 5) & 3];
    else if (!strcmp(what, "loopnotes")) {        /* the notes in all the loops */
        int l, i;
        g = 0;
        for (l = 0; l < EN_NLOOP; l++)
            for (i = 0; i < EN_LOOP_MAX; i++)
                g += en_loop_note[l][i] >= 0;
    } else if (!strcmp(what, "mask")) {           /* the degrees, relative to the root (hex ok) */
        g = proj.mask;
    } else if (!strcmp(what, "enmask")) {         /* and as the engine has it */
        g = (int)en_test_degrees();
    } else if (!strcmp(what, "keyslit")) {        /* the keys' lights */
        g = (int)keys_lit();
    } else if (!strcmp(what, "keysmid")) {        /* those at the middle level */
        g = (int)(keys_mid() & ~keys_lit());
    } else if (!strcmp(what, "handset")) {        /* the keys the engine is playing */
        g = (int)en_hand_set;
    } else if (!strcmp(what, "kplay")) {          /* those the panel has as played */
        g = (int)ui.kplay;
    } else if (!strcmp(what, "melother")) {       /* the melody's mode in the other arrangement */
        g = proj.mel_other;
    } else if (!strcmp(what, "help")) {           /* a help card is up: its button + 1 */
        g = ui.help;
    } else if (!strncmp(what, "ring", 4)) {       /* pitch class N on the ring: 0 out of the scale, 1 in it, 2 in it and no chords on it */
        const int pc = atoi(what + 4);
        g = pool_abs() >> pc & 1u ? 1 + degree_off(pc) : 0;
    } else if (!strcmp(what, "unfit")) {          /* what does not fit its place on the screen */
        g = ui_unfit();
    } else if (!strncmp(what, "soundis=", 8)) {   /* soundis=TEXT: 1 if the page's sounds, as its header says them, are TEXT */
        char c[40];
        c[0] = 0;
        page_sound(ui.view, c);
        g = !strcmp(c, what + 8);
    } else if (!strncmp(what, "par", 3))          /* parN: parameter N (amb.h P_*) */
        g = proj.par[atoi(what + 3)];
    else
        return 0;
    *got = g;
    return 1;
}

int app_host_cmd(const char *cmd, const char *a, const char *b, const char *c, const char *d)
{
    (void)d;
    if (!strcmp(cmd, "battery")) {               /* battery N: plat_battery as N (-1..4), "battery off" its own */
        batt_host = (int8_t)(strcmp(a, "off") ? atoi(a) : -2);
        return 1;
    }
    if (!strcmp(cmd, "drawreset")) {             /* the screen's counters afresh */
        scr.p_rows = scr.p_bytes = scr.p_px = scr.t_bytes = 0u;
        scr.brk_shown = scr.brk_quiet = scr.brk_wrong = 0u;
        return 1;
    }
    if (!strcmp(cmd, "panic")) {                 /* what the platform does before an update */
        app_panic();
        return 1;
    }
    if (!strcmp(cmd, "uiinit")) {                /* and after it */
        app_ui_init();
        return 1;
    }
    if (!strcmp(cmd, "overload")) {              /* two heavy blocks in a row */
        app_overload();
        app_overload();
        return 1;
    }
    if (!strcmp(cmd, "flood")) {                  /* flood N: N values at the engine at once, no render between, none of them the project's: more than its queue holds */
        int i;
        for (i = 0; i < atoi(a); i++)
            en_set(P_BLOOM, (proj.par[P_BLOOM] + 1 + i % 2) % 100);
        return 1;
    }
    if (!strcmp(cmd, "seed")) {
        en_seed((uint32_t)strtoul(a, 0, 0));
        return 1;
    }
    if (!strcmp(cmd, "setpar")) {
        host_par(atoi(a), atoi(b));
        return 1;
    }
    if (!strcmp(cmd, "energy")) {
        host_par(P_ENERGY, atoi(a));
        return 1;
    }
    if (!strcmp(cmd, "breaks")) {
        host_par(P_BREAKS, atoi(a));
        return 1;
    }
    if (!strcmp(cmd, "style")) {
        int s;
        for (s = 0; s < EN_NSTYLES; s++)
            if (!strcasecmp(a, EN_STYLE_NAME[s])) {
                host_par(P_STYLE, s);
                return 1;
            }
        return 0;
    }
    if (!strcmp(cmd, "trace")) {
        if (trace_f)
            fclose(trace_f);
        trace_f = strcmp(a, "off") ? fopen(a, "w") : 0;
        en_trace = trace_f ? trace_bar : 0;
        en_trace_chord = trace_f ? trace_chord : 0;
        en_trace_mel = trace_f ? trace_mel : 0;
        return 1;
    }
    if (!strcmp(cmd, "keysgain")) {
        en_test_keys_gain = (float)atof(a);
        return 1;
    }
    if (!strcmp(cmd, "melhear")) {                /* melhear T: a statement in timbre T at the next chance; -1 back to its own */
        en_test_mel_hear(atoi(a));
        return 1;
    }
    if (!strcmp(cmd, "storeamb")) {               /* what AMB-1 leaves in the store: its magic, its format */
        static const uint32_t o[2] = {0x314F4E45u /* "ENO1" */, 4u};
        plat_store_save(OBJ_PROJ, o, sizeof o);
        return 1;
    }
    if (!strcmp(cmd, "storeold")) {               /* our magic, our size, every value in range: only the format is old */
        static project_t old;
        project_save();
        old = proj;
        old.format = PROJ_FORMAT - 1u;
        plat_store_save(OBJ_PROJ, &old, sizeof old);
        return 1;
    }
    if (!strcmp(cmd, "storeshort")) {
        static project_t old;
        project_save();
        old = proj;
        old.format = PROJ_FORMAT - 1u;
        plat_store_save(OBJ_PROJ, &old, sizeof old - (size_t)(1 + NPIECES) * sizeof(int16_t));
        return 1;
    }
    if (!strcmp(cmd, "storebadother")) {          /* storebadother N V: piece N's melody's mode in the other arrangement is V; N -1: the piece playing's */
        static project_t bad;
        const int n = atoi(a);
        project_save();
        bad = proj;
        if (n < 0)
            bad.mel_other = (uint8_t)atoi(b);
        else
            bad.slot[n].mel_other = (uint8_t)atoi(b);
        plat_store_save(OBJ_PROJ, &bad, sizeof bad);
        return 1;
    }
    if (!strcmp(cmd, "storebad")) {
        static project_t bad;
        const int n = atoi(a);
        project_save();
        bad = proj;
        if (n < 0)
            bad.par[atoi(b)] = (int16_t)atoi(c);
        else
            bad.slot[n].par[atoi(b)] = (int16_t)atoi(c);
        plat_store_save(OBJ_PROJ, &bad, sizeof bad);
        return 1;
    }
    return 0;
}

void app_host_reboot(void)
{
    memset(&proj, 0, sizeof proj);
    memset(&midi_owed, 0, sizeof midi_owed);
    owed = 0u;
    ui.dirty = 0u;
}

void app_speak_dump(void) { speak_dump(); }
