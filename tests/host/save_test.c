/* SPDX-License-Identifier: MPL-2.0 */
/* What a flash write asks of the engine, and the system's voice. The volume's duck: the same engine rendered twice from the same start, once under a duck,
 * so that the duck is measured against the very samples it was given: a line down, zero, a line back, and after it the same samples, bit for bit: the music
 * under it went on and did not know. The voice: its notes, sung from the chord as it stands, rising, none of it on MIDI, and when it has rung out, nothing
 * of it left. The UI's side of it (what starts a save, what cancels it) is tests/scenarios/save.amb. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "amb.h"

static int fails;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); fails++; } } while (0)
#define GAIN 2430u
#define BLOCK 256u
#define MS(x) ((uint32_t)((float)(x) * 44.1f + 0.5f))

static uint32_t midi_hash, midi_n;
void en_midi_out(uint32_t st, uint32_t d1, uint32_t d2)
{
    midi_hash = (midi_hash ^ (st << 16 | d1 << 8 | d2)) * 16777619u;
    midi_n++;
}

static int32_t blk[2 * BLOCK];
static uint32_t ahash;
static int32_t apeak;
static void render_blocks(uint32_t n)
{
    uint32_t b, j;
    for (b = 0; b < n; b++) {
        en_render(blk, BLOCK, GAIN);
        for (j = 0; j < 2u * BLOCK; j++) {
            const int32_t a = blk[j] < 0 ? -blk[j] : blk[j];
            ahash = (ahash ^ (uint32_t)blk[j]) * 16777619u;
            if (a > apeak)
                apeak = a;
        }
    }
}
static void render_ms(uint32_t ms) { render_blocks(MS(ms) / BLOCK + 1u); }
static void start(void)
{
    en_init();
    en_seed(1234u);
    en_restore();
    ahash = 2166136261u;
    midi_hash = 2166136261u;
    midi_n = 0u;
    apeak = 0;
}

/* ---- the duck */
#define LINE_ROOM 1e-3f                          /* a line made by adding a float each sample: where it is, to 0.1 %. Half a sample's worth of its slope is less than that */
#define REF_BLOCKS 400u                          /* 2.3 s */
static int32_t ref[REF_BLOCKS][2 * BLOCK], dip[REF_BLOCKS][2 * BLOCK];
static void record(int32_t (*to)[2 * BLOCK], uint32_t from, uint32_t n)
{
    uint32_t b;
    for (b = from; b < from + n; b++)
        en_render(to[b], BLOCK, GAIN);
}
static void warm(void)                           /* some way in, drums and all: every voice in the mix */
{
    start();
    render_ms(60000u);
}

