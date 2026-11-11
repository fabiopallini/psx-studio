PYTHON ?= python3
CC ?= cc
PKGS = gio-2.0 json-glib-1.0 libpng
CPPFLAGS += $(shell pkg-config --cflags $(PKGS)) -Itools
CFLAGS ?= -O2 -g
CFLAGS += -std=c11 -Wall -Wextra -Wpedantic
LDLIBS += $(shell pkg-config --libs $(PKGS)) -lm
EXEEXT :=
ifeq ($(OS),Windows_NT)
EXEEXT := .exe
endif
SOURCES = tools/main.c tools/build.c tools/resources.c tools/assets.c tools/export.c tools/audio.c
OBJECTS = $(SOURCES:%.c=build/%.o)
.PHONY: all test clean
all: build/psx-lib$(EXEEXT)
build/psx-lib$(EXEEXT): $(OBJECTS)
	$(CC) $(CFLAGS) $(LDFLAGS) $^ $(LDLIBS) -o $@
build/%.o: %.c
	mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -MMD -MP -c $< -o $@
test: all
	$(MAKE) -C tests
clean:
	$(RM) -r build
-include $(OBJECTS:.o=.d)
