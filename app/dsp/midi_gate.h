/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal */
/* What keeps a note from hanging when MIDI's out queue fills (plat.h, plat_midi_out_room: "a sequencer keeps room for the note-offs it
 * owes before it sends a note-on"). A note-off that is owed and an all-notes-off always go; anything else goes only if, after it, the queue
 * still has room for every note-off owed and for the five all-notes-off of notes_off. A note-on that does not go is not owed. Pure, for the
 * tests: the five channels that have notes, a bit a note */
#pragma once
#include <stdint.h>
typedef struct {
    uint32_t down[5][4];
    uint8_t n[5];                                /* so many */
    uint16_t owed;
} midi_gate_t;
#define MIDI_GATE_SPARE 6u
static inline int midi_gate_ch(uint32_t c) { return c < 4u ? (int)c : c == 9u ? 4 : -1; }
static inline int midi_gate(midi_gate_t *g, uint32_t st, uint32_t d1, uint32_t d2, uint32_t room)
{
    const uint32_t kind = st & 0xF0u;
    const int c = midi_gate_ch(st & 15u);
    const uint32_t over = room >= (uint32_t)g->owed + MIDI_GATE_SPARE + 1u;
    uint32_t *w, bit;
    if (c >= 0 && kind == 0xB0u && d1 == 123u) {
        int i;
        for (i = 0; i < 4; i++)
            g->down[c][i] = 0u;
        g->owed = (uint16_t)(g->owed - g->n[c]);
        g->n[c] = 0u;
        return 1;
    }
    if (c < 0 || (kind != 0x80u && kind != 0x90u))
        return over;
    w = &g->down[c][d1 >> 5 & 3u];
    bit = 1u << (d1 & 31u);
    if (kind == 0x80u || !d2) {                  /* one that is not owed (said twice) is only a message like any other */
        if (!(*w & bit))
            return over;
        *w &= ~bit;
        g->n[c]--;
        g->owed--;
        return 1;
    }
    if (!over)
        return 0;
    if (!(*w & bit)) {
        *w |= bit;
        g->n[c]++;
        g->owed++;
    }
    return 1;
}
