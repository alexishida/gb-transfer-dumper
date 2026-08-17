/*
 * GB Transfer Dumper - Nintendo 64 Homebrew
 *
 * This application dumps and restores Game Boy cartridges through the
 * Nintendo 64 Transfer Pak.
 *
 * Author: Alex Ishida
 * Version: 1.0
 * License: MIT
 */

#include <libdragon.h>

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <dir.h>
#include <stdarg.h>

#include "libtrpak.h"

/* ============================================================================
 * DEFINITIONS AND CONSTANTS
 * ============================================================================ */

#define APP_NAME           "GB Transfer Dumper"
#define APP_AUTHOR         "Alex Ishida"
#define APP_VERSION        "1.0"
#define ROM_DUMP_DIRECTORY "sd:/romdump"
#define SAVE_DUMP_DIRECTORY "sd:/savedump"
#define DUMP_DIRECTORY_FALLBACK "sd:/"
#define IO_BUFFER_SIZE     4096
#define MENU_ITEM_COUNT    5

/* Application error codes */
#define APP_ERR_SD_MOUNT           (-100)
#define APP_ERR_SD_DIRECTORY       (-101)
#define APP_ERR_FILE_OPEN          (-102)
#define APP_ERR_FILE_WRITE         (-103)
#define APP_ERR_FILE_CLOSE         (-104)
#define APP_ERR_NAME_EXHAUSTED     (-105)
#define APP_ERR_INVALID_PARAM      (-106)
#define APP_ERR_FILE_READ          (-107)
#define APP_ERR_FILE_NOT_FOUND     (-108)
#define APP_ERR_FILE_SIZE_MISMATCH (-109)
#define APP_ERR_CANCELLED          (-110)

/* Status configuration */
#define STATUS_CACHE_DURATION_MS   50

/* Debug levels */
#define DEBUG_LEVEL_NONE     0
#define DEBUG_LEVEL_ERROR    1
#define DEBUG_LEVEL_INFO     2
#define DEBUG_LEVEL_VERBOSE  3

/* ============================================================================
 * TYPES AND STRUCTURES
 * ============================================================================ */

typedef enum menu_item {
    MENU_INFO = 0,
    MENU_SAVE_ROM,
    MENU_SAVE_RAM,
    MENU_RESTORE_RAM,
    MENU_EXIT
} menu_item;

typedef struct {
    uint8_t last_status;
    uint64_t last_poll_time;
    bool is_cached;
    uint32_t cache_duration_ms;
} status_cache_t;

/* ============================================================================
 * GLOBAL VARIABLES
 * ============================================================================ */

static const char *const menu_labels[MENU_ITEM_COUNT] = {
    "Info",
    "Dump ROM",
    "Backup Save",
    "Restore Save",
    "Exit"
};

static bool sd_ready;
static bool cart_active;
static bool cart_known;
static const char *rom_dump_directory = DUMP_DIRECTORY_FALLBACK;
static const char *save_dump_directory = DUMP_DIRECTORY_FALLBACK;
#ifdef ENABLE_DEBUG
static int debug_level = DEBUG_LEVEL_INFO;
#endif
static status_cache_t status_cache = {
    .cache_duration_ms = STATUS_CACHE_DURATION_MS
};

/* ============================================================================
 * DEBUG FUNCTIONS
 * ============================================================================ */

static void debug_log(int level, const char *format, ...)
{
    #ifdef ENABLE_DEBUG
    if (level <= debug_level) {
        char message[256];
        va_list args;
        va_start(args, format);

        const char *level_str;
        switch (level) {
            case DEBUG_LEVEL_ERROR: level_str = "ERROR"; break;
            case DEBUG_LEVEL_INFO: level_str = "INFO"; break;
            case DEBUG_LEVEL_VERBOSE: level_str = "VERBOSE"; break;
            default: level_str = "UNKNOWN"; break;
        }

        vsnprintf(message, sizeof(message), format, args);
        va_end(args);
        debugf("[%s] %s", level_str, message);
    }
    #else
    (void)level;
    (void)format;
    #endif
}

/* ============================================================================
 * HELPER FUNCTIONS
 * ============================================================================ */

static const char *system_name(void)
{
    if (trcart.gbc == 0xC0u) {
        return "Game Boy Color";
    }
    return trcart.gbc == 0x80u ? "Game Boy / Color" : "Game Boy";
}

static const char *yes_no(bool value)
{
    return value ? "Detected" : "--";
}

static const char *cartridge_title(void)
{
    return trcart.title[0] != '\0' ? trcart.title : "Untitled cartridge";
}

static const char *mapper_name(uint8_t mapper)
{
    switch (mapper) {
    case TRPAK_MAPPER_NONE: return "ROM only";
    case TRPAK_MAPPER_MBC1: return "MBC1";
    case TRPAK_MAPPER_MBC2: return "MBC2";
    case TRPAK_MAPPER_MMM01: return "MMM01";
    case TRPAK_MAPPER_MBC3: return "MBC3";
    case TRPAK_MAPPER_MBC5: return "MBC5";
    case TRPAK_MAPPER_CAMERA: return "Game Boy Camera";
    case TRPAK_MAPPER_TAMA5: return "TAMA5";
    case TRPAK_MAPPER_HUC3: return "HuC3";
    case TRPAK_MAPPER_HUC1: return "HuC1";
    case TRPAK_MAPPER_MBC4: return "MBC4";
    default: return "Unknown";
    }
}

/* ============================================================================
 * ERROR MESSAGES
 * ============================================================================ */

