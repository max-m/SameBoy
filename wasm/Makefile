# Make hacks
.INTERMEDIATE:

# Set target, configuration, version and destination folders

ifeq ($(PLATFORM),windows32)
_ := $(shell chcp 65001)
endif

DEFAULT := wasm
default: $(DEFAULT)

ifeq ($(MAKECMDGOALS),)
MAKECMDGOALS := $(DEFAULT)
endif

CONF ?= debug
CORE_DIR += ..

GIT := $(shell command -v git 2> /dev/null)
ifdef GIT
GIT_COMMIT   := $(shell $(GIT) describe --always --dirty=-modified)
LONG_VERSION := $(VERSION) ($(GIT_COMMIT))
else
LONG_VERSION := $(VERSION)
endif

ifdef GIT_COMMIT
CFLAGS += -DGB_GIT_COMMIT='"$(GIT_COMMIT)"'
endif
CFLAGS += -DGB_LONG_VERSION='"$(LONG_VERSION)"'

BIN := $(CORE_DIR)/build/wasm_bin
OBJ := $(CORE_DIR)/build/wasm_obj
BOOTROMS_DIR ?= $(CORE_DIR)/build/bin/BootROMs

# Set tools

CC := emcc

# Set compilation and linkage flags based on target, platform and configuration

CFLAGS += $(WARNINGS)

CFLAGS += -std=gnu11 -D_GNU_SOURCE -DGB_VERSION='"$(VERSION)"' -DGB_COPYRIGHT_YEAR='"$(COPYRIGHT_YEAR)"' -I. -D_USE_MATH_DEFINES

CFLAGS += -I$(CORE_DIR)
# CFLAGS += -Wcast-align -Wover-aligned -s SAFE_HEAP=1 -s WARN_UNALIGNED=1

# -Werror implies -Wpass-failed=transform-warning and for some reason Clang fails to unroll some loops in apu.c
CFLAGS += -Wno-pass-failed

CFLAGS += -s DEFAULT_TO_CXX=0
CFLAGS += -s USE_SDL=2

CFLAGS += -DGB_DISABLE_TIMEKEEPING -DGB_DISABLE_DEBUGGER -DGB_DISABLE_CHEATS
CFLAGS += -DGB_ENABLE_CHAMELEON_MODE

ifndef ENABLE_REWIND
	CFLAGS += -DGB_DISABLE_REWIND
endif

# Build as library
LDFLAGS += --no-entry
# Enable WASM and a virtual filesystem
LDFLAGS += -s WASM=1 -lidbfs.js
LDFLAGS += -s ALLOW_MEMORY_GROWTH=1
# Enable WebGL 2 support
LDFLAGS += -lEGL -lGL -s MAX_WEBGL_VERSION=2
# Create an ES6 Module
LDFLAGS += -s MODULARIZE=1 -s EXPORT_ES6=1 -s EXPORT_NAME=initSameBoy

# If set to 1, perform acorn pass that converts each HEAP access into a
# function call that uses DataView to enforce LE byte order for HEAP buffer;
# This makes generated JavaScript run on BE as well as LE machines. (If 0, only
# LE systems are supported). Does not affect generated wasm.
# LDFLAGS += -s SUPPORT_BIG_ENDIAN=1

LDFLAGS += -s LLD_REPORT_UNDEFINED
LDFLAGS += -s "EXPORTED_RUNTIME_METHODS=['ccall', 'cwrap', 'getValue', 'AsciiToString', 'FS']"

LDFLAGS += -lc -lm -ldl

ifeq ($(CONF),debug)
CFLAGS += -g -gseparate-dwarf
LDFLAGS += --emit-symbol-map --source-map-base ./
LDFLAGS += -s RETAIN_COMPILER_SETTINGS=1

CFLAGS += -s STRICT=1
# SDL2 uses symbols (behind a runtime check) that are only available when built with ASYNCIFY=1
LDFLAGS += -s ERROR_ON_UNDEFINED_SYMBOLS=0
CFLAGS += -Wno-error=js-compiler
else ifeq ($(CONF),release)
CFLAGS += -O3 -flto -DNDEBUG
LDFLAGS += -flto
LDFLAGS += -s GL_TRACK_ERRORS=0
else
$(error Invalid value for CONF: $(CONF). Use "debug", "release" or "native_release")
endif

