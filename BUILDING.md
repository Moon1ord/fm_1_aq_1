# Building AQ-1

AQ-1 is an app on [fm1-base](https://github.com/charlesvestal/fm1-base) (`base/`, a git submodule, with
felucca-bsp inside it). Clone with `--recursive`, or after cloning:

```
git submodule update --init --recursive
```

`./build.sh` runs fm1-base's build (`base/tools/build.py`) on `app/`, whose settings are in `app/fm1.toml`. It
makes, in `app/build/`:

| File | What |
| --- | --- |
| `aq1.bin` | the firmware app |
| `loader/ota.bin` | the update loader (felucca-bsp's) |
| `aq1.fwsc` | the installable package (app + loader) |

## Prerequisites (macOS)

- Python 3 with Pillow: `pip3 install Pillow` (Homebrew's Python, with libraqm, for the fonts). Set `PYTHON` if it
  isn't the `python3` on your PATH.
- Docker Desktop, or podman with a `docker` script that runs `exec podman "$@"` first on your PATH. The JieLi
  toolchain is Linux x86-64 only; the build runs it in a `linux/amd64` `debian:bookworm-slim` container
  (Rosetta on Apple silicon). Keep the source tree in a folder the container can share, e.g. under `/Users`.
- The JieLi Linux toolchain (clang 4.0.1 for pi32v2): `sh base/tools/get_toolchain.sh`.
- The JieLi AC79 SDK (Apache-2.0). The package uses three of its files (`cpu/wl82/tools/uboot.boot`,
  `cfg_tool.bin`, `cfg/eq_cfg_hw.bin`); none of it is part of this tree or of git.

  ```
  git clone --depth 1 --branch AC79NN_SDK_V1.2.1_2023-12-13 \
      https://gitee.com/Jieli-Tech/fw-AC79_AIoT_SDK.git ~/fw-AC79_AIoT_SDK
  ```

- Emscripten (optional, for the browser emulator) and Node.js (optional, for its test).

On Linux x86-64 the toolchain runs natively and no container is needed.

## Build

```
./build.sh                    # app/build/aq1.fwsc, identity FM-1_900
./build.sh --release X.Y      # app/build/aq1-X.Y.fwsc, identity FM-1_9XXYYZZ
./build.sh --host             # the simulator: app/build/host/aq1_host
./build.sh --web              # the browser emulator: app/build/emu/
```

`JIELI_TOOLCHAIN` and `AC79_SDK` override the default locations (`~/.jieli/toolchain`, `~/fw-AC79_AIoT_SDK`),
`PYTHON` the Python that runs the build. The build also checks the image, RAM and pool sizes, that register access
stays in felucca-bsp, and that no double-precision code is linked.

### The generated data

Two large headers are generated once and kept in git, so a clean checkout builds without their generators:

- `app/art_data.h`, the screen's art, comes from `tools/gen_art.py`. That tool needs the source pictures in
  `../art/source`, which are **not in this repository**. You need them only to change the art. Without them the
  build uses the checked-in header, and the test suite skips its `art-fresh` check. The art has its own licence
  status: see [LICENSING.md](LICENSING.md).
- `app/dsp/brk_data.h`, the drum sounds, comes from `tools/gen_breaks.c`, which synthesises them from nothing:

  ```
  cc -O2 -std=c99 -o /tmp/gen_breaks tools/gen_breaks.c -lm && /tmp/gen_breaks > app/dsp/brk_data.h
  ```

## Tests

```
sh tests/run_tests.sh             # AQ-1's; read its exit status
sh base/tests/run_tests.sh        # fm1-base's own
```

AQ-1's tests are:

- the engine, in `tests/host/*_test.c`: harmony, the arranger, rhythm, the melody, the knobs, Hands, the pieces,
  saving, the controls and MIDI;
- the update path against `app/build/aq1.fwsc`, skipped until you've run `./build.sh`;
- the browser build in Node, skipped without Emscripten;
- the whole app in the simulator (`tests/scenarios/*.amb`), with audio and screenshots in `app/build/scenarios/`,
  and every screen in `app/build/screens/`.

`app/build/host/aq1_host SCRIPT OUTDIR` runs one script. The commands are listed at the top of
`base/host/plat_host.c`, and AQ-1's own checks and commands at the top of `app/host.c`.

`tests/listen/*.amb` aren't tests. They render long performances to listen to; the command is at the top of each file.

## Install

From the command line (needs `pip3 install mido python-rtmidi`):

```
python3 base/tools/fm1_install.py app/build/aq1.fwsc
```

Installing firmware is at your own risk. Hold OCT− and OCT+ for 5 seconds for the update mode. If the FM-1 no
longer starts but reaches the chip's update mode (4C4A:8057 on USB), `base/bsp/tools/fm1_rescue.sh` puts stock
firmware back from a Mac.

## Publishing a release

Nothing has been published yet. `.github/workflows/release.yml` is set up to build a release on GitHub from its tag,
with the shared FM-1 builder, [`zvenson/fm1-build` at `v1`](https://github.com/zvenson/fm1-build/tree/v1), so anyone
can check that the package came from this source. The builder checks out the tagged commit, installs JieLi's Linux
toolchain and the AC79 SDK at its fixed tag, runs `./build.sh --release X.Y` and `tests/run_tests.sh` (a failing test
stops the release), then signs build provenance for `aq1-X.Y.fwsc` and `aq1-X.Y-app.bin`.

1. Write the notes first, as a draft: `gh release create vX.Y --draft --title "AQ-1 X.Y" --notes-file NOTES.md`.
2. Push the tag: `git tag vX.Y && git push origin vX.Y`. The workflow builds, tests, attests, checks the SHA-256 sums
   and the signatures, then publishes the draft. A tag with a suffix (`v1.4-beta`) is a prerelease. A published
   release is never rebuilt: a new build needs a new tag.

The site tooling is fm1-base's: `base/tools/make_site.py` builds a site (the landing page, the emulator, the web
installer, the manual, the words files and the firmware) from `app/` and a release package. AQ-1 has no
`app/site/landing.md` yet, and `repo` in `app/fm1.toml` is empty, so that has to be filled in first.

Before publishing, also settle these:

- the art's licence ([LICENSING.md](LICENSING.md));
- the `url:` line in `app/words.tsv`, which still points at AMB-1's site. The URL goes into the firmware, so changing
  it changes the build.
