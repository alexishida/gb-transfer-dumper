#include <libdragon.h>

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include "libtrpak.h"

#define APP_NAME "GB Transfer Dumper"
#define DUMP_DIRECTORY "sd:/gbdump"
#define IO_BUFFER_SIZE 512u
#define MENU_ITEM_COUNT 5

#define APP_ERR_SD_MOUNT       (-100)
#define APP_ERR_SD_DIRECTORY   (-101)
#define APP_ERR_FILE_OPEN      (-102)
#define APP_ERR_FILE_WRITE     (-103)
#define APP_ERR_FILE_CLOSE     (-104)
#define APP_ERR_NAME_EXHAUSTED (-105)

typedef enum menu_item {
    MENU_INFO = 0,
    MENU_SAVE_ROM,
    MENU_SAVE_RAM,
    MENU_RESTORE_RAM,
    MENU_EXIT
} menu_item;

static const char *const menu_labels[MENU_ITEM_COUNT] = {
    "Info",
    "Save ROM",
    "Save RAM",
    "Restore RAM (off)",
    "Exit"
};

static bool sd_ready;
static bool cart_active;
static bool cart_known;

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

static const char *app_error_string(int result)
{
    switch (result) {
    case APP_ERR_SD_MOUNT: return "nao foi possivel montar o microSD";
    case APP_ERR_SD_DIRECTORY: return "nao foi possivel criar /gbdump";
    case APP_ERR_FILE_OPEN: return "nao foi possivel criar o arquivo";
    case APP_ERR_FILE_WRITE: return "erro ao gravar no microSD";
    case APP_ERR_FILE_CLOSE: return "erro ao finalizar o arquivo";
    case APP_ERR_NAME_EXHAUSTED: return "nao ha nome de arquivo disponivel";
    default: return trpak_error_string(result);
    }
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
    printf("%s\n\n", APP_NAME);
    printf("%s\n\n", title);
    if (line1 != NULL) {
        printf("%s\n", line1);
    }
    if (line2 != NULL) {
        printf("%s\n", line2);
    }
    printf("\nA/B: voltar\n");
    console_render();
    wait_for_ack();
}

static void shutdown_cartridge(void)
{
    if (cart_active) {
        (void)trpak_shutdown();
        cart_active = false;
    }
}

static int refresh_cartridge(void)
{
    shutdown_cartridge();

    int result = trpak_init();
    if (result == TRPAK_OK) {
        cart_active = true;
        cart_known = true;
    } else {
        cart_active = false;
        cart_known = false;
    }
    return result;
}

static int ensure_sd(void)
{
    if (sd_ready) {
        return TRPAK_OK;
    }

    sd_ready = debug_init_sdfs("sd:/", -1);
    return sd_ready ? TRPAK_OK : APP_ERR_SD_MOUNT;
}

static int ensure_dump_directory(void)
{
    int result = ensure_sd();
    if (result != TRPAK_OK) {
        return result;
    }

    errno = 0;
    if (mkdir(DUMP_DIRECTORY, 0777) != 0 && errno != EEXIST) {
        return APP_ERR_SD_DIRECTORY;
    }
    return TRPAK_OK;
}

