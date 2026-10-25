PYTHON ?= python3
EXEEXT :=
OBJDIR := build
APP_LDFLAGS :=
ifeq ($(OS),Windows_NT)
EXEEXT := .exe
OBJDIR := build/windows
APP_LDFLAGS += -mwindows
endif
CC ?= cc
PKGS = gtk4 json-glib-1.0 epoxy libpng
CPPFLAGS += $(patsubst -I%,-isystem %,$(shell pkg-config --cflags $(PKGS))) -Ieditor
CFLAGS ?= -O2 -g
CFLAGS += -std=c11 -Wall -Wextra -Wpedantic
LDLIBS += $(shell pkg-config --libs $(PKGS)) -lm
SOURCES = editor/main.c editor/build.c editor/audio.c editor/scenes.c editor/project.c editor/viewport.c editor/export.c editor/assets.c
OBJECTS = $(SOURCES:editor/%.c=$(OBJDIR)/%.o) $(OBJDIR)/camera.o

.PHONY: all run test clean windows-package
all: build/psx-studio$(EXEEXT)
$(sort build/ $(OBJDIR)/):
	mkdir -p $@
$(OBJDIR)/%.o: editor/%.c editor/studio.h | $(OBJDIR)/
	$(CC) $(CPPFLAGS) $(CFLAGS) -MMD -MP -c $< -o $@
$(OBJDIR)/camera.o: runtime/camera.c runtime/camera.h | $(OBJDIR)/
	$(CC) $(CFLAGS) -c $< -o $@
build/psx-studio$(EXEEXT): $(OBJECTS) Makefile
	$(CC) $(CFLAGS) $(LDFLAGS) $(APP_LDFLAGS) $(OBJECTS) $(LDLIBS) -o $@
build/tests$(EXEEXT): tests/project.c editor/scenes.c editor/project.c editor/export.c editor/assets.c editor/studio.h | $(OBJDIR)/
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/project.c editor/scenes.c editor/project.c editor/export.c editor/assets.c $(LDLIBS) -o $@
build/windows/check-gl.exe: tools/check_windows_gl.c editor/viewport_shaders.h | $(OBJDIR)/
	$(CC) -std=c11 -Wall -Wextra -Werror -Ieditor tools/check_windows_gl.c -lEGL -lGLESv2 -o $@
windows-package: all build/windows/check-gl.exe
	powershell.exe -NoProfile -ExecutionPolicy Bypass -File tools/package_windows.ps1
run: all
	./build/psx-studio$(EXEEXT)
build/runtime-tests$(EXEEXT): tests/runtime.c runtime/object.c runtime/object.h | $(OBJDIR)/
	$(CC) -std=c89 -pedantic-errors -Wall -Wextra -Werror tests/runtime.c runtime/object.c -o $@
build/camera-tests$(EXEEXT): tests/camera.c runtime/camera.c runtime/camera.h | $(OBJDIR)/
	$(CC) -std=c89 -pedantic-errors -Wall -Wextra -Werror tests/camera.c runtime/camera.c -lm -o $@
$(OBJDIR)/camera-fixed.o: runtime/camera.c runtime/camera.h | $(OBJDIR)/
	$(CC) -std=c89 -pedantic-errors -Wall -Wextra -Werror -DPSX_RUNTIME_FIXED_ONLY -c $< -o $@
build/fps-tests$(EXEEXT): tests/fps.c runtime/fps.h | $(OBJDIR)/
	$(CC) -std=c89 -pedantic-errors -Wall -Wextra -Werror tests/fps.c -o $@
build/render-cache-tests$(EXEEXT): tests/render_cache.c runtime/render.h runtime/object.h | $(OBJDIR)/
	$(CC) -std=c89 -pedantic-errors -Wall -Wextra -Werror tests/render_cache.c -o $@
build/timing-tests$(EXEEXT): tests/timing.c runtime/timing.h | $(OBJDIR)/
	$(CC) -std=c89 -pedantic-errors -Wall -Wextra -Werror tests/timing.c -o $@
build/game-loop-tests$(EXEEXT): tests/game_loop.c tests/console/psx.h tests/console/scene.h pipeline/project-main.c runtime/timing.h runtime/object.h | $(OBJDIR)/
	$(CC) -std=c89 -pedantic-errors -Wall -Wextra -Werror -Itests/console tests/game_loop.c -o $@
build/frame-tests$(EXEEXT): tests/frame.c runtime/frame.h runtime/object.h | $(OBJDIR)/
	$(CC) -std=c89 -pedantic-errors -Wall -Wextra -Werror tests/frame.c -o $@
build/checkpoint-tests$(EXEEXT): tests/checkpoint.c runtime/save.h | $(OBJDIR)/
	$(CC) -std=c89 -pedantic-errors -Wall -Wextra -Werror tests/checkpoint.c -o $@
build/audio-tests$(EXEEXT): tests/audio.c editor/audio.c editor/audio.h | $(OBJDIR)/
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/audio.c editor/audio.c $(LDLIBS) -o $@
build/sound-tests$(EXEEXT): tests/sound.c tests/console/libspu.h pipeline/template/src/audio.c runtime/object.c runtime/object.h | $(OBJDIR)/
	$(CC) -std=c89 -pedantic-errors -Wall -Wextra -Werror -Itests/console -Iruntime tests/sound.c pipeline/template/src/audio.c runtime/object.c -o $@
build/input-tests$(EXEEXT): tests/input.c runtime/object.c runtime/object.h | $(OBJDIR)/
	$(CC) -std=c89 -pedantic-errors -Wall -Wextra -Werror tests/input.c runtime/object.c -lm -o $@
test: build/input-tests$(EXEEXT) build/sound-tests$(EXEEXT) build/audio-tests$(EXEEXT) build/psx-studio$(EXEEXT) build/tests$(EXEEXT) build/runtime-tests$(EXEEXT) build/camera-tests$(EXEEXT) $(OBJDIR)/camera-fixed.o build/fps-tests$(EXEEXT) build/render-cache-tests$(EXEEXT) build/timing-tests$(EXEEXT) build/game-loop-tests$(EXEEXT) build/frame-tests$(EXEEXT) build/checkpoint-tests$(EXEEXT)
	./build/input-tests$(EXEEXT)
	./build/sound-tests$(EXEEXT)
	./build/audio-tests$(EXEEXT)
	./build/tests$(EXEEXT)
	./build/runtime-tests$(EXEEXT)
	./build/camera-tests$(EXEEXT)
	./build/fps-tests$(EXEEXT)
	./build/render-cache-tests$(EXEEXT)
	./build/timing-tests$(EXEEXT)
	./build/game-loop-tests$(EXEEXT)
	./build/frame-tests$(EXEEXT)
	./build/checkpoint-tests$(EXEEXT)
	PYTHONDONTWRITEBYTECODE=1 $(PYTHON) tests/build_pipeline.py
clean:
	$(RM) -r build
-include $(OBJECTS:.o=.d)
