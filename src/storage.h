/*
 * GB Transfer Dumper - microSD mounting and dump file management.
 *
 * Current libdragon SD filesystem builds cannot create directories, so each
 * dump kind uses its dedicated directory when it already exists and silently
 * falls back to the card root when it does not.
 */

#ifndef STORAGE_H
#define STORAGE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

/** @brief Which set of dumps a path request refers to. */
typedef enum {
    STORAGE_KIND_ROM = 0, /**< `.gb` / `.gbc` ROM dumps. */
    STORAGE_KIND_SAVE     /**< `.sav` save backups and restores. */
} storage_kind;

/**
 * @brief Mounts the microSD card if it is not mounted yet.
 *
 * @retval TRPAK_OK          Card is mounted.
 * @retval APP_ERR_SD_MOUNT  Mounting failed.
 */
int storage_ensure_sd(void);

/** @brief True once the microSD card has been mounted successfully. */
bool storage_is_ready(void);

/**
 * @brief Mounts the card and resolves the active directory for a dump kind.
 *
 * @param kind Which directory to resolve.
 * @retval TRPAK_OK              Directory resolved; see storage_directory().
 * @retval APP_ERR_SD_MOUNT      The card could not be mounted.
 * @retval APP_ERR_SD_DIRECTORY  The path exists but is not usable.
 */
int storage_ensure_directory(storage_kind kind);

/**
 * @brief Directory currently in use for a dump kind.
 *
 * Valid only after storage_ensure_directory() returned ::TRPAK_OK; before that
 * it reports the card root fallback.
 *
 * @param kind Which directory to report.
 * @return Path string ending without a separator, e.g. "sd:/romdump".
 */
const char *storage_directory(storage_kind kind);

/**
 * @brief Creates the next unused dump file for the current cartridge.
 *
 * Tries `TITLE.ext` first, then `TITLE-01.ext` through `TITLE-99.ext`, so an
 * existing dump is never overwritten.
 *
 * @param directory Directory to create the file in.
 * @param extension Extension without the dot.
 * @param path      Receives the created path; ::APP_PATH_SIZE bytes.
 * @param file      Receives the open `"wb"` stream, or NULL on failure.
 * @retval TRPAK_OK                Created; caller owns @p file.
 * @retval APP_ERR_FILE_OPEN       The path could not be built or opened.
 * @retval APP_ERR_NAME_EXHAUSTED  All 100 candidate names are taken.
 */
int storage_open_unique_file(const char *directory, const char *extension,
                             char *path, FILE **file);

/**
 * @brief Picks the save file to restore from a directory.
 *
 * Prefers a `.sav` whose name was produced by this program for the cartridge
 * currently inserted (`TITLE.sav` or `TITLE-NN.sav`), which keeps a directory
 * holding several saves from restoring the wrong game. When no such file
 * exists it falls back to the first `.sav` the filesystem returns.
 *
 * @param directory Directory to scan.
 * @param path      Receives the selected path.
 * @param path_size Capacity of @p path.
 * @return true when a file was selected.
 */
bool storage_find_save_file(const char *directory, char *path, size_t path_size);

/**
 * @brief Checks that a save file matches the inserted cartridge's RAM size.
 *
 * RTC cartridges also accept recognized 44-byte or 48-byte emulator RTC
 * trailers after the SRAM payload. The restore path ignores that trailer.
 *
 * @param path File to inspect.
 * @retval TRPAK_OK                    SRAM payload and optional RTC trailer match.
 * @retval APP_ERR_FILE_NOT_FOUND      The file could not be inspected.
 * @retval APP_ERR_FILE_SIZE_MISMATCH  The size differs from the cartridge RAM.
 */
int storage_validate_save_file(const char *path);

#endif /* STORAGE_H */