static const char *app_error_string(int result)
{
    switch (result) {
        case APP_ERR_SD_MOUNT: return "Unable to mount microSD";
        case APP_ERR_SD_DIRECTORY: return "Unable to access dump directory";
        case APP_ERR_FILE_OPEN: return "Unable to create file";
        case APP_ERR_FILE_WRITE: return "Error writing to microSD";
        case APP_ERR_FILE_CLOSE: return "Error closing file";
        case APP_ERR_NAME_EXHAUSTED: return "No available filename";
        case APP_ERR_INVALID_PARAM: return "Invalid parameter";
        case APP_ERR_FILE_READ: return "Error reading from microSD";
        case APP_ERR_FILE_NOT_FOUND: return "File not found";
        case APP_ERR_FILE_SIZE_MISMATCH: return "File size mismatch";
        case APP_ERR_CANCELLED: return "Operation cancelled";
        default: return trpak_error_string(result);
    }
}

/* ============================================================================
 * INTERFACE FUNCTIONS
 * ============================================================================ */

static void draw_header(void)
{
    printf("+--------------------------------------+\n");
    printf("| %-21s PAK: %-9s |\n", APP_NAME,
           cart_active ? "CONNECTED" : "CHECK");
    printf("+--------------------------------------+\n");
}

static void draw_footer(const char *controls)
{
    printf("\n%s\n", controls);
    printf("%s | SC64 + Transfer Pak v%s\n", APP_AUTHOR, APP_VERSION);
}

static void wait_for_ack(void)
{
    for (;;) {
        joypad_poll();
        joypad_buttons_t pressed =
            joypad_get_buttons_pressed(JOYPAD_PORT_1);
        if (pressed.a || pressed.b || pressed.start) {
            return;
        }
        wait_ms(16);
    }
}

static void show_message(const char *title, const char *line1,
                         const char *line2)
{
    console_clear();
    draw_header();
    printf("\n%s\n", title);
    printf("------------------------------------------\n");
    if (line1 != NULL) {
        printf("%s\n", line1);
    }
    if (line2 != NULL) {
        printf("%s\n", line2);
    }
    draw_footer("A / B / START: continue");
    console_render();
    wait_for_ack();
}

static bool confirm_action(const char *title, const char *line1,
                           const char *line2)
{
    console_clear();
    draw_header();
    printf("\n%s\n", title);
    printf("------------------------------------------\n");
    printf("%s\n", line1);
    if (line2 != NULL) {
        printf("%s\n", line2);
    }
    draw_footer("A: confirm     B: cancel");
    console_render();

    for (;;) {
        joypad_buttons_t pressed;
        joypad_poll();
        pressed = joypad_get_buttons_pressed(JOYPAD_PORT_1);
        if (pressed.a || pressed.start) {
            return true;
        }
        if (pressed.b) {
            return false;
        }
        wait_ms(16);
    }
}

/* ============================================================================
 * HARDWARE FUNCTIONS
 * ============================================================================ */

static void shutdown_cartridge(void)
{
    if (cart_active) {
        (void)trpak_shutdown();
        cart_active = false;
        debug_log(DEBUG_LEVEL_INFO, "Cartridge powered down\n");
    }
}

static int get_cached_status(uint8_t *status, bool force_refresh)
{
    uint64_t now = (uint64_t)timer_ticks();

    if (!force_refresh && status_cache.is_cached &&
        (now - status_cache.last_poll_time) <
            (uint64_t)TICKS_FROM_MS(status_cache.cache_duration_ms)) {
        *status = status_cache.last_status;
        return TRPAK_OK;
    }

    int result = trpak_get_status(&status_cache.last_status);
    if (result == TRPAK_OK) {
        status_cache.is_cached = true;
        status_cache.last_poll_time = now;
        *status = status_cache.last_status;
        debug_log(DEBUG_LEVEL_VERBOSE, "Status cache updated: 0x%02X\n",
                 status_cache.last_status);
    }
    return result;
}

static int cartridge_ready(void)
{
    uint8_t status;
    int result = get_cached_status(&status, false);

    if (result != TRPAK_OK) {
        debug_log(DEBUG_LEVEL_VERBOSE, "Status query failed: %d\n", result);
        return result;
    }
    if ((status & TRPAK_STATUS_REMOVED) != 0u) {
        debug_log(DEBUG_LEVEL_ERROR, "Cartridge removed\n");
        return TRPAK_ERR_NO_CARTRIDGE;
    }
    if ((status & TRPAK_STATUS_POWERED) == 0u) {
        debug_log(DEBUG_LEVEL_VERBOSE, "Cartridge not powered\n");
        return TRPAK_ERR_POWER_OFF;
    }

    /* trpak_init() already waits for reset completion. Some hardware keeps
     * READY/RESETTING status bits asserted inconsistently during active
     * access, so block I/O is the authority after successful initialization. */
    return TRPAK_OK;
}

static int refresh_cartridge(void)
{
    shutdown_cartridge();
    status_cache.is_cached = false;

    int result = trpak_init();
    if (result == TRPAK_OK) {
        cart_active = true;
        cart_known = true;
        debug_log(DEBUG_LEVEL_INFO, "Cartridge initialized: %s\n",
                 cartridge_title());
    } else {
        cart_active = false;
        cart_known = false;
        debug_log(DEBUG_LEVEL_ERROR, "Cartridge init failed: %d\n", result);
    }
    return result;
}

/* ============================================================================
 * FILE FUNCTIONS
 * ============================================================================ */

static int ensure_sd(void)
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

