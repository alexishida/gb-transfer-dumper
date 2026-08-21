/*
 * GB Transfer Dumper - Transfer Pak lifecycle and cartridge metadata.
 *
 * Owns the accessory power state and the display/filename strings derived from
 * the cartridge header that libtrpak parsed into the global ::trcart. Cartridge
 * readiness during a transfer is libtrpak's responsibility, not this module's.
 */

#ifndef CART_H
#define CART_H

#include <stdbool.h>
#include <stddef.h>

#include "libtrpak.h"

/* Capacity required by cart_safe_title(), including the terminator. */
#define CART_SAFE_TITLE_SIZE 32

/**
 * @brief Powers the Transfer Pak up and re-reads the cartridge header.
 *
 * Any previously powered cartridge is shut down first. Every controller port
 * (1-4) is probed in order until one carries a Transfer Pak with a readable
 * cartridge, so the accessory can live in a spare port while port 1 drives
 * the menus.
 *
 * @return ::TRPAK_OK when a cartridge was identified, otherwise the libtrpak
 *         failure code from the last trpak_init() attempt.
 */
int cart_refresh(void);

/** @brief Powers the Transfer Pak down if it is currently on. */
void cart_shutdown(void);

/** @brief True while the Transfer Pak is powered by this application. */
bool cart_is_active(void);

/** @brief True once a cartridge header has been read successfully. */
bool cart_is_known(void);

/** @brief Controller port index where the Transfer Pak was last found. */
int cart_controller_port(void);

/** @brief Cartridge title, or a placeholder when the header carries none. */
const char *cart_title(void);

/** @brief "Game Boy", "Game Boy / Color" or "Game Boy Color". */
const char *cart_system_name(void);

/** @brief Display name for a `TRPAK_MAPPER_*` identifier. */
const char *cart_mapper_name(unsigned char mapper);

/** @brief File extension for a ROM dump of the current cartridge. */
const char *cart_rom_extension(void);

/** @brief True when the cartridge has usable, addressable save RAM. */
bool cart_has_ram(void);

/**
 * @brief Builds a filesystem-safe base name from the cartridge title.
 *
 * Keeps `[A-Za-z0-9_-]`, folds every other byte to a single underscore, trims
 * trailing underscores, and falls back to "cartridge" when nothing is left.
 *
 * @param output Buffer of at least ::CART_SAFE_TITLE_SIZE bytes.
 * @param size   Capacity of @p output.
 */
void cart_safe_title(char *output, size_t size);

#endif /* CART_H */
