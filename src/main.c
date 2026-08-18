/*
 * GB Transfer Dumper - Nintendo 64 Homebrew
 *
 * This application dumps and restores Game Boy cartridges through the
 * Nintendo 64 Transfer Pak.
 *
 * This file owns the menu, the cartridge information screen and the operation
 * flows that tie the cartridge, storage, transfer and UI modules together.
 *
 * Author: Alex Ishida
 * Version: 1.0.1
 * License: MIT
 */

#include <libdragon.h>

#include <stdbool.h>
#include <stdio.h>

#include "app.h"
#include "cart.h"
#include "storage.h"
#include "transfer.h"
#include "ui.h"

/* ============================================================================
 * MENU DEFINITION
 * ============================================================================ */

typedef enum menu_item {
    MENU_INFO = 0,
    MENU_DUMP_ROM,
    MENU_BACKUP_SAVE,
    MENU_RESTORE_SAVE,
    MENU_EXIT,
    MENU_ITEM_COUNT
} menu_item;

static const char *const menu_labels[MENU_ITEM_COUNT] = {
    "Info",
    "Dump ROM",
    "Backup Save",
    "Restore Save",
    "Exit"
};

/* Controller poll interval for the main menu loop, in milliseconds. */
#define MENU_POLL_INTERVAL_MS 16

/* Longest message assembled with snprintf() for the result screens. */
#define DETAIL_SIZE 96

/* ============================================================================
 * OPERATIONS
 * ============================================================================ */

/** @brief Shows the "preparing" screen and powers the cartridge up. */
static int begin_operation(const char *message)
{
    console_clear();
    ui_draw_header();
    printf("\n%s\n", message);
    ui_draw_footer("Please wait...");
    console_render();

    return cart_refresh();
}

/** @brief Reports a failure as "<message> (<code>)" on a message screen. */
static void show_failure(const char *title, int result, const char *hint)
{
    char detail[DETAIL_SIZE];

    snprintf(detail, sizeof(detail), "%s (%d)", app_error_string(result), result);
    ui_show_message(title, detail, hint);
}

/**
 * @brief Dumps the cartridge ROM or its save RAM to a new file.
 *
 * @param save_ram true to back up save RAM, false to dump the ROM.
 */
static void perform_dump(bool save_ram)
{
    storage_kind kind = save_ram ? STORAGE_KIND_SAVE : STORAGE_KIND_ROM;
    char path[APP_PATH_SIZE];
    char detail[DETAIL_SIZE];
    FILE *file = NULL;
    size_t bytes_written = 0u;
    bool file_created = false;
    bool file_removed = false;
    int result;

    result = begin_operation("Preparing Transfer Pak...");
    if (result != TRPAK_OK) {
        show_failure("Failed", result,
                     "Check controller, Transfer Pak and cartridge.");
        return;
    }

    if (save_ram && !cart_has_ram()) {
        ui_show_message("BACKUP SAVE", "This cartridge has no save RAM.", NULL);
        cart_shutdown();
        return;
    }

    if (!ui_confirm(save_ram ? "BACKUP SAVE" : "DUMP ROM", cart_title(),
                    "A new file will be created on the microSD.")) {
        cart_shutdown();
        return;
    }

    result = storage_ensure_directory(kind);
    if (result == TRPAK_OK) {
        const char *extension = save_ram ? "sav" : cart_rom_extension();
        result = storage_open_unique_file(storage_directory(kind), extension,
                                          path, &file);
    }
    if (result == TRPAK_OK) {
        result = save_ram
            ? transfer_dump_ram(file, path, &bytes_written)
            : transfer_dump_rom(file, path, &bytes_written);
    }

    if (file != NULL) {
        file_created = true;
        if (fclose(file) != 0 && result == TRPAK_OK) {
            debug_log(DEBUG_LEVEL_ERROR, "Failed to close file: %s\n", path);
            result = APP_ERR_FILE_CLOSE;
        }
        if (result != TRPAK_OK) {
            file_removed = remove(path) == 0;
            debug_log(file_removed ? DEBUG_LEVEL_INFO : DEBUG_LEVEL_ERROR,
                      file_removed ? "Partial file removed: %s\n"
                                   : "Failed to remove partial file: %s\n",
                      path);
        }
    }
    cart_shutdown();

    if (result == TRPAK_OK) {
        snprintf(detail, sizeof(detail), "%lu bytes written.",
                 (unsigned long)bytes_written);
        ui_show_message(save_ram ? "SAVE BACKUP COMPLETE" : "ROM DUMP COMPLETE",
                        path, detail);
        return;
    }

    snprintf(detail, sizeof(detail), "%s (%d)", app_error_string(result), result);
    ui_show_message(!file_created
                        ? "Failed"
                        : (file_removed ? "Failed; incomplete file removed"
                                        : "Failed; remove incomplete file"),
                    detail,
                    file_created && !file_removed ? path : NULL);
}

