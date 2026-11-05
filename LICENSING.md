# AQ-1 licensing

AQ-1's code is free software under the **Mozilla Public License 2.0** (`MPL-2.0`, full text in `LICENSE`), file by
file. If you change one of its files and share the result, share that file's source under the MPL.

**The art is the exception.** It is not under the MPL; its owner keeps all rights. See [The art](#the-art).

## Where the code comes from

AQ-1 is a fork of AMB-1 by Charles Vestal ([fm1-amb](https://github.com/charlesvestal/fm1-amb), MPL-2.0), taken at
commit `62a588a`.

| What | Origin | Licence |
| --- | --- | --- |
| The platform: `base/`, a git submodule (boot, USB and USB audio, the TRS jack, storage, the update path, screen, panel, the screen reader, the host simulator and the browser emulator, build and install tools) | [fm1-base](https://github.com/charlesvestal/fm1-base) and [felucca-bsp](https://github.com/hugelton/felucca-bsp), from [Felucca](https://github.com/hugelton/Felucca) by Leo Kuroshita (@kurogedelic), Hügelton Instruments, under MPL-2.0 with his permission ([felucca-bsp#4](https://github.com/hugelton/felucca-bsp/issues/4)). Not changed by AQ-1. | MPL-2.0 |
| Files that came from fm1-amb, most of them changed for AQ-1: `app/app.c`, `app/app.h`, `app/ui.c`, `app/project.c`, `app/host.c`, `app/dsp/amb.c` (with its Dattorro plate from FoMni-1), `app/dsp/amb.h`, `app/fm1.toml`, `app/words.*`, `build.sh`, `.github/workflows/release.yml`, `tests/run_tests.sh`, `tests/host/amb_test.c`, `tests/host/emu_test.mjs`, some of `tests/scenarios/`, `docs/MANUAL.md`, `docs/img/fm1-panel.svg` | Charles Vestal, with AQ-1's changes | MPL-2.0 |
| Files new in AQ-1: `app/art.h`, `app/dsp/brk.c`, `app/dsp/brk.h`, `app/dsp/brk_data.h`, `app/dsp/midi_gate.h`, `tools/gen_art.py`, `tools/gen_breaks.c`, the other tests in `tests/` | AQ-1 | MPL-2.0 |
| The idea of a browser emulator for FM-1 firmware (fm1-base's emulator) | [Groove OS](https://www.groove-os.com/) by Peter Gombos, whose site runs its firmware in the browser; no code from it | credit only |

The file headers follow this table. A file from upstream keeps its `Copyright (C) 2026 Charles Vestal` line, which
stays true of what it inherited, and its changes are in this repository's history. A file new in AQ-1 carries only its
`SPDX-License-Identifier`, because Charles Vestal did not write it.

Versions of AMB-1 up to 0.9 were GPL-3.0-only. That code is not in this tree. AQ-1 comes from fm1-amb's later MPL-2.0
code.

The drums are AQ-1's own. Their sounds are synthesised by `tools/gen_breaks.c` with no recordings in them, and
`app/dsp/brk.c` composes the patterns. Nothing was copied from X0X's `breaks.c`.

## The art

`app/art_data.h` is generated data: the screen's pictures (the face, the page chapters, the break's frame and the
title), made by `tools/gen_art.py` from the owner's source pictures, which are not in this repository. The screenshots
`docs/img/screen-*.png` show the same art.

**The screen's art is © 2026 Moon1ord, all rights reserved. You may install and use AQ-1's firmware with the art in it, build it from this source for your own use, and share unchanged packages of it. No other licence to copy or reuse the art is granted.**

The MPL does cover `tools/gen_art.py`, the code that generates the header, and `app/art.h`, the code that reads it. The
built firmware contains the art, so sharing a built package is covered by the terms above.

## Third-party material

| What | Licence | Where |
| --- | --- | --- |
| Fonts: Barlow Semi Condensed (The Barlow Project Authors), Terminus (Dimitar Toshkov Zhekov) | SIL OFL 1.1 | `base/assets/fonts/` |
| JieLi AC79 SDK: `uboot.boot`, `cfg_tool.bin`, `eq_cfg_hw.bin` are read from your SDK checkout at build time and placed in the package; no SDK files are in this tree | Apache-2.0 | <https://gitee.com/Jieli-Tech/fw-AC79_AIoT_SDK> |

## Trademarks

"Felucca" and "Hügelton Instruments" are names of Hügelton Instruments. "M-VAVE" and "FM-1" are trademarks of their
respective owners, used here only to say what hardware AQ-1 runs on. AQ-1 is independent firmware; it is not
affiliated with, endorsed by or supported by any of them, or by Charles Vestal.

## Radio

AQ-1 never enables the Bluetooth / Wi-Fi radio of the hardware.
