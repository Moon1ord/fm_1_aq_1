#!/bin/sh
# SPDX-License-Identifier: MPL-2.0
# Copyright (C) 2026 Charles Vestal
# AQ-1's tests: the instrument (tests/host/amb_test.c), the whole app in fm1-base's simulator (tests/scenarios/*.amb:
# UI, chords, rhythm, persistence, the screen reader), the browser build, and the update path against the package
# (./build.sh first). Screenshots and audio in app/build/scenarios/. fm1-base's own tests: base/tests/run_tests.sh.
#   tests/run_tests.sh
set -u
cd "$(dirname "$0")/.."
CC="${CC:-cc}"
PY="${PYTHON:-python3}"
export AC79_SDK="${AC79_SDK:-$HOME/fw-AC79_AIoT_SDK}"   # the package's SDK files, as ./build.sh
B=app/build
OUT=$B/tests
FAIL=0
mkdir -p "$OUT" $B/scenarios
run() {
    name="$1"; shift
    if "$@" > "$OUT/$name.log" 2>&1; then
        echo "  ok   $name"
    else
        echo "  FAIL $name (see $OUT/$name.log)"
        tail -15 "$OUT/$name.log" | sed 's/^/       /'
        FAIL=1
    fi
}
run amb sh -c "$CC -O2 -ffp-contract=off -std=c99 -Wall -Wextra -Wdouble-promotion -Werror -DEN_HOST -Iapp/dsp -Ibase/include \
    -o $OUT/amb_test tests/host/amb_test.c app/dsp/amb.c app/dsp/brk.c -lm && $OUT/amb_test"
run harm sh -c "$CC -O2 -ffp-contract=off -std=c99 -Wall -Wextra -Wdouble-promotion -Werror -DEN_HOST -Iapp/dsp -Ibase/include \
    -o $OUT/harm_test tests/host/harm_test.c app/dsp/amb.c app/dsp/brk.c -lm && $OUT/harm_test"
run gen sh -c "$CC -O2 -ffp-contract=off -std=c99 -Wall -Wextra -Wdouble-promotion -Werror -DEN_HOST -Iapp/dsp -Ibase/include \
    -o $OUT/gen_test tests/host/gen_test.c app/dsp/amb.c app/dsp/brk.c -lm && $OUT/gen_test"
run arr sh -c "$CC -O2 -ffp-contract=off -std=c99 -Wall -Wextra -Wdouble-promotion -Werror -DEN_HOST -Iapp/dsp -Ibase/include \
    -o $OUT/arr_test tests/host/arr_test.c app/dsp/amb.c app/dsp/brk.c -lm && $OUT/arr_test"
run rhy sh -c "$CC -O2 -ffp-contract=off -std=c99 -Wall -Wextra -Wdouble-promotion -Werror -DEN_HOST -Iapp/dsp -Ibase/include \
    -o $OUT/rhy_test tests/host/rhy_test.c app/dsp/amb.c app/dsp/brk.c -lm && $OUT/rhy_test"
run knob sh -c "$CC -O2 -ffp-contract=off -std=c99 -Wall -Wextra -Wdouble-promotion -Werror -DEN_HOST -Iapp/dsp -Ibase/include \
    -o $OUT/knob_test tests/host/knob_test.c app/dsp/amb.c app/dsp/brk.c -lm && $OUT/knob_test"
run mel sh -c "$CC -O2 -ffp-contract=off -std=c99 -D_POSIX_C_SOURCE=200809L -D_DARWIN_C_SOURCE -Wall -Wextra -Wdouble-promotion -Werror -DEN_HOST -Iapp/dsp -Ibase/include \
    -o $OUT/mel_test tests/host/mel_test.c app/dsp/amb.c app/dsp/brk.c -lm && $OUT/mel_test"
run hands sh -c "$CC -O2 -ffp-contract=off -std=c99 -Wall -Wextra -Wdouble-promotion -Werror -DEN_HOST -Iapp/dsp -Ibase/include \
    -o $OUT/hands_test tests/host/hands_test.c app/dsp/amb.c app/dsp/brk.c -lm && $OUT/hands_test"