/** @brief Writes a save file from the microSD card back into cartridge RAM. */
static void perform_restore(void)
{
    char path[APP_PATH_SIZE];
    char detail[DETAIL_SIZE];
    FILE *file;
    size_t bytes_read = 0u;
    int result;

    result = begin_operation("Preparing to restore save...");
    if (result != TRPAK_OK) {
        show_failure("Failed", result,
                     "Check controller, Transfer Pak and cartridge.");
        return;
    }

    if (!cart_has_ram()) {
        ui_show_message("RESTORE SAVE", "This cartridge has no save RAM.", NULL);
        cart_shutdown();
        return;
    }

    result = storage_ensure_directory(STORAGE_KIND_SAVE);
    if (result != TRPAK_OK) {
        ui_show_message("Failed", app_error_string(result), "Check microSD card.");
        cart_shutdown();
        return;
    }

    if (!storage_find_save_file(storage_directory(STORAGE_KIND_SAVE),
                                path, sizeof(path))) {
        ui_show_message("RESTORE SAVE", "No .sav file found.",
                        "Dump a save first.");
        cart_shutdown();
        return;
    }

    result = storage_validate_save_file(path);
    if (result != TRPAK_OK) {
        show_failure("Failed", result, "Invalid or incompatible file.");
        cart_shutdown();
        return;
    }

    if (!ui_confirm("RESTORE SAVE",
                    "WARNING: This will overwrite the current save!", path)) {
        cart_shutdown();
        return;
    }

    file = fopen(path, "rb");
    if (file == NULL) {
        ui_show_message("Failed", "Unable to open file.", "Check microSD card.");
        cart_shutdown();
        return;
    }

    result = transfer_restore_ram(file, path, &bytes_read);
    fclose(file);
    cart_shutdown();

    if (result == TRPAK_OK) {
        snprintf(detail, sizeof(detail), "%lu bytes restored.",
                 (unsigned long)bytes_read);
        ui_show_message("SAVE RESTORED SUCCESSFULLY", path, detail);
    } else {
        show_failure("Restore failed", result, NULL);
    }
}

/* ============================================================================
 * INFORMATION SCREEN
 * ============================================================================ */

/** @brief Draws the cartridge card, or the reason it could not be read. */
static void draw_info(int result)
{
    console_clear();
    ui_draw_header();
    printf("\nCARTRIDGE INFORMATION\n");
    printf("------------------------------------------\n");

    if (result != TRPAK_OK) {
        printf("Cartridge : Not ready\n");
        printf("Status    : %s\n", app_error_string(result));
        printf("\nCheck controller port 1, Transfer Pak,\n");
        printf("and the Game Boy cartridge.\n");
        ui_draw_footer("START: refresh       B: back");
        console_render();
        return;
    }

    printf("Title      %s\n", cart_title());
    printf("System     %s\n", cart_system_name());
    printf("Type       0x%02X  %s\n", (unsigned int)trcart.cartridge_type,
           cart_mapper_name(trcart.mapper));
    printf("ROM Size   %lu KiB  / %u banks\n",
           (unsigned long)(trcart.romsize / 1024u), (unsigned int)trcart.rombanks);
    printf("RAM Size   %lu KiB  / %u banks\n",
           (unsigned long)(trcart.ramsize / 1024u), (unsigned int)trcart.rambanks);
    printf("Features   Save:%s Battery:%s\n",
           ui_yes_no(trcart.ram != 0u), ui_yes_no(trcart.battery != 0u));
    printf("           RTC:%s Rumble:%s\n",
           ui_yes_no(trcart.rtc != 0u), ui_yes_no(trcart.rumble != 0u));
    printf("\nStatus     READY     SD: %s\n",
           storage_is_ready() ? "READY" : "NOT MOUNTED");
    printf("ROM path   %s\n", storage_directory(STORAGE_KIND_ROM));
    printf("Save path  %s\n", storage_directory(STORAGE_KIND_SAVE));
    ui_draw_footer("A: Dump ROM START: refresh B: back");
    console_render();
}

/**
 * @brief Runs the information screen until the user leaves it.
 *
 * @return true when the user asked to start a ROM dump from this screen.
 */
