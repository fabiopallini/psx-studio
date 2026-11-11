# C source starter

This project builds directly from `main.c`. It uses fixed 50 Hz updates, pad
input, a game-owned draw callback, generic 2D rectangles and text. There is no
`.psx` file or engine-defined gameplay/UI state.

From this directory:

```sh
make PSYQ=/path/to/psyq
```

Or invoke the native tool directly:

```sh
../../build/psx-lib --build-ps1 --source-root . --root ../.. \
  --build-directory build --max-objects 1 --render-triangles 256 \
  --psyq /path/to/psyq
```

`--prepare-only` snapshots sources and prepares the SDK build without DOSBox.
Build artifacts, ownership locks, incremental object receipts and CD verification
are managed by PSX Lib. The application owns its C entry point and runtime state.