static int ensure_dump_directory(bool save_ram)
{
    struct stat status;
    const char *directory = save_ram ? SAVE_DUMP_DIRECTORY : ROM_DUMP_DIRECTORY;
    const char **active_directory = save_ram
        ? &save_dump_directory : &rom_dump_directory;
    int result = ensure_sd();
    if (result != TRPAK_OK) {
        return result;
    }

    if (stat(directory, &status) == 0) {
        if (S_ISDIR(status.st_mode)) {
            *active_directory = directory;
            return TRPAK_OK;
        }
        debug_log(DEBUG_LEVEL_ERROR,
                 "Dump path exists but is not a directory: %s\n", directory);
        return APP_ERR_SD_DIRECTORY;
    }

    if (errno == ENOENT || errno == 0) {
        *active_directory = DUMP_DIRECTORY_FALLBACK;
        debug_log(DEBUG_LEVEL_INFO,
                 "%s does not exist; writing to %s\n",
                 directory, *active_directory);
        return TRPAK_OK;
    }

    debug_log(DEBUG_LEVEL_ERROR, "Unable to inspect directory %s: %s\n",
             directory, strerror(errno));
    return APP_ERR_SD_DIRECTORY;
}

static void safe_title(char output[32])
{
    memset(output, 0, 32);

    size_t write_index = 0u;
    size_t i;

    for (i = 0u; i < sizeof(trcart.title) && trcart.title[i] != '\0'; i++) {
        if (write_index >= 31u) break;

        unsigned char value = (unsigned char)trcart.title[i];
        char sanitized;

        if ((value >= 'A' && value <= 'Z') ||
            (value >= 'a' && value <= 'z') ||
            (value >= '0' && value <= '9') ||
            value == '-' || value == '_') {
            sanitized = (char)value;
        } else if (value == ' ') {
            sanitized = '_';
        } else {
            sanitized = '_';
        }

        if (write_index > 0u && sanitized == '_' &&
            output[write_index - 1u] == '_') {
            continue;
        }
        output[write_index++] = sanitized;
    }

    while (write_index > 0u && output[write_index - 1u] == '_') {
        write_index--;
    }

    if (write_index == 0u) {
        memcpy(output, "cartridge", sizeof("cartridge"));
    } else {
        output[write_index] = '\0';
    }

    output[31] = '\0';
    debug_log(DEBUG_LEVEL_VERBOSE, "Sanitized title: %s\n", output);
}

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

static int open_unique_file(const char *directory, const char *extension,
                            char path[128], FILE **file)
{
    char title[32];
    unsigned int suffix;

    safe_title(title);
    *file = NULL;

    for (suffix = 0u; suffix <= 99u; suffix++) {
        bool exists;
        int length;
        const char *separator = directory[strlen(directory) - 1u] == '/'
            ? "" : "/";
        if (suffix == 0u) {
            length = snprintf(path, 128, "%s%s%s.%s",
                              directory, separator, title, extension);
        } else {
            length = snprintf(path, 128, "%s%s%s-%02u.%s",
                              directory, separator, title, suffix, extension);
        }
        if (length < 0 || length >= 128) {
            debug_log(DEBUG_LEVEL_ERROR, "Path truncation: %d\n", length);
            return APP_ERR_FILE_OPEN;
        }

        int result = path_exists(path, &exists);
        if (result != TRPAK_OK) {
            return result;
        }
        if (exists) {
            debug_log(DEBUG_LEVEL_VERBOSE, "File exists: %s\n", path);
            continue;
        }

        *file = fopen(path, "wb");
        if (*file != NULL) {
            debug_log(DEBUG_LEVEL_INFO, "Created file: %s\n", path);
            return TRPAK_OK;
        }
        return APP_ERR_FILE_OPEN;
    }
    return APP_ERR_NAME_EXHAUSTED;
}

static bool select_restore_file(const char *directory, char *path,
                                size_t path_size)
{
    dir_t entry;
    int result = dir_findfirst(directory, &entry);

    while (result == 0) {
        size_t name_length = strlen(entry.d_name);
        bool is_save = name_length >= 4u &&
            strcmp(&entry.d_name[name_length - 4u], ".sav") == 0;
        if (entry.d_type == DT_REG && is_save) {
            size_t directory_length = strlen(directory);
            bool needs_separator = directory[directory_length - 1u] != '/';
            if (name_length + directory_length + (needs_separator ? 2u : 1u) > path_size) {
                result = dir_findnext(directory, &entry);
                continue;
            }
            memcpy(path, directory, directory_length);
            if (needs_separator) {
                path[directory_length++] = '/';
            }
            memcpy(&path[directory_length], entry.d_name,
                   name_length + 1u);
            debug_log(DEBUG_LEVEL_INFO, "Selected restore file: %s\n", path);
            return true;
        }
        result = dir_findnext(directory, &entry);
    }

    debug_log(DEBUG_LEVEL_INFO, "No .sav files found in %s\n", directory);
    return false;
}

static int validate_restore_file(const char *path)
{
    struct stat st;
    if (stat(path, &st) != 0) {
        debug_log(DEBUG_LEVEL_ERROR, "Restore file not found: %s\n", path);
        return APP_ERR_FILE_NOT_FOUND;
    }
    if (st.st_size != trcart.ramsize) {
        debug_log(DEBUG_LEVEL_ERROR, "File size mismatch: %zu vs %zu\n",
                 st.st_size, trcart.ramsize);
        return APP_ERR_FILE_SIZE_MISMATCH;
    }
    debug_log(DEBUG_LEVEL_INFO, "Restore file validated: %s (%zu bytes)\n",
             path, st.st_size);
    return TRPAK_OK;
}

