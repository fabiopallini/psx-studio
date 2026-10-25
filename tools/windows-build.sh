#!/usr/bin/env bash
set -eu
cd "$(cygpath -u "$PSX_STUDIO_ROOT")"
if [ -n "${PSX_STUDIO_PROJECT:-}" ] && [ "$PSX_STUDIO_TARGET" != build ]; then
    echo '-Project requires the build target.' >&2; exit 1
fi
if [ "$PSX_STUDIO_TARGET" = clean ]; then
    exec make clean
fi
for tool in gcc make pkg-config; do
    command -v "$tool" >/dev/null || { echo "Missing $tool. Install the MSYS2 dependencies listed in README.md." >&2; exit 1; }
done
pkg-config --atleast-version=4.12 gtk4
pkg-config --exists gtk4 json-glib-1.0 epoxy libpng
if [ "$PSX_STUDIO_TARGET" = build ] || [ "$PSX_STUDIO_TARGET" = run ]; then
    pkg-config --exists angleproject || { echo 'Install EGL/ANGLE: pacman -S mingw-w64-ucrt-x86_64-angleproject' >&2; exit 1; }
fi
case "$PSX_STUDIO_TARGET" in
    build) make CC=gcc windows-package ;;
    run) make CC=gcc windows-package; ./build/windows/psx-studio/psx-studio.exe ;;
    test) make CC=gcc PYTHON=python test ;;
    *) echo 'Unknown build target.' >&2; exit 1 ;;
esac
if [ -n "${PSX_STUDIO_PROJECT:-}" ]; then
    if [ -n "${PSX_STUDIO_SDK:-}" ]; then
        ./build/psx-studio.exe --build-ps1 --project "$PSX_STUDIO_PROJECT" --psyq "$PSX_STUDIO_SDK"
    else
        ./build/psx-studio.exe --build-ps1 --project "$PSX_STUDIO_PROJECT"
    fi
fi
