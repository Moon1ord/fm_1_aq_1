/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal */
/* AMB-1 on fm1-base: the app's unity build (compiled into fm1-base's, after the platform) and the glue between
 * fm1-base's contract (app.h) and AMB-1's parts: the project (project.c), the UI (ui.c) and the engine (dsp/amb.c,
 * its own -O2 unit). */
#include "project.c"
#include "ui.c"

/* the engine's MIDI out (clock, notes when MIDI out is on): to USB */
#ifdef PLAT_HOST
static uint16_t ledger[4];                       /* the simulator's count of the notes MIDI has said are down: channels 1..4 (host.c) */
static void ledger_note(uint32_t st, uint32_t d1, uint32_t d2)
{
    const uint32_t c = st & 15u;
    if (c < 4u && (st & 0xF0u) == 0x90u && d2)
        ledger[c]++;
    else if (c < 4u && (st & 0xF0u) == 0x80u && ledger[c])
        ledger[c]--;
    else if (c < 4u && (st & 0xF0u) == 0xB0u && d1 == 123u)
        ledger[c] = 0u;
}
#endif
void en_midi_out(uint32_t st, uint32_t d1, uint32_t d2)
{
#ifdef PLAT_HOST
    ledger_note(st, d1, d2);
#endif
    plat_midi_out(st >> 4 | st << 8 | d1 << 16 | d2 << 24);
}

/* ---- the contract */
void app_init(void)
{
    en_init();
    project_load();                      /* defaults when nothing (or another format) is saved */
    project_apply();
}
volatile uint32_t app_render_max_us, app_sheds;
static uint32_t blocks, last_over = 0xFFFFFFFFu;

void app_render(int32_t *lr, uint32_t n, uint32_t gain_q12)
{
    uint32_t t0 = plat_ticks24(), us;
    blocks++;
    en_render(lr, n, gain_q12);
    us = (plat_ticks24() - t0) / PLAT_TICKS_PER_US;
    if (us > app_render_max_us)
        app_render_max_us = us;
}
void app_overload(void)
{
    app_sheds++;
    if (last_over != 0xFFFFFFFFu && blocks - last_over <= 64u)   /* one heavy block is not a trend */
        en_shed();
    last_over = blocks;
}
void app_panic(void)
{
    en_play(0);
    en_panic();
}
void app_ui_init(void) { ui_init(); }
void app_frame(void) { ui_frame(); }
void app_input_only(void) { ui_input_only(); }
void app_say(const char *a, const char *b) { ui_say(a, b); }

#ifdef PLAT_HOST
#include "host.c"                        /* the host simulator's part: checks and commands */
#endif