static int safe_flush_buffer(FILE *file, const uint8_t *buffer, size_t size)
{
    if (!file || !buffer) {
        debug_log(DEBUG_LEVEL_ERROR, "Invalid parameters to flush_buffer\n");
        return APP_ERR_INVALID_PARAM;
    }
    if (size == 0u) {
        return TRPAK_OK;
    }

    size_t written = fwrite(buffer, 1u, size, file);
    if (written != size) {
        debug_log(DEBUG_LEVEL_ERROR, "fwrite failed: wrote %zu of %zu bytes\n",
                 written, size);
        return APP_ERR_FILE_WRITE;
    }
    return TRPAK_OK;
}

/* ============================================================================
 * PROGRESS AND VALIDATION FUNCTIONS
 * ============================================================================ */

static uint32_t update_crc32(uint32_t crc, const uint8_t *data, size_t size)
{
    for (size_t i = 0; i < size; i++) {
        crc ^= data[i];
        for (int j = 0; j < 8; j++) {
            if (crc & 1) {
                crc = (crc >> 1) ^ 0xEDB88320;
            } else {
                crc >>= 1;
            }
        }
    }
    return crc;
}

static void draw_progress(const char *operation, const char *path,
                          size_t done, size_t total, uint64_t elapsed_ticks)
{
    char bar[21];
    unsigned int filled;
    unsigned int index;
    unsigned long percent = total == 0u
        ? 0u
        : (unsigned long)((done * 100u) / total);

    filled = (unsigned int)((percent * 20u) / 100u);
    if (filled > 20u) {
        filled = 20u;
    }
    for (index = 0u; index < 20u; index++) {
        bar[index] = index < filled ? '#' : '-';
    }
    bar[20] = '\0';

    /* Calculate transfer speed and remaining time. */
    float rate_kb_s = 0.0f;
    uint32_t remaining_ms = 0;
    uint64_t elapsed_ms = (uint64_t)TICKS_TO_MS(elapsed_ticks);
    if (done > 0 && elapsed_ms > 0u) {
        float rate = (float)done / elapsed_ms;  /* bytes/ms */
        rate_kb_s = rate * 1000.0f / 1024.0f;   /* KiB/s */
        remaining_ms = (uint32_t)((total - done) / rate);
    }

    console_clear();
    draw_header();
    printf("\n%s\n\n", operation);
    printf("%s\n", cartridge_title());
    printf("[%s] %3lu%%\n\n", bar, percent);
    printf("%lu / %lu KiB\n",
           (unsigned long)(done / 1024u),
           (unsigned long)(total / 1024u));

    if (rate_kb_s > 0.0f) {
        printf("Speed: %.1f KiB/s\n", rate_kb_s);
        printf("Remaining: %02lu:%02lu\n",
               (unsigned long)(remaining_ms / 60000u),
               (unsigned long)((remaining_ms % 60000u) / 1000u));
    }

    printf("File: %s\n", path);
    printf("\nDo not remove the cartridge or Transfer Pak.\n");
    draw_footer("Please wait...");
    console_render();
}

/* ============================================================================
 * DUMP FUNCTIONS
 * ============================================================================ */

static int dump_rom_stream(FILE *file, const char *path, size_t *bytes_written)
{
    uint8_t *io_buffer = NULL;
    size_t buffer_size = IO_BUFFER_SIZE;
    size_t buffered = 0u;
    size_t total = 0u;
    uint16_t bank;
    int result = TRPAK_OK;
    uint64_t start_time = (uint64_t)timer_ticks();
    uint32_t checksum = 0xFFFFFFFFu;

    *bytes_written = 0u;

    /* Allocate transfer buffer. */
    io_buffer = malloc(buffer_size);
    if (!io_buffer) {
        debug_log(DEBUG_LEVEL_ERROR, "Failed to allocate I/O buffer\n");
        return APP_ERR_INVALID_PARAM;
    }

    if (trcart.mapper == TRPAK_MAPPER_MBC1 && trcart.rombanks > 128u) {
        debug_log(DEBUG_LEVEL_ERROR, "MBC1 with >128 banks not supported\n");
        free(io_buffer);
        return TRPAK_ERR_UNSUPPORTED_CARTRIDGE;
    }
    if (trcart.mapper == TRPAK_MAPPER_HUC1 && trcart.rombanks > 64u) {
        debug_log(DEBUG_LEVEL_ERROR, "HuC1 with >64 banks not supported\n");
        free(io_buffer);
        return TRPAK_ERR_UNSUPPORTED_CARTRIDGE;
    }

    draw_progress("Dumping ROM...", path, 0u, trcart.romsize, 0u);

    for (bank = 0u; bank < trcart.rombanks; bank++) {
        uint32_t address;

        result = cartridge_ready();
        if (result != TRPAK_OK) {
            debug_log(DEBUG_LEVEL_ERROR, "Cartridge not ready at bank %u\n", bank);
            break;
        }
        result = trpak_select_rom_bank(bank);
        if (result != TRPAK_OK) {
            debug_log(DEBUG_LEVEL_ERROR, "Failed to select bank %u: %d\n",
                     bank, result);
            break;
        }

        for (address = 0xC000u; address <= 0xFFE0u;
             address += TRPAK_TRANSFER_BLOCK_SIZE) {
            result = cartridge_ready();
            if (result != TRPAK_OK) {
                debug_log(DEBUG_LEVEL_ERROR, "Cartridge lost at bank %u, addr 0x%04X\n",
                         bank, address);
                break;
            }
            result = trpak_read_rom_block((uint16_t)address,
                                          &io_buffer[buffered]);
            if (result != TRPAK_OK) {
                debug_log(DEBUG_LEVEL_ERROR, "Read failed at bank %u, addr 0x%04X: %d\n",
                         bank, address, result);
                break;
            }
            buffered += TRPAK_TRANSFER_BLOCK_SIZE;
            total += TRPAK_TRANSFER_BLOCK_SIZE;

            if (buffered == buffer_size) {
                checksum = update_crc32(checksum, io_buffer, buffered);
                result = safe_flush_buffer(file, io_buffer, buffered);
                if (result != TRPAK_OK) {
                    debug_log(DEBUG_LEVEL_ERROR, "Flush failed: %d\n", result);
                    break;
                }
                buffered = 0u;
            }
        }
        if (result != TRPAK_OK) {
            break;
        }
        draw_progress("Dumping ROM...", path, total, trcart.romsize,
                     (uint64_t)timer_ticks() - start_time);
    }

    if (result == TRPAK_OK) {
        checksum = update_crc32(checksum, io_buffer, buffered);
        result = safe_flush_buffer(file, io_buffer, buffered);
        if (result == TRPAK_OK) {
            debug_log(DEBUG_LEVEL_INFO, "ROM CRC32: 0x%08X\n", ~checksum);
        }
    }

    *bytes_written = total;

    {
        int reset_result = trpak_select_rom_bank(0u);
        if (result == TRPAK_OK && reset_result != TRPAK_OK) {
            result = reset_result;
        }
    }

    free(io_buffer);
    return result;
}