static void safe_title(char output[32])
{
    size_t write_index = 0u;
    size_t i;

    for (i = 0u; i < sizeof(trcart.title) && trcart.title[i] != '\0'; i++) {
        unsigned char value = (unsigned char)trcart.title[i];
        char sanitized;

        if ((value >= 'A' && value <= 'Z') ||
            (value >= 'a' && value <= 'z') ||
            (value >= '0' && value <= '9') || value == '-' || value == '_') {
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

static int open_unique_file(const char *extension, char path[128], FILE **file)
{
    char title[32];
    unsigned int suffix;

    safe_title(title);
    *file = NULL;

    for (suffix = 0u; suffix <= 99u; suffix++) {
        bool exists;
        int length;
        if (suffix == 0u) {
            length = snprintf(path, 128, "%s/%s.%s",
                              DUMP_DIRECTORY, title, extension);
        } else {
            length = snprintf(path, 128, "%s/%s-%02u.%s",
                              DUMP_DIRECTORY, title, suffix, extension);
        }
        if (length < 0 || length >= 128) {
            return APP_ERR_FILE_OPEN;
        }
        int result = path_exists(path, &exists);
        if (result != TRPAK_OK) {
            return result;
        }
        if (exists) {
            continue;
        }

        *file = fopen(path, "wb");
        return *file != NULL ? TRPAK_OK : APP_ERR_FILE_OPEN;
    }
    return APP_ERR_NAME_EXHAUSTED;
}

static int cartridge_ready(void)
{
    uint8_t status;
    int result = trpak_get_status(&status);

    if (result != TRPAK_OK) {
        return result;
    }
    if ((status & TRPAK_STATUS_REMOVED) != 0u) {
        return TRPAK_ERR_NO_CARTRIDGE;
    }
    if ((status & TRPAK_STATUS_POWERED) == 0u) {
        return TRPAK_ERR_POWER_OFF;
    }
    if ((status & TRPAK_STATUS_READY) == 0u ||
        (status & TRPAK_STATUS_IS_RESETTING) != 0u) {
        return TRPAK_ERR_ACCESS_STATE;
    }
    return TRPAK_OK;
}

static int flush_buffer(FILE *file, const uint8_t *buffer, size_t size)
{
    if (size == 0u) {
        return TRPAK_OK;
    }
    return fwrite(buffer, 1u, size, file) == size
        ? TRPAK_OK
        : APP_ERR_FILE_WRITE;
}

static void draw_progress(const char *operation, const char *path,
                          size_t done, size_t total)
{
    unsigned long percent = total == 0u
        ? 0u
        : (unsigned long)((done * 100u) / total);

    console_clear();
    printf("%s\n\n", APP_NAME);
    printf("%s\n", operation);
    printf("%s\n\n", path);
    printf("%lu / %lu KiB  (%lu%%)\n",
           (unsigned long)(done / 1024u),
           (unsigned long)(total / 1024u),
           percent);
    printf("\nNao remova a fita ou o Transfer Pak.\n");
    console_render();
}

static int dump_rom_stream(FILE *file, const char *path, size_t *bytes_written)
{
    uint8_t io_buffer[IO_BUFFER_SIZE];
    size_t buffered = 0u;
    size_t total = 0u;
    uint16_t bank;
    int result = TRPAK_OK;

    *bytes_written = 0u;

    if (trcart.mapper == TRPAK_MAPPER_MBC1 && trcart.rombanks > 32u) {
        return TRPAK_ERR_UNSUPPORTED_CARTRIDGE;
    }
    if (trcart.mapper == TRPAK_MAPPER_HUC1 && trcart.rombanks > 64u) {
        return TRPAK_ERR_UNSUPPORTED_CARTRIDGE;
    }

    draw_progress("Salvando ROM...", path, 0u, trcart.romsize);

    for (bank = 0u; bank < trcart.rombanks; bank++) {
        uint32_t address;

        result = cartridge_ready();
        if (result != TRPAK_OK) {
            break;
        }
        result = trpak_select_rom_bank(bank);
        if (result != TRPAK_OK) {
            break;
        }

        for (address = 0xC000u; address <= 0xFFE0u;
             address += TRPAK_TRANSFER_BLOCK_SIZE) {
            result = cartridge_ready();
            if (result != TRPAK_OK) {
                break;
            }
            result = trpak_read_rom_block((uint16_t)address,
                                          &io_buffer[buffered]);
            if (result != TRPAK_OK) {
                break;
            }
            buffered += TRPAK_TRANSFER_BLOCK_SIZE;
            total += TRPAK_TRANSFER_BLOCK_SIZE;

            if (buffered == sizeof(io_buffer)) {
                result = flush_buffer(file, io_buffer, buffered);
                if (result != TRPAK_OK) {
                    break;
                }
                buffered = 0u;
            }
        }
        if (result != TRPAK_OK) {
            break;
        }
        draw_progress("Salvando ROM...", path, total, trcart.romsize);
    }

    if (result == TRPAK_OK) {
        result = flush_buffer(file, io_buffer, buffered);
    }
    *bytes_written = total;

    {
        int reset_result = trpak_select_rom_bank(0u);
        if (result == TRPAK_OK && reset_result != TRPAK_OK) {
            result = reset_result;
        }
    }
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
    uint8_t io_buffer[IO_BUFFER_SIZE];
    size_t buffered = 0u;
    size_t total = 0u;
    uint16_t bank;
    int result = TRPAK_OK;

    *bytes_written = 0u;
    if (!trcart.ram || trcart.ramsize == 0u) {
        return TRPAK_ERR_NO_RAM;
    }

    draw_progress("Salvando RAM...", path, 0u, trcart.ramsize);

    for (bank = 0u; bank < trcart.rambanks; bank++) {
        size_t bank_size = ram_bytes_for_bank(bank);
        size_t bank_offset;

        result = cartridge_ready();
        if (result != TRPAK_OK) {
            break;
        }
        result = trpak_select_ram_bank(bank);
        if (result != TRPAK_OK) {
            break;
        }

        for (bank_offset = 0u; bank_offset < bank_size;
             bank_offset += TRPAK_TRANSFER_BLOCK_SIZE) {
            uint8_t *block = &io_buffer[buffered];
            uint16_t address = (uint16_t)(0xE000u + bank_offset);

            result = cartridge_ready();
            if (result != TRPAK_OK) {
                break;
            }
            result = trpak_read_ram_block(address, block);
            if (result != TRPAK_OK) {
                break;
            }
            if (trcart.mapper == TRPAK_MAPPER_MBC2) {
                normalize_mbc2(block);
            }
            buffered += TRPAK_TRANSFER_BLOCK_SIZE;
            total += TRPAK_TRANSFER_BLOCK_SIZE;

            if (buffered == sizeof(io_buffer)) {
                result = flush_buffer(file, io_buffer, buffered);
                if (result != TRPAK_OK) {
                    break;
                }
                buffered = 0u;
            }
        }
        if (result != TRPAK_OK) {
            break;
        }
        draw_progress("Salvando RAM...", path, total, trcart.ramsize);
    }

    if (result == TRPAK_OK) {
        result = flush_buffer(file, io_buffer, buffered);
    }
    *bytes_written = total;

    {
        int cleanup_result = trpak_disable_ram();
        if (result == TRPAK_OK && cleanup_result != TRPAK_OK) {
            result = cleanup_result;
        }
    }
    return result;
}

static void show_info(void)
{
    int result = refresh_cartridge();

    console_clear();
    printf("%s\n\n", APP_NAME);
    printf("Info\n\n");
    if (result != TRPAK_OK) {
        printf("Transfer Pak/fita: ERRO\n");
        printf("%s (%d)\n\n", app_error_string(result), result);
        printf("Confira o controle na porta 1, o\n");
        printf("Transfer Pak e a fita de Game Boy.\n");
    } else {
        const char *system = trcart.gbc == 0xC0u
            ? "Game Boy Color only"
            : (trcart.gbc == 0x80u ? "Game Boy / Color" : "Game Boy");

        printf("Title: %s\n", trcart.title);
        printf("System: %s\n", system);
        printf("Mapper: %s\n", mapper_name(trcart.mapper));
        printf("Cart type: 0x%02X\n", trcart.cartridge_type);
        printf("ROM: %lu KiB (%u banks)\n",
               (unsigned long)(trcart.romsize / 1024u), trcart.rombanks);
        if (trcart.ram) {
            printf("RAM: %lu bytes (%u banks)\n",
                   (unsigned long)trcart.ramsize, trcart.rambanks);
        } else {
            printf("RAM: none\n");
        }
        printf("Battery: %s  RTC: %s\n",
               trcart.battery ? "yes" : "no",
               trcart.rtc ? "yes" : "no");
        printf("Rumble: %s  SGB: %s\n",
               trcart.rumble ? "yes" : "no",
               trcart.sgb == 0x03u ? "yes" : "no");
    }
    printf("\nmicroSD: %s\n", sd_ready ? "ready" : "not mounted");
    printf("\nA/B: voltar\n");
    console_render();
    wait_for_ack();
}

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
    printf("%s\n\n", APP_NAME);
    printf("Inicializando Transfer Pak...\n");
    console_render();

    result = refresh_cartridge();
    if (result != TRPAK_OK) {
        snprintf(detail, sizeof(detail), "%s (%d)",
                 app_error_string(result), result);
        show_message("Falha", detail,
                     "Confira o controle, Transfer Pak e fita.");
        return;
    }

    if (save_ram && (!trcart.ram || trcart.ramsize == 0u)) {
        show_message("Save RAM", "Esta fita nao possui RAM.", NULL);
        shutdown_cartridge();
        return;
    }

    result = ensure_dump_directory();
    if (result == TRPAK_OK) {
        const char *extension = save_ram
            ? "sav"
            : (trcart.gbc != 0u ? "gbc" : "gb");
        result = open_unique_file(extension, path, &file);
    }
    if (result == TRPAK_OK) {
        result = save_ram
            ? dump_ram_stream(file, path, &bytes_written)
            : dump_rom_stream(file, path, &bytes_written);
    }

    if (file != NULL) {
        partial_created = true;
        if (fclose(file) != 0 && result == TRPAK_OK) {
            result = APP_ERR_FILE_CLOSE;
        }
        if (result != TRPAK_OK) {
            partial_removed = remove(path) == 0;
        }
    }
    shutdown_cartridge();

    if (result == TRPAK_OK) {
        snprintf(detail, sizeof(detail), "%lu bytes gravados.",
                 (unsigned long)bytes_written);
        show_message(save_ram ? "RAM salva" : "ROM salva", path, detail);
    } else {
        snprintf(detail, sizeof(detail), "%s (%d)",
                 app_error_string(result), result);
        show_message(!partial_created
                         ? "Falha"
                         : (partial_removed
                                ? "Falha; arquivo incompleto removido"
                                : "Falha; remova o arquivo incompleto"),
                     detail,
                     partial_created && !partial_removed ? path : NULL);
    }
}

static bool menu_item_selectable(int item)
{
    return item != MENU_RESTORE_RAM;
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
    printf("%s\n", APP_NAME);
    printf("SummerCart64 + Transfer Pak\n\n");

    if (cart_known) {
        printf("Cart: %s\n", trcart.title);
    } else {
        printf("Cart: not detected\n");
    }
    printf("microSD: %s\n\n", sd_ready ? "ready" : "not mounted");

    for (i = 0; i < MENU_ITEM_COUNT; i++) {
        const char *cursor = i == selected ? ">" : " ";
        const char *suffix = i == MENU_RESTORE_RAM ? " [disabled]" : "";
        printf("%s %s%s\n", cursor, menu_labels[i], suffix);
    }

    printf("\nD-Pad: mover   A: selecionar\n");
    console_render();
}

int main(void)
{
    int selected = MENU_INFO;
    bool running = true;

    console_init();
    console_set_render_mode(RENDER_MANUAL);
    joypad_init();
    debug_init(DEBUG_FEATURE_LOG_USB | DEBUG_FEATURE_LOG_EMU);

    (void)ensure_sd();
    (void)refresh_cartridge();
    draw_menu(selected);

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
        } else if (pressed.a || pressed.start) {
            switch ((menu_item)selected) {
            case MENU_INFO:
                show_info();
                break;
            case MENU_SAVE_ROM:
                perform_dump(false);
                break;
            case MENU_SAVE_RAM:
                perform_dump(true);
                break;
            case MENU_RESTORE_RAM:
                break;
            case MENU_EXIT:
                running = false;
                break;
            }
            if (running) {
                draw_menu(selected);
            }
        }
        wait_ms(16);
    }

    shutdown_cartridge();
    if (sd_ready) {
        debug_close_sdfs();
    }

    console_clear();
    printf("%s\n\n", APP_NAME);
    printf("Transfer Pak desligado.\n");
    printf("Pode reiniciar o Nintendo 64.\n");
    console_render();

    joypad_close();
    return 0;
}
