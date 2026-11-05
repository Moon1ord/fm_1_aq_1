#!/bin/sh
# SPDX-License-Identifier: MPL-2.0
# One scenario under the sanitized simulator: san-run.sh SIM SCENARIO OUTDIR SECONDS. AddressSanitizer stops the run and it fails. UBSan says each place once and goes on, so its lines are read here:
# any "runtime error" fails, but for exactly one: base/include/fastmath.h:58, fm_exp2_nf's left shift of a negative exponent, which is fm1-base's and works on every compiler we have.
set -u
sim="$1"; scn="$2"; out="$3"; secs="$4"
log="$out/san.log"
mkdir -p "$out"
ASAN_OPTIONS="detect_leaks=0:detect_stack_use_after_return=1:strict_string_checks=1:abort_on_error=0:color=never" UBSAN_OPTIONS="print_stacktrace=1:color=never" \
    perl -e 'alarm shift; exec @ARGV' "$secs" "$sim" "$scn" "$out" > "$log" 2>&1
rc=$?
ub=$(grep "runtime error:" "$log" | grep -v "base/include/fastmath.h:58:[0-9]*: runtime error: left shift of negative value")
if [ -n "$ub" ]; then
    echo "$ub"
    rc=1
fi
if [ $rc -ne 0 ]; then
    echo "exit $rc"
    grep -E "ERROR: AddressSanitizer|SUMMARY|^    #[0-5] |FAIL|expect" "$log" | head -30
fi
exit $rc