static size_t ram_bytes_for_bank(uint16_t bank)
{
    size_t offset = (size_t)bank * TRPAK_RAM_BANK_SIZE;
    if (offset >= trcart.ramsize) {
        return 0u;
    }

    size_t remaining = trcart.ramsize - offset;
    return remaining < TRPAK_RAM_BANK_SIZE ? remaining : TRPAK_RAM_BANK_SIZE;
}

static void normalize_mbc2(uint8_t block[TRPAK_TRANSFER_BLOCK_SIZE])
{
    size_t i;
    for (i = 0u; i < TRPAK_TRANSFER_BLOCK_SIZE; i++) {
        block[i] &= 0x0Fu;
    }
}

static int dump_ram_stream(FILE *file, const char *path, size_t *bytes_written)
{
    uint8_t *io_buffer = NULL;
    size_t buffer_size = IO_BUFFER_SIZE;
    size_t buffered = 0u;
    size_t total = 0u;
    uint16_t bank;
    int result = TRPAK_OK;
    uint64_t start_time = (uint64_t)timer_ticks();
    uint32_t checksum = 0xFFFFFFFFu;

    *bytes_written = 0u;
    if (!trcart.ram || trcart.ramsize == 0u) {
        debug_log(DEBUG_LEVEL_ERROR, "No RAM present\n");
        return TRPAK_ERR_NO_RAM;
    }

    io_buffer = malloc(buffer_size);
    if (!io_buffer) {
        debug_log(DEBUG_LEVEL_ERROR, "Failed to allocate I/O buffer\n");
        return APP_ERR_INVALID_PARAM;
    }

    draw_progress("Backing up Save...", path, 0u, trcart.ramsize, 0u);

    for (bank = 0u; bank < trcart.rambanks; bank++) {
        size_t bank_size = ram_bytes_for_bank(bank);
        size_t bank_offset;

        result = cartridge_ready();
        if (result != TRPAK_OK) {
            debug_log(DEBUG_LEVEL_ERROR, "Cartridge not ready at RAM bank %u\n", bank);
            break;
        }
        result = trpak_select_ram_bank(bank);
        if (result != TRPAK_OK) {
            debug_log(DEBUG_LEVEL_ERROR, "Failed to select RAM bank %u: %d\n",
                     bank, result);
            break;
        }

        for (bank_offset = 0u; bank_offset < bank_size;
             bank_offset += TRPAK_TRANSFER_BLOCK_SIZE) {
            uint8_t *block = &io_buffer[buffered];
            uint16_t address = (uint16_t)(0xE000u + bank_offset);

            result = cartridge_ready();
            if (result != TRPAK_OK) {
                debug_log(DEBUG_LEVEL_ERROR, "Cartridge lost at RAM bank %u, offset 0x%04X\n",
                         bank, bank_offset);
                break;
            }
            result = trpak_read_ram_block(address, block);
            if (result != TRPAK_OK) {
                debug_log(DEBUG_LEVEL_ERROR, "Read failed at RAM bank %u: %d\n",
                         bank, result);
                break;
            }
            if (trcart.mapper == TRPAK_MAPPER_MBC2) {
                normalize_mbc2(block);
            }
            buffered += TRPAK_TRANSFER_BLOCK_SIZE;
            total += TRPAK_TRANSFER_BLOCK_SIZE;

            if (buffered == buffer_size) {
                checksum = update_crc32(checksum, io_buffer, buffered);
                result = safe_flush_buffer(file, io_buffer, buffered);
                if (result != TRPAK_OK) {
                    debug_log(DEBUG_LEVEL_ERROR, "Flush failed: %d\n", result);
                    break;
                }
                buffered = 0u;
            }
        }
        if (result != TRPAK_OK) {
            break;
        }
        draw_progress("Backing up Save...", path, total, trcart.ramsize,
                     (uint64_t)timer_ticks() - start_time);
    }

    if (result == TRPAK_OK) {
        checksum = update_crc32(checksum, io_buffer, buffered);
        result = safe_flush_buffer(file, io_buffer, buffered);
        if (result == TRPAK_OK) {
            debug_log(DEBUG_LEVEL_INFO, "RAM CRC32: 0x%08X\n", ~checksum);
        }
    }
    *bytes_written = total;

    {
        int cleanup_result = trpak_disable_ram();
        if (result == TRPAK_OK && cleanup_result != TRPAK_OK) {
            result = cleanup_result;
        }
    }

    free(io_buffer);
    return result;
}