CFLAGS := $(CFLAGS) $(CFLAGS_OVERRIDES)
LDFLAGS := $(LDFLAGS) $(LDFLAGS_OVERRIDES)

# Define our targets

BOOTROM_SOURCES_RAW := $(shell ls $(CORE_DIR)/BootROMs/*_boot.asm)
BOOTROM_SOURCES := $(shell realpath --relative-to=$(CORE_DIR) $(BOOTROM_SOURCES_RAW))
BOOTROM_OBJECTS := $(patsubst BootROMs/%.asm,$(BOOTROMS_DIR)/%.bin,$(BOOTROM_SOURCES))

# Incompatible with `make -jN`
bootroms: $(BOOTROM_OBJECTS)

# Get a list of our source files and their respective object file targets

# CORE_SOURCES_RAW := $(shell ls $(CORE_DIR)/Core/*.c)
CORE_SOURCES := \
	$(CORE_DIR)/Core/gb.c \
	$(CORE_DIR)/Core/sgb.c \
	$(CORE_DIR)/Core/apu.c \
	$(CORE_DIR)/Core/memory.c \
	$(CORE_DIR)/Core/mbc.c \
	$(CORE_DIR)/Core/timing.c \
	$(CORE_DIR)/Core/display.c \
	$(CORE_DIR)/Core/symbol_hash.c \
	$(CORE_DIR)/Core/camera.c \
	$(CORE_DIR)/Core/sm83_cpu.c \
	$(CORE_DIR)/Core/joypad.c \
	$(CORE_DIR)/Core/save_state.c \
	$(CORE_DIR)/Core/random.c \
	$(CORE_DIR)/Core/rumble.c \
	$(CORE_DIR)/Core/printer.c \
	$(CORE_DIR)/Core/workboy.c

ifdef ENABLE_REWIND
	CORE_SOURCES += $(CORE_DIR)/Core/rewind.c
endif

CORE_OBJECTS := $(patsubst $(CORE_DIR)/%,$(OBJ)/%.o,$(CORE_SOURCES))

SDL_SOURCES := \
	$(CORE_DIR)/SDL/configuration.c \
	$(CORE_DIR)/SDL/audio.c \
	$(CORE_DIR)/SDL/audio/sdl.c \
	$(CORE_DIR)/SDL/font.c \
	$(CORE_DIR)/SDL/gui.c \
	$(CORE_DIR)/SDL/utils.c
SDL_OBJECTS := $(patsubst $(CORE_DIR)/%,$(OBJ)/%.o,$(SDL_SOURCES))

WASM_SOURCES := $(shell ls src/*.c)
WASM_OBJECTS := $(patsubst %,$(OBJ)/%.o,$(WASM_SOURCES))

ICON_SIZES := 16x16 32x32 64x64 128x128 256x256 512x512
ICONS := $(foreach size,$(ICON_SIZES),$(BIN)/img/icon-$(size).png)

WEB_SOURCES := $(shell find web/ressources/ -type f)
WEB_OBJECTS := $(patsubst web/ressources/%,$(BIN)/%,$(WEB_SOURCES))

JS_SOURCES := $(filter-out _debug.js pre.js main.js,$(shell ls web/js/.))
JS_FILES := $(patsubst %,$(BIN)/js/%,$(JS_SOURCES))

SHADERS := $(shell ls $(CORE_DIR)/Shaders/*.fsh)
PALETTES := $(shell ls $(CORE_DIR)/Misc/Palettes/*.sbp | sed 's: :\\ :g')

EMCCFLAGS := --post-js "web/js/main.js"
ifeq ($(CONF),debug)
EMCCFLAGS += --pre-js "web/js/_debug.js"
endif
EMCCFLAGS += --pre-js "web/js/pre.js"
EMCCFLAGS += --preload-file $(BOOTROMS_DIR)@/BootROMs
EMCCFLAGS += --preload-file $(CORE_DIR)/Shaders@/Shaders
EMCCFLAGS += --preload-file $(CORE_DIR)/Misc/Palettes@/Palettes
EMCCFLAGS += --preload-file $(CORE_DIR)/SDL/background.bmp@/background.bmp

wasm: $(BIN)/index.html
all: wasm

# Automatic dependency generation

ifneq ($(filter-out clean %.bin, $(MAKECMDGOALS)),)
-include $(CORE_OBJECTS:.o=.dep)
ifneq ($(filter $(MAKECMDGOALS),wasm),)
-include $(SDL_OBJECTS:.o=.dep)
-include $(WASM_OBJECTS:.o=.dep)
endif
endif

$(OBJ)/SDL/%.dep: $(CORE_DIR)/SDL/%
	-@$(MKDIR) -p $(dir $@)
	$(CC) $(CFLAGS) -MT $(OBJ)/$^.o -M $^ -c -o $@

$(OBJ)/%.dep: %
	-@$(MKDIR) -p $(dir $@)
	$(CC) $(CFLAGS) -MT $(OBJ)/$^.o -M $^ -c -o $@

$(CORE_DIR)/build/bin/BootROMs/%_boot.bin: $(BOOTROM_SOURCES)
	$(MAKE) -C $(CORE_DIR) $(patsubst $(CORE_DIR)/%,%,$@)

# Compilation rules

$(OBJ)/Core/%.c.o: $(CORE_DIR)/Core/%.c
	-@$(MKDIR) -p $(dir $@)
	$(CC) $(CFLAGS) -DGB_INTERNAL -c $< -o $@

$(OBJ)/SDL/%.c.o: $(CORE_DIR)/SDL/%.c
	-@$(MKDIR) -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ)/src/%.c.o: src/%.c
	-@$(MKDIR) -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BIN)/img/icon-%.png: $(CORE_DIR)/FreeDesktop/AppIcon/%.png
	-@$(MKDIR) -p $(dir $@)
	cp $< $@

$(BIN)/js/%.js: web/js/%.js
	-@$(MKDIR) -p $(dir $@)
	cp $< $@

$(BIN)/service_worker.js: $(JS_FILES) $(ICONS) $(WEB_OBJECTS)
	# This script prepends the version number (plus git info, if available),
	# the names of the files that should be cached to our service worker “template”
	# and saves the result in $(BIN)/service_worker.js
	sh create_service_worker.sh "$(BIN)" "$(VERSION)" "$(GIT_COMMIT)" \
		$(BIN)/index.html $(BIN)/index.js $(BIN)/index.data $(BIN)/index.wasm \
		$(BIN)/sameboy.webmanifest \
		$(filter-out $(wildcard $(BIN)/demos/*),$^)

$(BIN)/index.html: web/index.html $(BIN)/index.js $(BIN)/sameboy.webmanifest $(JS_FILES) $(ICONS) $(WEB_OBJECTS)
	-@$(MKDIR) -p $(dir $@)
	sed "s/@VERSION/$(LONG_VERSION)/;s/@COPYRIGHT_YEAR/$(COPYRIGHT_YEAR)/" < web/index.html > $@

$(BIN)/index.js: $(CORE_OBJECTS) $(SDL_OBJECTS) $(WASM_OBJECTS) $(SHADERS) $(PALETTES) $(BOOTROM_OBJECTS) web/js/_debug.js web/js/pre.js web/js/main.js $(BIN)/service_worker.js $(CORE_DIR)/SDL/background.bmp
	-@$(MKDIR) -p $(dir $@)
	$(CC) \
		$(CFLAGS) \
		$(LDFLAGS) \
		$(EMCCFLAGS) \
		$(filter %.o, $^) \
		-o $@

$(BIN)/%: web/ressources/%
	-@$(MKDIR) -p $(dir $@)
	cp -a $< $@

$(BIN)/sameboy.webmanifest: web/sameboy.webmanifest
	-@$(MKDIR) -p $(dir $@)
	cp -a $< $@

$(CORE_DIR)/build/bin/BootROMs/%_boot.bin:
	$(MAKE) -C $(CORE_DIR) $(patsubst $(CORE_DIR)/%,%,$@)

clean:
	rm -rf $(BIN) $(OBJ) $(BOOTROMS_DIR)
