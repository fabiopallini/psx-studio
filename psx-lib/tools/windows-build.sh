#!/usr/bin/env bash
set -euo pipefail
engine_root="$(cygpath -u "$PSX_LIB_ROOT")"
make -C "$engine_root" "${PSX_LIB_TARGET:-all}"