run piece sh -c "$CC -O2 -ffp-contract=off -std=c99 -D_DEFAULT_SOURCE -D_DARWIN_C_SOURCE -Wall -Wextra -Wdouble-promotion -Werror -DEN_HOST -Iapp/dsp -Ibase/include \
    -o $OUT/piece_test tests/host/piece_test.c app/dsp/amb.c app/dsp/brk.c -lm && $OUT/piece_test"
run save sh -c "$CC -O2 -ffp-contract=off -std=c99 -D_DEFAULT_SOURCE -D_DARWIN_C_SOURCE -Wall -Wextra -Wdouble-promotion -Werror -DEN_HOST -Iapp/dsp -Ibase/include \
    -o $OUT/save_test tests/host/save_test.c app/dsp/amb.c app/dsp/brk.c -lm && $OUT/save_test"
run ctl sh -c "$CC -O2 -ffp-contract=off -std=c99 -Wall -Wextra -Wdouble-promotion -Werror -DEN_HOST -Iapp/dsp -Ibase/include \
    -o $OUT/ctl_test tests/host/ctl_test.c app/dsp/amb.c app/dsp/brk.c -lm && $OUT/ctl_test"
run range sh -c "$CC -O2 -ffp-contract=off -std=c99 -D_DEFAULT_SOURCE -Wall -Wextra -Wdouble-promotion -Werror -DEN_HOST -Iapp/dsp -Ibase/include \
    -o $OUT/range_test tests/host/range_test.c app/dsp/amb.c app/dsp/brk.c -lm && $OUT/range_test"
run sound sh -c "$CC -O2 -ffp-contract=off -std=c99 -D_DEFAULT_SOURCE -D_DARWIN_C_SOURCE -Wall -Wextra -Wdouble-promotion -Werror -DEN_HOST -Iapp/dsp -Ibase/include \
    -o $OUT/sound_test tests/host/sound_test.c app/dsp/amb.c app/dsp/brk.c -lm && $OUT/sound_test"
run art sh -c "$CC -O2 -std=c99 -Wall -Wextra -Werror -Iapp -o $OUT/art_test tests/host/art_test.c && $OUT/art_test"
if [ -f ../art/source/eyes.png ]; then
    run art-fresh perl -e 'alarm shift; exec @ARGV' 300 "$PY" -I tools/gen_art.py --check   # the header is what the owner's pictures give, the chapters what was approved
else
    echo "  skip art-fresh (no ../art/source: app/art_data.h is kept in git)"
fi
run brk sh -c "$CC -O2 -ffp-contract=off -std=c99 -Wall -Wextra -Wdouble-promotion -Werror -Iapp/dsp -o $OUT/brk_test tests/host/brk_test.c app/dsp/brk.c -lm && $OUT/brk_test"
if [ -f $B/aq1.fwsc ]; then
    run ota-entry sh -c "$CC -w -o $OUT/ota_test base/tests/ota_test.c && $OUT/ota_test $B/aq1.fwsc"
    run update-loader sh -c "head -c 100000 $B/aq1.bin > $OUT/old_app.bin && \
        $PY base/tools/fm1pkg_make.py $OUT/old_app.bin $B/loader/ota.bin $OUT/old.fwsc >/dev/null && \
        $CC -w -o $OUT/ldr_test base/tests/ldr_test.c && $OUT/ldr_test $OUT/old.fwsc $B/aq1.fwsc"
else
    echo "  skip update path (no $B/aq1.fwsc: ./build.sh)"
fi
run host-build sh build.sh --host
if command -v emcc >/dev/null 2>&1; then
    run emu sh -c "sh build.sh --web && node tests/host/emu_test.mjs $B/emu/app.wasm"
else
    echo "  skip emu (no emcc)"
fi
for s in tests/scenarios/*.amb; do
    n=$(basename "$s" .amb)
    mkdir -p "$B/scenarios/$n"
    run "scenario-$n" $B/host/aq1_host "$s" "$B/scenarios/$n"
done
mkdir -p $B/screens                                     # every screen, to be looked at: the plan's check
run screens $B/host/aq1_host tests/screens/screens.amb $B/screens
[ $FAIL -eq 0 ] && echo "all tests passed" || echo "TESTS FAILED"
exit $FAIL
