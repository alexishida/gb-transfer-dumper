BUILD_DIR := build
SOURCE_DIR := src

LIBTRPAK_REPOSITORY := https://github.com/alexishida/libtrpak.git
LIBTRPAK_COMMIT := 22aa35fb928247686b0f9dbcdf3f901768c96d70
LIBTRPAK_DIR := .deps/libtrpak
LIBTRPAK_STAMP := $(LIBTRPAK_DIR)/.checked-out-$(LIBTRPAK_COMMIT)

include $(N64_INST)/include/n64.mk

CFLAGS += -I$(LIBTRPAK_DIR)

ROM := gb-transf-dumper.z64
OBJS := \
	$(BUILD_DIR)/main.o \
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

$(BUILD_DIR)/main.o: src/main.c $(LIBTRPAK_DIR)/libtrpak.h

clean:
	rm -rf $(BUILD_DIR) $(ROM) *.v64 *.n64

-include $(wildcard $(BUILD_DIR)/*.d)
