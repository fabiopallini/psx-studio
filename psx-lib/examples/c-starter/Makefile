ENGINE ?= ../..
ENGINE_ROOT := $(abspath $(ENGINE))
PSYQ ?=
.PHONY: all prepare clean
all:
	$(MAKE) -C "$(ENGINE_ROOT)"
	"$(ENGINE_ROOT)/build/psx-lib" --build-ps1 --root "$(ENGINE_ROOT)" --source-root "$(CURDIR)" --build-directory "$(CURDIR)/build" --max-objects 1 --render-triangles 256 --psyq "$(PSYQ)"
prepare:
	$(MAKE) -C "$(ENGINE_ROOT)"
	"$(ENGINE_ROOT)/build/psx-lib" --build-ps1 --root "$(ENGINE_ROOT)" --source-root "$(CURDIR)" --build-directory "$(CURDIR)/build" --max-objects 1 --render-triangles 256 --psyq "$(PSYQ)" --prepare-only
clean:
	$(RM) -r build
