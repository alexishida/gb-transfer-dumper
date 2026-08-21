/*
 * GB Transfer Dumper - Transfer Pak lifecycle and cartridge metadata.
 */

#include "cart.h"

#include <libdragon.h>

#include <string.h>

#include "app.h"
#include "transfer.h"

static bool cart_active;
static bool cart_known;
static int cart_controller = JOYPAD_PORT_1;

void cart_shutdown(void)
{
    if (cart_active) {
        (void)trpak_shutdown();
        cart_active = false;
        debug_log(DEBUG_LEVEL_INFO, "Cartridge powered down\n");
    }
}

int cart_refresh(void)
{
    int result = TRPAK_ERR_NO_CARTRIDGE;
    int port;

    cart_shutdown();

    /* libtrpak drives the one Transfer Pak port configured with
     * trpak_configure_io(), so this application scans every controller port
     * on each refresh: the Transfer Pak usually rides on the menu controller
     * (port 1), but it may safely sit in a spare port while port 1 stays free
     * for navigation. The first port whose full bring-up handshake succeeds
     * wins, and that port keeps the backend through the following transfer. */
    for (port = JOYPAD_PORT_1; port < JOYPAD_PORT_COUNT; port++) {
        result = transfer_configure_port(port);
        if (result != TRPAK_OK) {
            debug_log(DEBUG_LEVEL_ERROR,
                      "Failed to configure controller port %d: %d\n",
                      port + 1, result);
            continue;
        }

        result = trpak_init();
        if (result == TRPAK_OK) {
            cart_controller = port;
            cart_active = true;
            cart_known = true;
            debug_log(DEBUG_LEVEL_INFO,
                      "Cartridge initialized on controller port %d: %s\n",
                      port + 1, cart_title());
            return TRPAK_OK;
        }
        debug_log(DEBUG_LEVEL_VERBOSE,
                  "No Transfer Pak on controller port %d: %s (%d)\n",
                  port + 1, app_error_string(result), result);
    }

    cart_active = false;
    cart_known = false;
    debug_log(DEBUG_LEVEL_ERROR, "Cartridge init failed on all ports: %d\n",
              result);
    return result;
}

bool cart_is_active(void)
{
    return cart_active;
}

bool cart_is_known(void)
{
    return cart_known;
}

int cart_controller_port(void)
{
    return cart_controller;
}

bool cart_has_ram(void)
{
    return trcart.ram != 0u && trcart.ramsize != 0u;
}

const char *cart_title(void)
{
    return trcart.title[0] != '\0' ? trcart.title : "Untitled cartridge";
}

const char *cart_system_name(void)
{
    if (trcart.gbc == 0xC0u) {
        return "Game Boy Color";
    }
    return trcart.gbc == 0x80u ? "Game Boy / Color" : "Game Boy";
}

const char *cart_rom_extension(void)
{
    return trcart.gbc != 0u ? "gbc" : "gb";
}

const char *cart_mapper_name(unsigned char mapper)
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

void cart_safe_title(char *output, size_t size)
{
    size_t write_index = 0u;
    size_t i;

    if (output == NULL || size == 0u) {
        return;
    }
    memset(output, 0, size);

    for (i = 0u; i < sizeof(trcart.title) && trcart.title[i] != '\0'; i++) {
        unsigned char value = (unsigned char)trcart.title[i];
        bool keep = (value >= 'A' && value <= 'Z') ||
                    (value >= 'a' && value <= 'z') ||
                    (value >= '0' && value <= '9') ||
                    value == '-' || value == '_';
        char sanitized = keep ? (char)value : '_';

        if (write_index + 1u >= size) {
            break;
        }
        /* Collapse runs of substituted characters into one underscore. */
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
        const char *fallback = "cartridge";
        size_t length = strlen(fallback);
        if (length >= size) {
            length = size - 1u;
        }
        memcpy(output, fallback, length);
        output[length] = '\0';
    } else {
        output[write_index] = '\0';
    }

    debug_log(DEBUG_LEVEL_VERBOSE, "Sanitized title: %s\n", output);
}