/* ============================================================================
 * RESTORE FUNCTIONS
 * ============================================================================ */

static int restore_ram_stream(FILE *file, const char *path, size_t *bytes_read)
{
    uint8_t *io_buffer = NULL;
    uint8_t verification[TRPAK_TRANSFER_BLOCK_SIZE];
    size_t buffer_size = IO_BUFFER_SIZE;
    size_t buffered = 0u;
    size_t buffer_offset = 0u;
    size_t total = 0u;
    uint16_t bank;
    int result = TRPAK_OK;
    uint64_t start_time = (uint64_t)timer_ticks();

    *bytes_read = 0u;
    if (!trcart.ram || trcart.ramsize == 0u) {
        debug_log(DEBUG_LEVEL_ERROR, "No RAM present\n");
        return TRPAK_ERR_NO_RAM;
    }

    io_buffer = malloc(buffer_size);
    if (!io_buffer) {
        debug_log(DEBUG_LEVEL_ERROR, "Failed to allocate I/O buffer\n");
        return APP_ERR_INVALID_PARAM;
    }

    draw_progress("Restoring Save...", path, 0u, trcart.ramsize, 0u);

    for (bank = 0u; bank < trcart.rambanks; bank++) {
        size_t bank_size = ram_bytes_for_bank(bank);
        size_t bank_offset;

        result = cartridge_ready();
        if (result != TRPAK_OK) {
            debug_log(DEBUG_LEVEL_ERROR, "Cartridge not ready at RAM bank %u\n", bank);
            break;
        }
        result = trpak_select_ram_bank(bank);
        if (result != TRPAK_OK) {
            debug_log(DEBUG_LEVEL_ERROR, "Failed to select RAM bank %u: %d\n",
                     bank, result);
            break;
        }

        for (bank_offset = 0u; bank_offset < bank_size;
             bank_offset += TRPAK_TRANSFER_BLOCK_SIZE) {

            /* Refill only after every byte in the previous chunk was used. */
            if (buffer_offset == buffered) {
                size_t to_read = buffer_size;
                if (to_read > bank_size - bank_offset) {
                    to_read = bank_size - bank_offset;
                }
                size_t read = fread(io_buffer, 1u, to_read, file);
                if (read != to_read) {
                    debug_log(DEBUG_LEVEL_ERROR, "File read failed: read %zu of %zu bytes\n",
                             read, to_read);
                    result = APP_ERR_FILE_READ;
                    break;
                }
                buffered = to_read;
                buffer_offset = 0u;
            }

            uint16_t address = (uint16_t)(0xE000u + bank_offset);
            result = cartridge_ready();
            if (result != TRPAK_OK) {
                debug_log(DEBUG_LEVEL_ERROR,
                         "Cartridge lost at RAM bank %u, offset 0x%04X\n",
                         bank, bank_offset);
                break;
            }
            result = trpak_write_ram_block(address, &io_buffer[buffer_offset]);
            if (result != TRPAK_OK) {
                debug_log(DEBUG_LEVEL_ERROR, "Write failed at RAM bank %u: %d\n",
                         bank, result);
                break;
            }

            result = trpak_read_ram_block(address, verification);
            if (result != TRPAK_OK) {
                debug_log(DEBUG_LEVEL_ERROR,
                         "Verification read failed at RAM bank %u: %d\n",
                         bank, result);
                break;
            }
            if (trcart.mapper == TRPAK_MAPPER_MBC2) {
                normalize_mbc2(verification);
            }
            if (memcmp(&io_buffer[buffer_offset], verification,
                       TRPAK_TRANSFER_BLOCK_SIZE) != 0) {
                debug_log(DEBUG_LEVEL_ERROR,
                         "Verification failed at RAM bank %u, offset 0x%04X\n",
                         bank, bank_offset);
                result = TRPAK_ERR_VERIFY_FAILED;
                break;
            }

            buffer_offset += TRPAK_TRANSFER_BLOCK_SIZE;
            total += TRPAK_TRANSFER_BLOCK_SIZE;

            if (total % buffer_size == 0u || total == trcart.ramsize) {
                draw_progress("Restoring Save...", path, total, trcart.ramsize,
                              (uint64_t)timer_ticks() - start_time);
            }
        }
        if (result != TRPAK_OK) {
            break;
        }
    }

    {
        int cleanup_result = trpak_disable_ram();
        if (result == TRPAK_OK && cleanup_result != TRPAK_OK) {
            result = cleanup_result;
        }
    }
    if (result == TRPAK_OK) {
        debug_log(DEBUG_LEVEL_INFO, "RAM restore completed: %zu bytes\n", total);
    }

    *bytes_read = total;
    free(io_buffer);
    return result;
}

/* ============================================================================
 * OPERATION FUNCTIONS
 * ============================================================================ */

