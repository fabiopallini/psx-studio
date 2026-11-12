PYTHON ?= python3
CC ?= cc
PKGS = gio-2.0 json-glib-1.0 libpng
HOSTFLAGS = -std=c11 -Wall -Wextra -O2 $(shell pkg-config --cflags $(PKGS)) -I../tools
HOSTLIBS = $(shell pkg-config --libs $(PKGS)) -lm
C89 = -std=c89 -pedantic-errors -Wall -Wextra -Werror -O2
UNITS = overlay runtime camera input timing fps render_cache frame checkpoint sound audio
BINS = $(addprefix ../build/test-,$(UNITS))
.PHONY: all
all: $(BINS) ../build/tests
	@set -e; for test in $(BINS); do $$test; done
	cd .. && ./build/tests
	cd .. && PYTHONDONTWRITEBYTECODE=1 $(PYTHON) tests/build_pipeline.py
../build/test-%: %.c ../runtime/object.c ../runtime/object.h
	$(CC) $(C89) $< ../runtime/object.c -lm -o $@
../build/test-render_cache: ../runtime/render.h
../build/test-frame: ../runtime/frame.h
../build/test-checkpoint: ../runtime/save.h
../build/test-camera: camera.c ../runtime/camera.c
	$(CC) $(C89) $^ -lm -o $@
../build/test-sound: sound.c ../pipeline/template/src/audio.c ../runtime/object.c
	$(CC) $(C89) -Iconsole -I../runtime $^ -o $@
../build/test-audio: audio.c ../tools/audio.c
	$(CC) $(HOSTFLAGS) $^ $(HOSTLIBS) -o $@
../build/tests: resources.c ../tools/resources.c ../tools/export.c ../tools/assets.c
	$(CC) $(HOSTFLAGS) $^ $(HOSTLIBS) -o $@
