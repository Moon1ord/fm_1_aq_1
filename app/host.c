/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal */
/* AQ-1 in fm1-base's host simulator (host/plat_host.c, which includes app.c and so this): AMB-1's checks for
 * "expect", its command, a reboot and the screen reader's dump.
 *
 * AMB-1's checks: playing frozen view piece pool dirty sixteenths ext pad_level bass_level chordroot chordN loopnotes
 * parN battery section style level mask keyslit help ringN (fm1-base's own: store_writes clocks_out midi_out peak_db_max peak_db_min); unfit: the labels,
 * values, help lines and messages wider than their places. Its commands:
 * storeamb (what AMB-1 leaves in the store: its magic and format, nothing of ours); storeold (the project as it is, whole, but
 * for the format before this one); battery N (the battery read as N,
 * plat.h's plat_battery: -1..4; "battery off" the simulator's own, full); seed N (the musical seed, from the next choice on;
 * until told it is EN_HARM_SEED); setpar P V (parameter P to V, as a knob would); style NAME; energy N; breaks N; trace FILE
 * (a line for each bar: step section level character break's bars; a line for each chord: "chord" step name bars character key tones bass, the last two above the key)
 * and trace off. */
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
    else if (!strcmp(what, "frozen"))
        g = en_frozen;
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
    } else if (!strcmp(what, "keyslit")) {        /* the keys' lights */
        g = (int)keys_lit();
    } else if (!strcmp(what, "help")) {           /* a help card is up: its button + 1 */
        g = ui.help;
    } else if (!strncmp(what, "ring", 4)) {       /* pitch class N on the ring: 0 out of the scale, 1 in it, 2 in it and no chords on it */
        const int pc = atoi(what + 4);
        g = pool_abs() >> pc & 1u ? 1 + degree_off(pc) : 0;
    } else if (!strcmp(what, "unfit")) {          /* what does not fit its place on the screen */
        g = ui_unfit();
    } else if (!strncmp(what, "par", 3))          /* parN: parameter N (amb.h P_*) */
        g = proj.par[atoi(what + 3)];
    else
        return 0;
    *got = g;
    return 1;
}

int app_host_cmd(const char *cmd, const char *a, const char *b, const char *c, const char *d)
{
    (void)c, (void)d;
    if (!strcmp(cmd, "battery")) {               /* battery N: plat_battery as N (-1..4), "battery off" its own */
        batt_host = (int8_t)(strcmp(a, "off") ? atoi(a) : -2);
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
    return 0;
}

void app_host_reboot(void) { memset(&proj, 0, sizeof proj); }

void app_speak_dump(void) { speak_dump(); }