static void t_duck_line(void)
{
    uint32_t b, j;
    int32_t refpeak = 0;
    const uint32_t lead = 20u;                   /* blocks before it */
    uint32_t ducked_at = 0u, back_at = 0u, undone = 0u;
    warm();
    record(ref, 0u, REF_BLOCKS);
    warm();
    record(dip, 0u, lead);
    en_duck(1);
    CHECK(!en_ducked, "not there the moment it is asked");
    for (b = lead; b < REF_BLOCKS; b++) {
        en_render(dip[b], BLOCK, GAIN);
        if (en_ducked && !ducked_at) {
            uint32_t k, nz = 0u;
            ducked_at = b;
            for (k = b - 1u; k <= b; k++)        /* the last two blocks, the whole of the DMA's ring, are nothing at all */
                for (j = 0; j < 2u * BLOCK; j++)
                    nz += dip[k][j] != 0;
            CHECK(!nz, "it says zero with %u samples of sound in the ring", nz);
        }
        if (ducked_at && !back_at && b >= ducked_at + 10u) {
            back_at = b + 1u;
            en_duck(0);
        }
    }
    CHECK(ducked_at, "it never said so");
    if (!ducked_at || !back_at)
        return;
    for (b = 0; b < lead; b++)
        CHECK(!memcmp(ref[b], dip[b], sizeof ref[b]), "block %u: moved before it was asked", b);
    /* down, on the line: 150 ms from the block it was asked at (each sample the render gave, times the line; the samples are ints, so a little room) */
    for (b = lead; b < ducked_at; b++)
        for (j = 0; j < BLOCK; j++) {
            const float t = (float)((b - lead) * BLOCK + j + 1u) / (0.15f * 44100.0f), g = t >= 1.0f ? 0.0f : 1.0f - t;
            const float r = (float)ref[b][2u * j], got = (float)dip[b][2u * j];
            if (fabsf(r * g - got) > 2.0f + fabsf(r) * LINE_ROOM)
                undone++;
            if (t >= 1.0f + LINE_ROOM && dip[b][2u * j] != 0)
                undone++;
        }
    CHECK(!undone, "down: %u samples off the 150 ms line", undone);
    undone = 0u;
    /* said within the two blocks after the line ends: not a timer of its own, not early, not late */
    {
        const float ms = (float)((ducked_at - lead + 1u) * BLOCK) / 44.1f;
        CHECK(ms >= 150.0f + 2.0f * 256.0f / 44.1f && ms <= 150.0f + 4.0f * 256.0f / 44.1f, "zero said after %.1f ms", (double)ms);
    }
    /* flat at zero until it is told; and en_ducked goes the moment it is told */
    for (b = ducked_at; b < back_at; b++)
        for (j = 0; j < 2u * BLOCK; j++)
            if (dip[b][j])
                undone++;
    CHECK(!undone, "%u samples of sound at zero", undone);
    CHECK(!en_ducked, "still says zero after it came back");
    /* up, on the line: 300 ms */
    undone = 0u;
    for (b = back_at; b < REF_BLOCKS; b++)
        for (j = 0; j < BLOCK; j++) {
            const float t = (float)((b - back_at) * BLOCK + j + 1u) / (0.30f * 44100.0f), g = t >= 1.0f ? 1.0f : t;
            const float r = (float)ref[b][2u * j], r2 = (float)ref[b][2u * j + 1u];
            if (fabsf(r * g - (float)dip[b][2u * j]) > 2.0f + fabsf(r) * LINE_ROOM)
                undone++;
            if (fabsf(r2 * g - (float)dip[b][2u * j + 1u]) > 2.0f + fabsf(r2) * LINE_ROOM)
                undone++;
        }
    CHECK(!undone, "up: %u samples off the 300 ms line", undone);
    /* and after it, the same samples: the same music, bit for bit */
    {
        const uint32_t after = back_at + MS(300u) / BLOCK + 2u;
        CHECK(after + 5u < REF_BLOCKS, "the test's own room: %u", after);
        for (b = after; b < REF_BLOCKS; b++)
            CHECK(!memcmp(ref[b], dip[b], sizeof ref[b]), "block %u: not the same music after it", b);
    }
    for (b = 0; b < REF_BLOCKS; b++)
        for (j = 0; j < 2u * BLOCK; j++)
            if (ref[b][j] > refpeak)
                refpeak = ref[b][j];
    CHECK(refpeak > 100000, "the test has music to duck (%d)", (int)refpeak);
}

static void t_duck_turned_back(void)             /* asked back halfway: from where it is, with no step */
{
    uint32_t b, j, jumps = 0u;
    warm();
    record(ref, 0u, 100u);
    warm();
    record(dip, 0u, 10u);
    en_duck(1);
    record(dip, 10u, 8u);                        /* about 46 ms: partway */
    CHECK(!en_ducked, "no zero after 46 ms");
    en_duck(0);
    record(dip, 18u, 82u);
    for (b = 10; b < 100u; b++)
        for (j = 0; j < BLOCK; j++) {
            /* the gain, as far as it can be read: only where the ref says enough */
            const float r = (float)ref[b][2u * j];
            if (fabsf(r) > 200000.0f) {
                const float g = (float)dip[b][2u * j] / r;
                const float t = (float)((b - 10u) * BLOCK + j + 1u);
                const float top = 1.0f - fminf(t, 8.0f * 256.0f) / (0.15f * 44100.0f) + fmaxf(t - 8.0f * 256.0f, 0.0f) / (0.30f * 44100.0f);
                if (fabsf(g - fminf(top, 1.0f)) > 0.003f)
                    jumps++;
            }
        }
    CHECK(!jumps, "%u samples off a line down and back", jumps);
    CHECK(!memcmp(ref[99], dip[99], sizeof ref[99]), "not back whole");
}

