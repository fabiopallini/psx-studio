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

.PHONY: all studio lib run test test-studio test-lib clean clean-studio clean-lib windows-package
all: lib studio
studio: build/psx-studio$(EXEEXT)
lib:
	$(MAKE) -C psx-lib
$(sort build/ $(OBJDIR)/):
	mkdir -p $@
$(OBJDIR)/%.o: editor/%.c editor/studio.h editor/game-main.h | $(OBJDIR)/
	$(CC) $(CPPFLAGS) $(CFLAGS) -MMD -MP -c $< -o $@
$(OBJDIR)/camera.o: editor/camera.c editor/camera.h | $(OBJDIR)/
	$(CC) $(CFLAGS) -c $< -o $@
build/psx-studio$(EXEEXT): $(OBJECTS) Makefile
	$(CC) $(CFLAGS) $(LDFLAGS) $(APP_LDFLAGS) $(OBJECTS) $(LDLIBS) -o $@
build/tests$(EXEEXT): tests/project.c editor/scenes.c editor/project.c editor/export.c editor/assets.c editor/studio.h editor/game-main.h | $(OBJDIR)/
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/project.c editor/scenes.c editor/project.c editor/export.c editor/assets.c $(LDLIBS) -o $@
build/viewport-tests$(EXEEXT): tests/viewport.c editor/viewport.c editor/project.c editor/scenes.c editor/assets.c editor/camera.c editor/studio.h editor/viewport_shaders.h | $(OBJDIR)/
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/viewport.c editor/viewport.c editor/project.c editor/scenes.c editor/assets.c editor/camera.c $(LDLIBS) -o $@
build/windows/check-gl.exe: tools/check_windows_gl.c editor/viewport_shaders.h | $(OBJDIR)/
	$(CC) -std=c11 -Wall -Wextra -Werror -Ieditor tools/check_windows_gl.c -lEGL -lGLESv2 -o $@
windows-package: all build/windows/check-gl.exe
	powershell.exe -NoProfile -ExecutionPolicy Bypass -File tools/package_windows.ps1
run: all
	./build/psx-studio$(EXEEXT)
build/audio-tests$(EXEEXT): tests/audio.c editor/audio.c editor/audio.h | $(OBJDIR)/
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/audio.c editor/audio.c $(LDLIBS) -o $@
test: test-lib test-studio
test-lib: lib
	$(MAKE) -C psx-lib test
test-studio: lib build/viewport-tests$(EXEEXT) build/audio-tests$(EXEEXT) build/psx-studio$(EXEEXT) build/tests$(EXEEXT)
	./build/viewport-tests$(EXEEXT)
	./build/audio-tests$(EXEEXT)
	./build/tests$(EXEEXT)
	PYTHONDONTWRITEBYTECODE=1 $(PYTHON) tests/build_pipeline.py
clean: clean-studio clean-lib
clean-studio:
	$(RM) -r build
clean-lib:
	$(MAKE) -C psx-lib clean
-include $(OBJECTS:.o=.d)
