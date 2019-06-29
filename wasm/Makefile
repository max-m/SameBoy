CORE_DIR     += ..

BIN := $(CORE_DIR)/build/wasm_bin
OBJ := $(CORE_DIR)/build/wasm_obj
BOOTROMS_DIR ?= $(CORE_DIR)/build/bin/BootROMs

ifdef DATA_DIR
CFLAGS += -DDATA_DIR="\"$(DATA_DIR)\""
endif

# Set tools

CC := emcc

# Set compilation and linkage flags based on target, platform and configuration

CFLAGS += -Werror -Wall -Wno-strict-aliasing -Wno-unknown-warning -Wno-unknown-warning-option -Wno-multichar -Wno-int-in-bool-context -std=gnu11 -D_GNU_SOURCE -DVERSION="$(VERSION)" -I. -D_USE_MATH_DEFINES
# CFLAGS += -DGB_INTERNAL=1 # get access to internal APIs
CFLAGS += -I$(CORE_DIR)
CFLAGS += -s WASM=1 -s USE_SDL=2 --preload-file $(BOOTROMS_DIR)@/BootROMs --preload-file $(CORE_DIR)/Shaders@/Shaders -s "EXTRA_EXPORTED_RUNTIME_METHODS=['ccall', 'cwrap', 'getValue', 'AsciiToString', 'FS']"
CFLAGS += -s USE_WEBGL2=1
# CFLAGS += -Wcast-align -Wover-aligned -s SAFE_HEAP=1 -s WARN_UNALIGNED=1
WASM_LDFLAGS :=

LDFLAGS += -lc -lm -ldl
CFLAGS += -Wno-deprecated-declarations

ifeq ($(CONF),debug)
CFLAGS += -g -g4
CFLAGS += --cpuprofiler --memoryprofiler
else ifeq ($(CONF), release)
CFLAGS += -O3 -DNDEBUG --emit-symbol-map
CFLAGS += --llvm-lto 3 #  might be unstable
else
$(error Invalid value for CONF: $(CONF). Use "debug", "release" or "native_release")
endif

# Define our targets

all: bootroms $(BIN)/index.html $(WEB_OBJECTS) $(SHADERS)

bootroms: $(BOOTROMS_DIR)/agb_boot.bin \
          $(BOOTROMS_DIR)/cgb_boot.bin \
          $(BOOTROMS_DIR)/dmg_boot.bin \
          $(BOOTROMS_DIR)/sgb_boot.bin \
          $(BOOTROMS_DIR)/sgb2_boot.bin

# Get a list of our source files and their respective object file targets

CORE_SOURCES_RAW := $(shell ls $(CORE_DIR)/Core/*.c)
CORE_SOURCES := $(shell realpath --relative-to=$(CORE_DIR) $(CORE_SOURCES_RAW))
CORE_OBJECTS := $(patsubst %,$(OBJ)/%.o,$(CORE_SOURCES))

WASM_SOURCES := $(shell ls *.c)
WASM_OBJECTS := $(patsubst %,$(OBJ)/%.o,$(WASM_SOURCES))

WEB_SOURCES := $(shell ls ressources/.)
WEB_OBJECTS := $(patsubst %,$(BIN)/ressources/%,$(WEB_SOURCES))

SHADERS := $(shell ls $(CORE_DIR)/Shaders/*.fsh)

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
		$(LDFLAGS) \
		$(WASM_LDFLAGS)
ifeq ($(CONF), release)
endif

$(CORE_DIR)/build/bin/BootROMs/%_boot.bin:
	$(MAKE) -C $(CORE_DIR) $(patsubst $(CORE_DIR)/%,%,$@)

clean:
	rm -f $(WASM_OBJECTS) $(BIN)/SameBoy.js $(BIN)/SameBoy.wasm