static void t_duck_panic(void)
{
    warm();
    en_duck(1);
    render_ms(250u);
    CHECK(en_ducked, "ducked after 250 ms");
    en_panic();
    render_blocks(2u);
    CHECK(!en_ducked, "a panic ends the duck");
    apeak = 0;
    render_ms(2000u);
    CHECK(apeak == 0, "a panic is silence, duck or none (%d)", (int)apeak);
    en_restore();
    render_ms(3000u);
    apeak = 0;
    render_ms(10000u);
    CHECK(!en_ducked && apeak > 100000, "it comes back: not mute after a panic in a duck (%d)", (int)apeak);

    /* a panic with the duck halfway down */
    warm();
    en_duck(1);
    render_ms(60u);
    en_panic();
    en_restore();
    render_ms(3000u);
    apeak = 0;
    render_ms(10000u);
    CHECK(apeak > 100000, "halfway down and panicked: not back (%d)", (int)apeak);

    /* in silence there is nothing to fade: a save can go at once, and back again */
    warm();
    en_panic();
    render_blocks(2u);
    en_duck(1);
    render_blocks(2u);
    CHECK(en_ducked, "silent is at zero");
    en_duck(0);
    render_blocks(2u);
    CHECK(!en_ducked, "and back");
    en_restore();
    render_ms(3000u);
    apeak = 0;
    render_ms(10000u);
    CHECK(apeak > 100000, "a save in silence: not back (%d)", (int)apeak);

    /* en_init: nothing left of a duck: another start is not mute */
    warm();
    en_duck(1);
    render_ms(250u);
    start();
    CHECK(!en_ducked, "en_init: still says zero");
    render_ms(30000u);
    CHECK(apeak > 100000, "en_init: mute (%d)", (int)apeak);
}

/* ---- the voice */
static uint32_t chord_mask(void)
{
    uint32_t m = 0u;
    int i;
    for (i = 0; i < EN_NPAD; i++)
        m |= 1u << (en_chord[i] % 12u);
    return m;
}

static const uint32_t SYS_COUNT[3] = {1u, 5u, 3u};

/* each kind's notes: how many, each in the chord as it stood, each higher than the last, and the time between them */
static void sing_and_check(int kind, const char *where)
{
    const uint32_t s0 = en_sys_notes;
    uint32_t seen = 0u, n = s0, frames = 0u, last_at = 0u, b, gapmin = 0xFFFFFFFFu, gapmax = 0u;
    int last = -1;
    en_sys(kind);
    for (b = 0; b < MS(1500u) / BLOCK; b++) {
        const uint32_t before = chord_mask();
        en_render(blk, BLOCK, GAIN);
        frames += BLOCK;
        if (en_sys_notes != n) {
            const int note = en_sys_note;
            CHECK(en_sys_notes - n == 1u, "%s kind %d: %u notes in a block", where, kind, en_sys_notes - n);
            n = en_sys_notes;
            seen++;
            /* the chord before this block or after it: it may have moved in it */
            CHECK((before | chord_mask()) >> (note % 12) & 1u, "%s kind %d: note %d not in the chord %03x", where, kind, note, before);
            CHECK(note > last, "%s kind %d: %d after %d: not rising", where, kind, note, last);
            if (last >= 0) {
                const uint32_t gap = frames - last_at;
                gapmin = gap < gapmin ? gap : gapmin;
                gapmax = gap > gapmax ? gap : gapmax;
            }
            last = note;
            last_at = frames;
        }
    }
    CHECK(seen == SYS_COUNT[kind], "%s kind %d: %u notes", where, kind, seen);
    if (seen > 1u)
        CHECK(gapmin >= MS(80u) && gapmax <= MS(160u), "%s kind %d: %u .. %u frames between notes", where, kind, gapmin, gapmax);
}

