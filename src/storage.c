/*
 * GB Transfer Dumper - microSD mounting and dump file management.
 */

#include "storage.h"

#include <libdragon.h>

#include <dir.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include "app.h"
#include "cart.h"

#define ROM_DUMP_DIRECTORY      "sd:/romdump"
#define SAVE_DUMP_DIRECTORY     "sd:/savedump"
#define DUMP_DIRECTORY_FALLBACK "sd:/"

#define SAVE_EXTENSION     ".sav"
#define SAVE_EXTENSION_LEN 4u

/* Highest `-NN` suffix storage_open_unique_file() will try. */
#define MAX_NAME_SUFFIX 99u

static bool sd_ready;
static const char *rom_directory = DUMP_DIRECTORY_FALLBACK;
static const char *save_directory = DUMP_DIRECTORY_FALLBACK;

int storage_ensure_sd(void)
{
    if (sd_ready) {
        return TRPAK_OK;
    }

    sd_ready = debug_init_sdfs("sd:/", -1);
    if (sd_ready) {
        debug_log(DEBUG_LEVEL_INFO, "SD card mounted successfully\n");
    } else {
        debug_log(DEBUG_LEVEL_ERROR, "Failed to mount SD card\n");
    }
    return sd_ready ? TRPAK_OK : APP_ERR_SD_MOUNT;
}

bool storage_is_ready(void)
{
    return sd_ready;
}

const char *storage_directory(storage_kind kind)
{
    return kind == STORAGE_KIND_SAVE ? save_directory : rom_directory;
}

int storage_ensure_directory(storage_kind kind)
{
    const char *directory = kind == STORAGE_KIND_SAVE
        ? SAVE_DUMP_DIRECTORY : ROM_DUMP_DIRECTORY;
    const char **active = kind == STORAGE_KIND_SAVE
        ? &save_directory : &rom_directory;
    struct stat status;
    int result = storage_ensure_sd();

    if (result != TRPAK_OK) {
        return result;
    }

    /* stat() is only required to set errno when it fails, and the SD backend
     * may leave it untouched, so clear it first rather than trust a value
     * carried over from an unrelated call. */
    errno = 0;
    if (stat(directory, &status) == 0) {
        if (S_ISDIR(status.st_mode)) {
            *active = directory;
            return TRPAK_OK;
        }
        debug_log(DEBUG_LEVEL_ERROR,
                  "Dump path exists but is not a directory: %s\n", directory);
        return APP_ERR_SD_DIRECTORY;
    }

    if (errno == ENOENT || errno == 0) {
        *active = DUMP_DIRECTORY_FALLBACK;
        debug_log(DEBUG_LEVEL_INFO, "%s does not exist; writing to %s\n",
                  directory, *active);
        return TRPAK_OK;
    }

    debug_log(DEBUG_LEVEL_ERROR, "Unable to inspect directory %s: %s\n",
              directory, strerror(errno));
    return APP_ERR_SD_DIRECTORY;
}

/**
 * @brief Reports whether a path exists.
 *
 * @param path   Path to test.
 * @param exists Receives the answer; untouched on failure.
 * @retval TRPAK_OK           Answer stored in @p exists.
 * @retval APP_ERR_FILE_OPEN  The path could not be inspected.
 */
static int path_exists(const char *path, bool *exists)
{
    struct stat status;

    errno = 0;
    if (stat(path, &status) == 0) {
        *exists = true;
        return TRPAK_OK;
    }
    if (errno == ENOENT || errno == 0) {
        *exists = false;
        return TRPAK_OK;
    }
    return APP_ERR_FILE_OPEN;
}

/** @brief Separator to insert between a directory and a file name. */
static const char *path_separator(const char *directory)
{
    size_t length = strlen(directory);
    return (length > 0u && directory[length - 1u] == '/') ? "" : "/";
}

int storage_open_unique_file(const char *directory, const char *extension,
                             char *path, FILE **file)
{
    char title[CART_SAFE_TITLE_SIZE];
    const char *separator = path_separator(directory);
    unsigned int suffix;

    cart_safe_title(title, sizeof(title));
    *file = NULL;

    for (suffix = 0u; suffix <= MAX_NAME_SUFFIX; suffix++) {
        bool exists;
        int result;
        int length;

        if (suffix == 0u) {
            length = snprintf(path, APP_PATH_SIZE, "%s%s%s.%s",
                              directory, separator, title, extension);
        } else {
            length = snprintf(path, APP_PATH_SIZE, "%s%s%s-%02u.%s",
                              directory, separator, title, suffix, extension);
        }
        if (length < 0 || length >= APP_PATH_SIZE) {
            debug_log(DEBUG_LEVEL_ERROR, "Path truncation: %d\n", length);
            return APP_ERR_FILE_OPEN;
        }

        result = path_exists(path, &exists);
        if (result != TRPAK_OK) {
            return result;
        }
        if (exists) {
            debug_log(DEBUG_LEVEL_VERBOSE, "File exists: %s\n", path);
            continue;
        }

        *file = fopen(path, "wb");
        if (*file == NULL) {
            return APP_ERR_FILE_OPEN;
        }
        debug_log(DEBUG_LEVEL_INFO, "Created file: %s\n", path);
        return TRPAK_OK;
    }
    return APP_ERR_NAME_EXHAUSTED;
}