static bool show_info(void)
{
    int result = cart_refresh();

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
                result = cart_refresh();
                break;
            }
            if (pressed.a && result == TRPAK_OK) {
                return true;
            }
            wait_ms(MENU_POLL_INTERVAL_MS);
        }
    }
}

/* ============================================================================
 * MAIN MENU
 * ============================================================================ */

static void draw_menu(int selected)
{
    int i;

    console_clear();
    ui_draw_header();
    printf("\nCARTRIDGE\n");
    printf("------------------------------------------\n");

    if (cart_is_known()) {
        printf("%s\n", cart_title());
        printf("%s  |  %lu KiB ROM\n", cart_system_name(),
               (unsigned long)(trcart.romsize / 1024u));
    } else {
        printf("No cartridge detected\n");
        printf("Use Info to check the Transfer Pak.\n");
    }
    printf("microSD: %s\n\n", storage_is_ready() ? "READY" : "NOT MOUNTED");

    printf("ACTIONS\n");
    printf("------------------------------------------\n");
    for (i = 0; i < MENU_ITEM_COUNT; i++) {
        printf("%s %-22s\n", i == selected ? ">" : " ", menu_labels[i]);
    }

    ui_draw_footer("D-Pad: move A: select START: refresh");
    console_render();
}

/** @brief Re-reads the cartridge and both dump directories. */
static void refresh_all(void)
{
    (void)cart_refresh();
    (void)storage_ensure_directory(STORAGE_KIND_ROM);
    (void)storage_ensure_directory(STORAGE_KIND_SAVE);
}

/**
 * @brief Runs the selected menu entry.
 *
 * @param selected Entry to run.
 * @return false when the application should exit.
 */
static bool run_menu_item(menu_item selected)
{
    switch (selected) {
    case MENU_INFO:
        if (show_info()) {
            perform_dump(false);
        }
        break;
    case MENU_DUMP_ROM:
        perform_dump(false);
        break;
    case MENU_BACKUP_SAVE:
        perform_dump(true);
        break;
    case MENU_RESTORE_SAVE:
        perform_restore();
        break;
    case MENU_EXIT:
        return !ui_confirm("EXIT", "Do you want to exit?",
                           "The Transfer Pak will be powered down.");
    case MENU_ITEM_COUNT:
    default:
        break;
    }
    return true;
}

/* ============================================================================
 * ENTRY POINT
 * ============================================================================ */

int main(void)
{
    int selected = MENU_INFO;
    bool running = true;
    int result;

    console_init();
    console_set_render_mode(RENDER_MANUAL);
    joypad_init();
    timer_init();
    debug_init(DEBUG_FEATURE_LOG_USB | DEBUG_FEATURE_LOG_EMU);

    debug_log(DEBUG_LEVEL_INFO, "%s v%s by %s\n",
              APP_NAME, APP_VERSION, APP_AUTHOR);

    /* The streaming backend carries the Joybus primitives libtrpak talks to,
     * so it has to be installed before the first cartridge is powered up. */
    result = transfer_init();
    if (result != TRPAK_OK) {
        show_failure("Failed", result, "The program cannot access the pak.");
        joypad_close();
        timer_close();
        return 1;
    }

    refresh_all();
    draw_menu(selected);

    while (running) {
        joypad_buttons_t pressed;

        joypad_poll();
        pressed = joypad_get_buttons_pressed(JOYPAD_PORT_1);

        if (pressed.d_up) {
            selected = (selected + MENU_ITEM_COUNT - 1) % MENU_ITEM_COUNT;
            draw_menu(selected);
        } else if (pressed.d_down) {
            selected = (selected + 1) % MENU_ITEM_COUNT;
            draw_menu(selected);
        } else if (pressed.start) {
            refresh_all();
            draw_menu(selected);
        } else if (pressed.a) {
            running = run_menu_item((menu_item)selected);
            if (running) {
                draw_menu(selected);
            }
        }
        wait_ms(MENU_POLL_INTERVAL_MS);
    }

    cart_shutdown();
    if (storage_is_ready()) {
        debug_close_sdfs();
    }

    console_clear();
    ui_draw_header();
    printf("\nTRANSFER PAK POWERED DOWN\n\n");
    printf("You can restart the Nintendo 64.\n");
    ui_draw_footer("Thank you for preserving your games.");
    console_render();

    debug_log(DEBUG_LEVEL_INFO, "Application exiting\n");

    joypad_close();
    timer_close();
    return 0;
}