static void t_sys_notes(void)
{
    int kind, root, mode;
    start();
    render_ms(1000u);
    for (kind = EN_SYS_BLIP; kind <= EN_SYS_SAVE; kind++)
        sing_and_check(kind, "growing");
    render_ms(40000u);
    for (kind = EN_SYS_BLIP; kind <= EN_SYS_SAVE; kind++)
        sing_and_check(kind, "later");
    en_play(0);
    render_ms(500u);
    for (kind = EN_SYS_BLIP; kind <= EN_SYS_SAVE; kind++)
        sing_and_check(kind, "resting");
    /* every key and mode: the chord's notes wherever it is */
    for (root = 0; root < 12; root += 5)
        for (mode = 0; mode < EN_NMODES; mode++) {
            start();
            en_set(P_ROOT, root);
            en_set(P_MODE, mode);
            en_pool(EN_MODE_POOL[mode]);
            render_ms(6000u);
            sing_and_check(EN_SYS_BOOT, "a key and mode");
            sing_and_check(EN_SYS_SAVE, "a key and mode");
        }
    /* from the first block: the boot's first note is already the chord's (nothing has been composed before the first block) */
    start();
    en_sys(EN_SYS_BOOT);
    {
        const uint32_t s0 = en_sys_notes;
        en_render(blk, BLOCK, GAIN);
        CHECK(en_sys_notes == s0 + 1u, "boot: its first note in the first block");
        CHECK(chord_mask() >> (en_sys_note % 12u) & 1u, "boot: first note %d not in the chord %03x", en_sys_note, chord_mask());
    }
}

/* a blip does not break into a boot's or a save's notes: one voice, and those say something */
static void t_sys_blip_waits(void)
{
    const uint32_t s0 = (start(), render_ms(500u), en_sys_notes);
    en_sys(EN_SYS_BOOT);
    render_ms(150u);
    en_sys(EN_SYS_BLIP);
    render_ms(1000u);
    CHECK(en_sys_notes - s0 == 5u, "a blip in the boot's notes: %u notes", en_sys_notes - s0);
    en_sys(EN_SYS_BLIP);
    render_ms(10u);
    CHECK(en_sys_notes - s0 == 6u, "a blip once they have finished: %u", en_sys_notes - s0);
    en_sys(EN_SYS_BOOT);                         /* and the save's, over a boot's: they say more than the rest of it */
    render_ms(150u);
    en_sys(EN_SYS_SAVE);
    render_ms(1000u);
    CHECK(en_sys_notes - s0 >= 6u + 3u && en_sys_notes - s0 <= 6u + 3u + 2u, "a save over a boot: %u", en_sys_notes - s0);
}

/* none of it on MIDI, none of it in what is composed: the MIDI the same, byte for byte, with the voice singing throughout */
static void t_sys_no_midi(void)
{
    uint32_t h, n, i;
    start();
    render_blocks(90u * 172u);
    h = midi_hash;
    n = midi_n;
    CHECK(n > 500u, "the test has MIDI to compare (%u)", n);
    start();
    for (i = 0; i < 90u; i++) {
        en_sys((int)(i % 3u));
        render_blocks(172u);
    }
    CHECK(midi_hash == h && midi_n == n, "the voice on MIDI, or in what is composed: %u messages, then %u", n, midi_n);
    /* the whole of it, under the duck too */
    start();
    for (i = 0; i < 18u; i++) {
        en_sys(EN_SYS_SAVE);
        en_duck(1);
        render_blocks(430u);
        en_duck(0);
        render_blocks(430u);
    }
    CHECK(midi_hash == h && midi_n == n, "the duck moves MIDI: %u messages, then %u", n, midi_n);
}

