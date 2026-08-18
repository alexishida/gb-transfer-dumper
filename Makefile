BUILD_DIR := build
SOURCE_DIR := src

LIBTRPAK_REPOSITORY := https://github.com/alexishida/libtrpak.git
LIBTRPAK_COMMIT := 4a55f4d567ee0cbbf805982089fdd5d325a72617
LIBTRPAK_DIR := .deps/libtrpak
LIBTRPAK_STAMP := $(LIBTRPAK_DIR)/.checked-out-$(LIBTRPAK_COMMIT)

include $(N64_INST)/include/n64.mk

CFLAGS += -I$(LIBTRPAK_DIR)

ROM := gb-transf-dumper.z64

# Every application source is compiled by n64.mk's own $(BUILD_DIR)/%.o rule,
# so new files under src/ are picked up without editing this list.
SOURCES := $(wildcard $(SOURCE_DIR)/*.c)
OBJS := $(patsubst $(SOURCE_DIR)/%.c,$(BUILD_DIR)/%.o,$(SOURCES)) \
	$(BUILD_DIR)/libtrpak.o

.PHONY: all clean

all: $(ROM)

$(ROM): N64_ROM_TITLE = "GB Transfer Dumper"
$(ROM): N64_ROM_REGIONFREE = true
$(ROM): N64_ROM_CONTROLLER1 = n64,pak=transfer

$(BUILD_DIR)/gb-transf-dumper.elf: $(OBJS)

$(LIBTRPAK_STAMP):
	@mkdir -p .deps
	@if [ ! -d "$(LIBTRPAK_DIR)/.git" ]; then \
		git clone --filter=blob:none --no-checkout "$(LIBTRPAK_REPOSITORY)" "$(LIBTRPAK_DIR)"; \
	fi
	@cd "$(LIBTRPAK_DIR)" && \
		git fetch --depth 1 origin "$(LIBTRPAK_COMMIT)" && \
		git checkout --detach "$(LIBTRPAK_COMMIT)"
	@touch "$@"

$(LIBTRPAK_DIR)/libtrpak.c $(LIBTRPAK_DIR)/libtrpak.h: $(LIBTRPAK_STAMP)
	@:

$(BUILD_DIR)/libtrpak.o: $(LIBTRPAK_DIR)/libtrpak.c $(LIBTRPAK_DIR)/libtrpak.h
	@mkdir -p $(dir $@)
	@echo "    [CC] $<"
	$(CC) -c $(CFLAGS) -o $@ $<

# Prerequisite-only rule: it makes the dependency checkout happen before any
# application source is compiled, while leaving n64.mk's recipe in charge.
$(OBJS): $(LIBTRPAK_DIR)/libtrpak.h

clean:
	rm -rf $(BUILD_DIR) $(ROM) *.v64 *.n64

-include $(wildcard $(BUILD_DIR)/*.d)
