/* SPDX-License-Identifier: MPL-2.0 */
/* Every note MIDI says is a note MIDI has: 0..127, on every channel, at the octaves' tops and the keys' ends, with the key moved under a phrase; and each note-on has its note-off, of that same number.
 * (Key G, Octave +2, a phrase on its top note 98, the root moved to C# mid-phrase: 98 + 6 + 24 = 128. It folds down by octaves, in tune.) */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include "amb.h"

static int fails;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); fails++; } } while (0)

static int down[16][256], top[16], low[16] = {255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255};
static long over, unpaired, ons;
void en_midi_out(uint32_t st, uint32_t d1, uint32_t d2)
{
    const uint32_t k = st & 0xF0u, c = st & 15u;
    if (k == 0xB0u && d1 == 123u) {
        int i;
        for (i = 0; i < 256; i++)
            down[c][i] = 0;
        return;
    }
    if (k != 0x80u && k != 0x90u)
        return;
    if (d1 > 255u || d2 > 127u) {
        over++;
        return;
    }
    if (k == 0x90u && d2) {
        ons++;
        down[c][d1]++;
        over += d1 > 127u;
        top[c] = (int)d1 > top[c] ? (int)d1 : top[c];
        low[c] = (int)d1 < low[c] ? (int)d1 : low[c];
    } else {
        over += d1 > 127u;
        if (down[c][d1])
            down[c][d1]--;
        else if (c == 1u)                        /* (the melody's: the other channels may say a note-off twice: midi_gate.h) */
            unpaired++;
    }
}

static void sweep(int sd, int k0, int k1, int moct, int boct, uint32_t blocks, uint32_t every)
{
    static int32_t b[512];
    uint32_t n;
    en_init();
    for (n = 0; n < P_NPARAMS; n++)
        en_set((int)n, en_param_info((int)n)->def);
    en_pool(EN_MODE_POOL[3]);
    en_arrange(EN_AUTO);
    en_seed((uint32_t)sd * 77u);
    en_set(P_ROOT, k0);
    en_set(P_MOCT, moct);
    en_set(P_BASSOCT, boct);
    en_set(P_MRATE, 100);
    en_set(P_ENERGY, 100);
    en_set(P_EVOLVE, 100);
    en_restore();
    for (n = 0; n < blocks; n++) {
        en_render(b, 256, 4096u);
        if (n % every == every / 2u) {
            en_set(P_ROOT, (n / every) & 1u ? k1 : k0);
        }
    }
    en_play(0);
    en_panic();
    en_render(b, 256, 4096u);
    en_render(b, 256, 4096u);
    for (n = 0; n < 16u; n++) {
        int i;
        for (i = 0; i < 256; i++)
            down[n][i] = 0;
    }
}

int main(void)
{
    int sd, c;
    alarm(280u);
    /* two seeds that reach it (what is composed at these blocks: top[1] below says if they stop meaning anything); then each key at the top of its octaves, the root moving a tritone away */
    sweep(26, 7, 1, 2, 0, 11500u, 700u);
    sweep(19, 7, 1, 2, 0, 12900u, 700u);
    CHECK(en_mel_fold(127) == 127 && en_mel_fold(128) == 116 && en_mel_fold(139) == 127 && en_mel_fold(140) == 116 && en_mel_fold(60) == 60, "it folds by octaves");
    for (sd = 0; sd < 12; sd++)
        sweep(100 + sd, sd, (sd + 6) % 12, 2, 2, 8000u, 700u);
    for (sd = 0; sd < 12; sd++)
        sweep(200 + sd, sd, (sd + 7) % 12, -2, -2, 4000u, 500u);
    printf("notes on: %ld; over the top: %ld; off with no on: %ld\n", ons, over, unpaired);
    for (c = 0; c < 16; c++)
        if (top[c])
            printf("  channel %2d: notes %d..%d\n", c + 1, low[c], top[c]);
    CHECK(ons > 2000, "it played: %ld notes", ons);
    CHECK(!over, "%ld notes over 127", over);
    CHECK(!unpaired, "%ld note-offs with no note-on: a note that moved between", unpaired);
    CHECK(top[1] >= 116, "the sweep went high enough to mean something: the melody's top %d", top[1]);
    printf(fails ? "range: %d FAILED\n" : "range: ok\n", fails);
    return fails != 0;
}