/* level 0 everywhere: the voice alone. Its pitch; a blip quieter than the others; an end of it, and with no reverb nothing left of it */
static void all_levels_off(void)
{
    static const int L[] = {P_KEYS, P_MOTIFS, P_BASS, P_DRUMS, P_HATS};
    unsigned i;
    for (i = 0; i < sizeof L / sizeof L[0]; i++)
        en_set(L[i], 0);
}
static float goertzel(const float *x, uint32_t n, float hz)
{
    const float c = 2.0f * cosf(6.2831853f * hz / 44100.0f);
    float a = 0.0f, b = 0.0f;
    uint32_t i;
    for (i = 0; i < n; i++) {
        const float t = x[i] + c * a - b;
        b = a;
        a = t;
    }
    return sqrtf(a * a + b * b - c * a * b);
}
static float alone[32768];
static void t_sys_alone(void)
{
    int32_t pk[3];
    int kind;
    uint32_t i, j;
    for (kind = EN_SYS_BLIP; kind <= EN_SYS_SAVE; kind++) {
        start();
        all_levels_off();
        en_set(P_ECHO, 0);
        en_set(P_REVERB, 0);
        render_ms(20000u);                       /* the room empties */
        apeak = 0;
        render_ms(200u);
        CHECK(apeak == 0, "kind %d: the test's silence is not (%d)", kind, (int)apeak);
        en_sys(kind);
        render_ms(1500u);
        pk[kind] = apeak;
        CHECK(apeak > 0, "kind %d: not a sound", kind);
        apeak = 0;
        render_ms(500u);                         /* it has rung out: all of it, to the last bit */
        CHECK(apeak == 0, "kind %d: still sounding after 2 s (%d)", kind, (int)apeak);
    }
    CHECK(pk[EN_SYS_BLIP] * 3 / 2 < pk[EN_SYS_SAVE], "the blip %d, the save %d: it should be the quieter", (int)pk[EN_SYS_BLIP], (int)pk[EN_SYS_SAVE]);
    CHECK(pk[EN_SYS_SAVE] < 8300000 / 2, "the voice at more than half scale (%d)", (int)pk[EN_SYS_SAVE]);

    /* what is sung is at its note: a blip's sound, at the frequency it says */
    start();
    all_levels_off();
    en_set(P_ECHO, 0);
    en_set(P_REVERB, 0);
    render_ms(20000u);
    en_sys(EN_SYS_BLIP);
    for (i = 0; i < 32768u / BLOCK; i++) {
        en_render(blk, BLOCK, GAIN);
        for (j = 0; j < BLOCK; j++)
            alone[i * BLOCK + j] = (float)blk[2u * j];
    }
    {
        const float hz = 440.0f * powf(2.0f, ((float)en_sys_note - 69.0f) / 12.0f);
        const float at = goertzel(alone, 8192u, hz), off1 = goertzel(alone, 8192u, hz * 1.122f), off2 = goertzel(alone, 8192u, hz * 0.891f);
        CHECK(at > 4.0f * off1 && at > 4.0f * off2, "note %d: %.0f at it, %.0f and %.0f a tone off", en_sys_note, (double)at, (double)off1, (double)off2);
    }

    /* it goes to the reverb too, as the spec has it: with reverb on, its tail is there once the voice has gone */
    start();
    all_levels_off();
    en_set(P_ECHO, 0);
    render_ms(30000u);
    apeak = 0;
    render_ms(100u);
    {
        const int32_t before = apeak;
        en_sys(EN_SYS_SAVE);
        render_ms(1400u);
        apeak = 0;
        render_ms(200u);
        CHECK(apeak > before + 500, "no tail in the reverb: %d after, %d before", (int)apeak, (int)before);
    }
}

/* the music's samples outside the voice: before it, and with no reverb to carry it, after it */
static void t_sys_leaves_nothing(void)
{
    uint32_t a, b, c;
    start();
    en_set(P_REVERB, 0);
    render_blocks(700u);
    a = ahash;
    render_blocks(1400u);
    b = ahash;
    ahash = 2166136261u;
    render_blocks(1000u);
    c = ahash;
    start();
    en_set(P_REVERB, 0);
    render_blocks(700u);
    CHECK(ahash == a, "before it: moved");
    en_sys(EN_SYS_BOOT);
    render_blocks(1400u);
    CHECK(ahash != b, "the voice is not heard in the music");
    ahash = 2166136261u;                         /* the same stretch, once it has rung out */
    render_blocks(1000u);
    CHECK(ahash == c, "the voice leaves something in the mix when it has gone");
}

static void t_sys_panic(void)
{
    uint32_t n;
    start();
    render_ms(2000u);
    en_sys(EN_SYS_BOOT);
    render_ms(150u);
    en_panic();
    render_blocks(1u);
    n = en_sys_notes;
    apeak = 0;
    render_ms(2000u);
    CHECK(apeak == 0, "the voice through a panic (%d)", (int)apeak);
    CHECK(en_sys_notes == n, "its notes go on in a panic");
    en_sys(EN_SYS_BLIP);
    render_ms(500u);
    CHECK(apeak == 0 && en_sys_notes == n, "a blip in the silence: %d", (int)apeak);
    en_restore();                                /* and nothing of the run comes back with the sound */
    render_ms(2000u);
    CHECK(en_sys_notes == n, "a run comes back after a restore");
}

int main(void)
{
    t_duck_line();
    t_duck_turned_back();
    t_duck_panic();
    t_sys_notes();
    t_sys_blip_waits();
    t_sys_no_midi();
    t_sys_alone();
    t_sys_leaves_nothing();
    t_sys_panic();
    if (fails) {
        printf("%d failed\n", fails);
        return 1;
    }
    printf("save: ok\n");
    return 0;
}
