#!/bin/sh
# SPDX-License-Identifier: MPL-2.0
# The host's compiler with AddressSanitizer and UBSan on, for tests/run_tests.sh: CC=tests/host/san-cc.sh sh build.sh --host.
# The same flags as the plain build follow, so it is the same program, but for what these add.
exec cc -g -fsanitize=address,undefined -fno-omit-frame-pointer "$@"