static void perform_dump(bool save_ram)
{
    FILE *file = NULL;
    char path[128];
    char detail[96];
    size_t bytes_written = 0u;
    bool partial_created = false;
    bool partial_removed = false;
    int result;

    console_clear();
    draw_header();
    printf("\nPreparing Transfer Pak...\n");
    draw_footer("Please wait...");
    console_render();

    result = refresh_cartridge();
    if (result != TRPAK_OK) {
        snprintf(detail, sizeof(detail), "%s (%d)",
                 app_error_string(result), result);
        show_message("Failed", detail,
                     "Check controller, Transfer Pak and cartridge.");
        return;
    }

    if (save_ram && (!trcart.ram || trcart.ramsize == 0u)) {
        show_message("BACKUP SAVE", "This cartridge has no save RAM.", NULL);
        shutdown_cartridge();
        return;
    }

    if (!confirm_action(save_ram ? "BACKUP SAVE" : "DUMP ROM",
                        cartridge_title(),
                        "A new file will be created on the microSD.")) {
        shutdown_cartridge();
        return;
    }

    result = ensure_dump_directory(save_ram);
    if (result == TRPAK_OK) {
        const char *extension = save_ram
            ? "sav"
            : (trcart.gbc != 0u ? "gbc" : "gb");
        result = open_unique_file(save_ram ? save_dump_directory : rom_dump_directory,
                                  extension, path, &file);
    }
    if (result == TRPAK_OK) {
        result = save_ram
            ? dump_ram_stream(file, path, &bytes_written)
            : dump_rom_stream(file, path, &bytes_written);
    }

    if (file != NULL) {
        partial_created = true;
        if (fclose(file) != 0 && result == TRPAK_OK) {
            debug_log(DEBUG_LEVEL_ERROR, "Failed to close file: %s\n", path);
            result = APP_ERR_FILE_CLOSE;
        }
        if (result != TRPAK_OK) {
            partial_removed = remove(path) == 0;
            if (partial_removed) {
                debug_log(DEBUG_LEVEL_INFO, "Partial file removed: %s\n", path);
            } else {
                debug_log(DEBUG_LEVEL_ERROR, "Failed to remove partial file: %s\n", path);
            }
        }
    }
    shutdown_cartridge();

    if (result == TRPAK_OK) {
        snprintf(detail, sizeof(detail), "%lu bytes written.",
                 (unsigned long)bytes_written);
        show_message(save_ram ? "SAVE BACKUP COMPLETE" : "ROM DUMP COMPLETE",
                     path, detail);
    } else {
        snprintf(detail, sizeof(detail), "%s (%d)",
                 app_error_string(result), result);
        show_message(!partial_created
                         ? "Failed"
                         : (partial_removed
                                ? "Failed; incomplete file removed"
                                : "Failed; remove incomplete file"),
                     detail,
                     partial_created && !partial_removed ? path : NULL);
    }
}

static void perform_restore(void)
{
    FILE *file = NULL;
    char path[128];
    char detail[96];
    size_t bytes_read = 0u;
    int result;

    console_clear();
    draw_header();
    printf("\nPreparing to restore save...\n");
    draw_footer("Please wait...");
    console_render();

    result = refresh_cartridge();
    if (result != TRPAK_OK) {
        snprintf(detail, sizeof(detail), "%s (%d)",
                 app_error_string(result), result);
        show_message("Failed", detail,
                     "Check controller, Transfer Pak and cartridge.");
        return;
    }

    if (!trcart.ram || trcart.ramsize == 0u) {
        show_message("RESTORE SAVE",
                     "This cartridge has no save RAM.", NULL);
        shutdown_cartridge();
        return;
    }

    result = ensure_dump_directory(true);
    if (result != TRPAK_OK) {
        show_message("Failed", app_error_string(result),
                     "Check microSD card.");
        shutdown_cartridge();
        return;
    }

    if (!select_restore_file(save_dump_directory, path, sizeof(path))) {
        show_message("RESTORE SAVE",
                     "No .sav file found.",
                     "Dump a save first.");
        shutdown_cartridge();
        return;
    }

    result = validate_restore_file(path);
    if (result != TRPAK_OK) {
        snprintf(detail, sizeof(detail), "%s (%d)",
                 app_error_string(result), result);
        show_message("Failed", detail,
                     "Invalid or incompatible file.");
        shutdown_cartridge();
        return;
    }

    if (!confirm_action("RESTORE SAVE",
                        "WARNING: This will overwrite the current save!",
                        path)) {
        shutdown_cartridge();
        return;
    }

    file = fopen(path, "rb");
    if (!file) {
        show_message("Failed", "Unable to open file.",
                     "Check microSD card.");
        shutdown_cartridge();
        return;
    }

    result = restore_ram_stream(file, path, &bytes_read);
    fclose(file);
    shutdown_cartridge();

    if (result == TRPAK_OK) {
        snprintf(detail, sizeof(detail), "%lu bytes restored.",
                 (unsigned long)bytes_read);
        show_message("SAVE RESTORED SUCCESSFULLY", path, detail);
    } else {
        snprintf(detail, sizeof(detail), "%s (%d)",
                 app_error_string(result), result);
        show_message("Restore failed", detail, NULL);
    }
}

/* ============================================================================
 * INFO MENU FUNCTIONS
 * ============================================================================ */

