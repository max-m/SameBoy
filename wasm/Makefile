# Make hacks
.INTERMEDIATE:

# Set target, configuration, version and destination folders

PLATFORM := $(shell uname -s)
ifneq ($(findstring MINGW,$(PLATFORM)),)
PLATFORM := windows32
USE_WINDRES := true
endif

ifneq ($(findstring MSYS,$(PLATFORM)),)
PLATFORM := windows32
endif

ifeq ($(PLATFORM),windows32)
_ := $(shell chcp 65001)
endif

DEFAULT := wasm
default: $(DEFAULT)

ifeq ($(MAKECMDGOALS),)
MAKECMDGOALS := $(DEFAULT)
endif

CORE_DIR     += ..

CFLAGS += -DGB_VERSION=\"$(VERSION)\"
CONF ?= debug

BIN := $(CORE_DIR)/build/wasm_bin
OBJ := $(CORE_DIR)/build/wasm_obj
BOOTROMS_DIR ?= $(CORE_DIR)/build/bin/BootROMs

ifdef DATA_DIR
CFLAGS += -DDATA_DIR="\"$(DATA_DIR)\""
endif

# Set tools

CC := emcc

ifeq ($(PLATFORM),windows32)
# To force use of the Unix version instead of the Windows version
MKDIR := $(shell which mkdir)
else
MKDIR := mkdir
endif

ifeq ($(CONF),native_release)
override CONF := release
endif

# Set compilation and linkage flags based on target, platform and configuration

CFLAGS += -Werror -Wall -Wno-strict-aliasing -Wno-unknown-warning -Wno-unknown-warning-option -Wno-multichar -Wno-int-in-bool-context -std=gnu11 -D_GNU_SOURCE -DVERSION="$(VERSION)" -I. -D_USE_MATH_DEFINES
# CFLAGS += -DGB_INTERNAL=1 # get access to internal APIs
CFLAGS += -I$(CORE_DIR)
# CFLAGS += -Wcast-align -Wover-aligned -s SAFE_HEAP=1 -s WARN_UNALIGNED=1
# -Werror implies -Wpass-failed=transform-warning and for some reason Clang fails to unroll some loops in apu.c
CFLAGS += -Wno-pass-failed
CFLAGS += -s USE_SDL=2

# Build as library
LDFLAGS += --no-entry
# Enable WASM and a virtual filesystem
LDFLAGS += -s WASM=1 -lidbfs.js
LDFLAGS += -s ALLOW_MEMORY_GROWTH=1
# Enable WebGL 2 support
LDFLAGS += -s USE_WEBGL2=1

LDFLAGS += --preload-file $(BOOTROMS_DIR)@/BootROMs --preload-file $(CORE_DIR)/Shaders@/Shaders
LDFLAGS += -s "EXPORTED_RUNTIME_METHODS=['ccall', 'cwrap', 'getValue', 'AsciiToString', 'FS']"

LDFLAGS += -lc -lm -ldl
CFLAGS += -Wno-deprecated-declarations

ifeq ($(CONF),debug)
CFLAGS += -g -g4
CFLAGS += --cpuprofiler --memoryprofiler
else ifeq ($(CONF), release)
CFLAGS += -O3 -flto -DNDEBUG
LDFLAGS += -flto --emit-symbol-map
else
$(error Invalid value for CONF: $(CONF). Use "debug", "release" or "native_release")
endif

# Define our targets

bootroms: $(BOOTROMS_DIR)/agb_boot.bin \
          $(BOOTROMS_DIR)/cgb_boot.bin \
          $(BOOTROMS_DIR)/cgb0_boot.bin \
          $(BOOTROMS_DIR)/dmg_boot.bin \
          $(BOOTROMS_DIR)/sgb_boot.bin \
          $(BOOTROMS_DIR)/sgb2_boot.bin

# Get a list of our source files and their respective object file targets

CORE_SOURCES_RAW := $(shell ls $(CORE_DIR)/Core/*.c)
CORE_SOURCES := $(shell realpath --relative-to=$(CORE_DIR) $(CORE_SOURCES_RAW))
CORE_OBJECTS := $(patsubst %,$(OBJ)/%.o,$(CORE_SOURCES))

WASM_SOURCES := $(shell ls *.c) $(CORE_DIR)/SDL/audio/sdl.c
WASM_OBJECTS := $(patsubst %,$(OBJ)/%.o,$(WASM_SOURCES))

WEB_SOURCES := $(shell ls ressources/.)
WEB_OBJECTS := $(patsubst %,$(BIN)/ressources/%,$(WEB_SOURCES))

SHADERS := $(shell ls $(CORE_DIR)/Shaders/*.fsh)

wasm: bootroms $(BIN)/index.html $(WEB_OBJECTS) $(SHADERS)
all: wasm

# Automatic dependency generation

ifneq ($(filter-out clean %.bin, $(MAKECMDGOALS)),)
-include $(CORE_OBJECTS:.o=.dep)
ifneq ($(filter $(MAKECMDGOALS),wasm),)
-include $(WASM_OBJECTS:.o=.dep)
endif
endif

$(OBJ)/%.dep: %
	-@$(MKDIR) -p $(dir $@)
	$(CC) $(CFLAGS) -MT $(OBJ)/$^.o -M $^ -c -o $@

$(CORE_DIR)/build/bin/BootROMs/%_boot.bin:
	$(MAKE) -C $(CORE_DIR) $(patsubst $(CORE_DIR)/%,%,$@)

# Compilation rules

$(OBJ)/Core/%.c.o: $(CORE_DIR)/Core/%.c
	-@$(MKDIR) -p $(dir $@)
	$(CC) $(CFLAGS) -DGB_INTERNAL -c $< -o $@

$(OBJ)/%.c.o: %.c
	-@$(MKDIR) -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BIN)/ressources/%:
	-@$(MKDIR) -p $(dir $@)
	cp -a $(patsubst $(BIN)/%,%,$@) $@

$(BIN)/index.html: $(CORE_OBJECTS) $(WASM_OBJECTS) index-shell.html main.js
	-@$(MKDIR) -p $(dir $@)
	$(CC) \
		$(CFLAGS) \
		$(filter %.o, $^) \
		-o $@ \
		--shell-file "index-shell.html" \
		--post-js "main.js" \
		$(LDFLAGS)
ifeq ($(CONF), release)
endif

$(CORE_DIR)/build/bin/BootROMs/%_boot.bin:
	$(MAKE) -C $(CORE_DIR) $(patsubst $(CORE_DIR)/%,%,$@)

clean:
	rm -f $(WASM_OBJECTS) $(BIN)/SameBoy.js $(BIN)/SameBoy.wasm
