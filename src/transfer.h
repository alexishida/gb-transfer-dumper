/*
 * GB Transfer Dumper - streaming ROM and save transfers.
 *
 * Each transfer is performed by the matching libtrpak bulk helper, with the
 * blocks routed through this module's streaming callbacks instead of a buffer
 * sized for the whole ROM. Nothing larger than 4 KiB is ever held in RDRAM, so
 * no Expansion Pak is required, while cartridge readiness, Transfer Pak reset
 * recovery, mapper bank limits and MBC2 nibble handling stay in libtrpak.
 */

#ifndef TRANSFER_H
#define TRANSFER_H

#include <stddef.h>
#include <stdio.h>

/**
 * @brief Installs the file-streaming libtrpak backend.
 *
 * Must be called once at startup, before the first cart_refresh(): the backend
 * carries the Joybus primitives every libtrpak call needs, and installing it
 * later would discard the multicart detection performed by trpak_init().
 *
 * @retval TRPAK_OK The backend is installed; transfers may run.
 * @return The libtrpak failure code otherwise.
 */
int transfer_init(void);

/**
 * @brief Streams the whole cartridge ROM into an open file.
 *
 * @param file          Destination stream, opened for binary writing.
 * @param path          Path shown on the progress screen.
 * @param bytes_written Receives the number of bytes transferred, even on
 *                      failure, so a partial dump can be reported.
 * @retval TRPAK_OK                          Whole ROM written.
 * @retval TRPAK_ERR_UNSUPPORTED_CARTRIDGE   Mapper cannot address every bank.
 * @retval TRPAK_ERR_TRANSFER_TIMEOUT        The cartridge kept resetting.
 * @retval APP_ERR_FILE_WRITE                microSD write failed.
 * @return Other libtrpak codes when the cartridge stops responding.
 */
int transfer_dump_rom(FILE *file, const char *path, size_t *bytes_written);

/**
 * @brief Streams the whole cartridge save RAM into an open file.
 *
 * @param file          Destination stream, opened for binary writing.
 * @param path          Path shown on the progress screen.
 * @param bytes_written Receives the number of bytes transferred.
 * @retval TRPAK_OK                          Whole save RAM written.
 * @retval TRPAK_ERR_NO_RAM                  The cartridge has no save RAM.
 * @retval TRPAK_ERR_UNSUPPORTED_CARTRIDGE   Mapper cannot address every bank.
 * @retval APP_ERR_FILE_WRITE                microSD write failed.
 */
int transfer_dump_ram(FILE *file, const char *path, size_t *bytes_written);

/**
 * @brief Writes a save file back into cartridge RAM, verifying every block.
 *
 * The caller must have already checked that the file is exactly
 * `trcart.ramsize` bytes long.
 *
 * @param file       Source stream, opened for binary reading.
 * @param path       Path shown on the progress screen.
 * @param bytes_read Receives the number of bytes restored.
 * @retval TRPAK_OK                  Whole save restored and verified.
 * @retval TRPAK_ERR_NO_RAM          The cartridge has no save RAM.
 * @retval TRPAK_ERR_VERIFY_FAILED   A block read back differently.
 * @retval APP_ERR_FILE_READ         The file ended early or could not be read.
 */
int transfer_restore_ram(FILE *file, const char *path, size_t *bytes_read);

#endif /* TRANSFER_H */
