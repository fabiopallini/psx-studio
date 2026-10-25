#!/usr/bin/env bash
set -euo pipefail
studio_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
if [[ -f "$studio_dir/build/psx-studio.exe" && "${OS:-}" == Windows_NT ]]; then
    exec "$studio_dir/build/psx-studio.exe" --build-ps1 "$@"
fi
exec "$studio_dir/build/psx-studio" --build-ps1 "$@"