/** @brief Case-insensitive test for a name ending in ".sav". */
static bool has_save_extension(const char *name, size_t name_length)
{
    const char *tail;
    size_t i;

    if (name_length < SAVE_EXTENSION_LEN) {
        return false;
    }
    tail = &name[name_length - SAVE_EXTENSION_LEN];
    for (i = 0u; i < SAVE_EXTENSION_LEN; i++) {
        char lowered = tail[i];
        if (lowered >= 'A' && lowered <= 'Z') {
            lowered = (char)(lowered - 'A' + 'a');
        }
        if (lowered != SAVE_EXTENSION[i]) {
            return false;
        }
    }
    return true;
}

/**
 * @brief Tests whether a `.sav` name was generated for the given title.
 *
 * Matches exactly the two shapes storage_open_unique_file() produces:
 * `TITLE.sav` and `TITLE-NN.sav`.
 *
 * @param name  File name, already known to end in ".sav".
 * @param title Sanitized cartridge title.
 * @return true when the name belongs to that cartridge.
 */
static bool save_name_matches_title(const char *name, const char *title)
{
    size_t title_length = strlen(title);
    size_t name_length = strlen(name);
    const char *rest;
    size_t rest_length;

    if (title_length == 0u || name_length <= title_length ||
        strncmp(name, title, title_length) != 0) {
        return false;
    }

    rest = &name[title_length];
    rest_length = name_length - title_length;

    /* "TITLE.sav" */
    if (rest_length == SAVE_EXTENSION_LEN) {
        return true;
    }
    /* "TITLE-NN.sav" */
    return rest_length == SAVE_EXTENSION_LEN + 3u &&
           rest[0] == '-' &&
           rest[1] >= '0' && rest[1] <= '9' &&
           rest[2] >= '0' && rest[2] <= '9';
}

/**
 * @brief Joins a directory and a file name into a caller buffer.
 *
 * @return false when the result would not fit in @p path_size.
 */
static bool join_path(const char *directory, const char *name,
                      char *path, size_t path_size)
{
    const char *separator = path_separator(directory);
    int length = snprintf(path, path_size, "%s%s%s", directory, separator, name);

    return length >= 0 && (size_t)length < path_size;
}

bool storage_find_save_file(const char *directory, char *path, size_t path_size)
{
    char title[CART_SAFE_TITLE_SIZE];
    char fallback[APP_PATH_SIZE];
    bool has_fallback = false;
    dir_t entry;
    int result;

    cart_safe_title(title, sizeof(title));

    for (result = dir_findfirst(directory, &entry); result == 0;
         result = dir_findnext(directory, &entry)) {
        if (entry.d_type != DT_REG ||
            !has_save_extension(entry.d_name, strlen(entry.d_name))) {
            continue;
        }
        if (save_name_matches_title(entry.d_name, title)) {
            if (join_path(directory, entry.d_name, path, path_size)) {
                debug_log(DEBUG_LEVEL_INFO,
                          "Selected save for this cartridge: %s\n", path);
                return true;
            }
            continue;
        }
        if (!has_fallback) {
            has_fallback = join_path(directory, entry.d_name,
                                     fallback, sizeof(fallback));
        }
    }

    if (has_fallback && strlen(fallback) < path_size) {
        memcpy(path, fallback, strlen(fallback) + 1u);
        debug_log(DEBUG_LEVEL_INFO,
                  "No save matches this cartridge; using %s\n", path);
        return true;
    }

    debug_log(DEBUG_LEVEL_INFO, "No .sav files found in %s\n", directory);
    return false;
}

int storage_validate_save_file(const char *path)
{
    struct stat status;

    errno = 0;
    if (stat(path, &status) != 0) {
        debug_log(DEBUG_LEVEL_ERROR, "Restore file not found: %s\n", path);
        return APP_ERR_FILE_NOT_FOUND;
    }
    if (status.st_size < 0 ||
        (unsigned long long)status.st_size != (unsigned long long)trcart.ramsize) {
        debug_log(DEBUG_LEVEL_ERROR, "File size mismatch: %lu vs %lu\n",
                  (unsigned long)status.st_size, (unsigned long)trcart.ramsize);
        return APP_ERR_FILE_SIZE_MISMATCH;
    }
    debug_log(DEBUG_LEVEL_INFO, "Restore file validated: %s (%lu bytes)\n",
              path, (unsigned long)status.st_size);
    return TRPAK_OK;
}