static void draw_info(int result)
{
    console_clear();
    draw_header();
    printf("\nCARTRIDGE INFORMATION\n");
    printf("------------------------------------------\n");
    if (result != TRPAK_OK) {
        printf("Cartridge : Not ready\n");
        printf("Status    : %s\n", app_error_string(result));
        printf("\nCheck controller port 1, Transfer Pak,\n");
        printf("and the Game Boy cartridge.\n");
        draw_footer("START: refresh       B: back");
    } else {
        printf("Title      %s\n", cartridge_title());
        printf("System     %s\n", system_name());
        printf("Type       0x%02X  %s\n", trcart.cartridge_type,
               mapper_name(trcart.mapper));
        printf("ROM Size   %lu KiB  / %u banks\n",
               (unsigned long)(trcart.romsize / 1024u), trcart.rombanks);
        printf("RAM Size   %lu KiB  / %u banks\n",
               (unsigned long)(trcart.ramsize / 1024u), trcart.rambanks);
        printf("Features   Save:%s Battery:%s\n",
               yes_no(trcart.ram), yes_no(trcart.battery));
        printf("           RTC:%s Rumble:%s\n",
               yes_no(trcart.rtc), yes_no(trcart.rumble));
        printf("\nStatus     READY     SD: %s\n",
               sd_ready ? "READY" : "NOT MOUNTED");
        printf("ROM path   %s\n", rom_dump_directory);
        printf("Save path  %s\n", save_dump_directory);
        draw_footer("A: Dump ROM START: refresh B: back");
    }
    console_render();
}

static bool show_info(void)
{
    int result = refresh_cartridge();

    for (;;) {
        joypad_buttons_t pressed;
        draw_info(result);
        for (;;) {
            joypad_poll();
            pressed = joypad_get_buttons_pressed(JOYPAD_PORT_1);
            if (pressed.b) {
                return false;
            }
            if (pressed.start) {
                result = refresh_cartridge();
                break;
            }
            if (pressed.a && result == TRPAK_OK) {
                return true;
            }
            wait_ms(16);
        }
    }
}

/* ============================================================================
 * MAIN MENU FUNCTIONS
 * ============================================================================ */

static bool menu_item_selectable(int item)
{
    /* All options are selectable now. */
    return true;
}

static int move_selection(int current, int direction)
{
    do {
        current = (current + direction + MENU_ITEM_COUNT) % MENU_ITEM_COUNT;
    } while (!menu_item_selectable(current));
    return current;
}

static void draw_menu(int selected)
{
    int i;

    console_clear();
    draw_header();
    printf("\nCARTRIDGE\n");
    printf("------------------------------------------\n");

    if (cart_known) {
        printf("%s\n", cartridge_title());
        printf("%s  |  %lu KiB ROM\n", system_name(),
               (unsigned long)(trcart.romsize / 1024u));
    } else {
        printf("No cartridge detected\n");
        printf("Use Info to check the Transfer Pak.\n");
    }
    printf("microSD: %s\n\n", sd_ready ? "READY" : "NOT MOUNTED");

    printf("ACTIONS\n");
    printf("------------------------------------------\n");

    for (i = 0; i < MENU_ITEM_COUNT; i++) {
        const char *cursor = i == selected ? ">" : " ";
        printf("%s %-22s\n", cursor, menu_labels[i]);
    }

    draw_footer("D-Pad: move A: select START: refresh");
    console_render();
}

/* ============================================================================
 * MAIN FUNCTION
 * ============================================================================ */

int main(void)
{
    int selected = MENU_INFO;
    bool running = true;

    /* Initialization */
    console_init();
    console_set_render_mode(RENDER_MANUAL);
    joypad_init();
    timer_init();
    debug_init(DEBUG_FEATURE_LOG_USB | DEBUG_FEATURE_LOG_EMU);

    debug_log(DEBUG_LEVEL_INFO, "%s v%s by %s\n", APP_NAME, APP_VERSION, APP_AUTHOR);

    /* Initialize hardware. */
    (void)ensure_dump_directory(false);
    (void)ensure_dump_directory(true);
    (void)refresh_cartridge();
    draw_menu(selected);

    /* Main loop. */
    while (running) {
        joypad_poll();
        joypad_buttons_t pressed =
            joypad_get_buttons_pressed(JOYPAD_PORT_1);

        if (pressed.d_up) {
            selected = move_selection(selected, -1);
            draw_menu(selected);
        } else if (pressed.d_down) {
            selected = move_selection(selected, 1);
            draw_menu(selected);
        } else if (pressed.start) {
            (void)refresh_cartridge();
            (void)ensure_dump_directory(false);
            (void)ensure_dump_directory(true);
            draw_menu(selected);
        } else if (pressed.a) {
            switch ((menu_item)selected) {
            case MENU_INFO:
                if (show_info()) {
                    perform_dump(false);
                }
                break;
            case MENU_SAVE_ROM:
                perform_dump(false);
                break;
            case MENU_SAVE_RAM:
                perform_dump(true);
                break;
            case MENU_RESTORE_RAM:
                perform_restore();
                break;
            case MENU_EXIT:
                if (confirm_action("EXIT", "Do you want to exit?",
                                   "The Transfer Pak will be powered down.")) {
                    running = false;
                }
                break;
            }
            if (running) {
                draw_menu(selected);
            }
        }
        wait_ms(16);
    }

    /* Shutdown */
    shutdown_cartridge();
    if (sd_ready) {
        debug_close_sdfs();
    }

    console_clear();
    draw_header();
    printf("\nTRANSFER PAK POWERED DOWN\n\n");
    printf("You can restart the Nintendo 64.\n");
    draw_footer("Thank you for preserving your games.");
    console_render();

    debug_log(DEBUG_LEVEL_INFO, "Application exiting\n");

    joypad_close();
    timer_close();
    return 0;
}
