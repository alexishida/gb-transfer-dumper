/*
 * GB Transfer Dumper - console rendering and controller input.
 */

#include "ui.h"

#include <libdragon.h>
#include <libcart/cart.h>

#include <stdio.h>

#include "app.h"
#include "cart.h"

/* Width of the progress bar in characters. */
#define PROGRESS_BAR_WIDTH 20

/* Controller poll interval for the blocking input loops, in milliseconds. */
#define INPUT_POLL_INTERVAL_MS 16

void ui_draw_header(void)
{
    printf("+--------------------------------------+\n");
    printf("| %-21s PAK: %-9s |\n", APP_NAME,
           cart_is_active() ? "CONNECTED" : "CHECK");
    printf("+--------------------------------------+\n");
}

const char* ui_cart_name(){
	switch (cart_type)
	{
		case CART_CI:
			return "64drive";
			break;
		case CART_ED:
			return "Everdrive 64";
			break;
		case CART_EDX:
			return "Everdrive 64X";
			break;
		case CART_SC:
			return "SC64";
			break;
		default:
			return "Unknown";
			break;
	}
}

void ui_draw_footer(const char *controls)
{
    printf("\n%s\n", controls);
    printf("%s | %s + Transfer Pak v%s\n", APP_AUTHOR, ui_cart_name(), APP_VERSION);
    printf("libtrpak v%s\n", trpak_version_string());
}

const char *ui_yes_no(bool value)
{
    return value ? "Detected" : "--";
}

/**
 * @brief Blocks until A, B or START is pressed.
 *
 * Reads edge transitions, so a button still held from the previous screen does
 * not count as a press here.
 */
static void wait_for_ack(void)
{
    for (;;) {
        joypad_buttons_t pressed;

        joypad_poll();
        pressed = joypad_get_buttons_pressed(JOYPAD_PORT_1);
        if (pressed.a || pressed.b || pressed.start) {
            return;
        }
        wait_ms(INPUT_POLL_INTERVAL_MS);
    }
}

void ui_show_message(const char *title, const char *line1, const char *line2)
{
    console_clear();
    ui_draw_header();
    printf("\n%s\n", title);
    printf("------------------------------------------\n");
    if (line1 != NULL) {
        printf("%s\n", line1);
    }
    if (line2 != NULL) {
        printf("%s\n", line2);
    }
    ui_draw_footer("A / B / START: continue");
    console_render();
    wait_for_ack();
}

bool ui_confirm(const char *title, const char *line1, const char *line2)
{
    console_clear();
    ui_draw_header();
    printf("\n%s\n", title);
    printf("------------------------------------------\n");
    printf("%s\n", line1);
    if (line2 != NULL) {
        printf("%s\n", line2);
    }
    ui_draw_footer("A: confirm     B: cancel");
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
        wait_ms(INPUT_POLL_INTERVAL_MS);
    }
}

void ui_progress_begin(ui_progress_t *progress, const char *operation,
                       const char *path, size_t total)
{
    progress->operation = operation;
    progress->path = path;
    progress->total = total;
    progress->start_ticks = (uint64_t)timer_ticks();
    ui_progress_draw(progress, 0u);
}

void ui_progress_draw(const ui_progress_t *progress, size_t done)
{
    char bar[PROGRESS_BAR_WIDTH + 1];
    unsigned int index;
    unsigned int filled;
    unsigned long percent = progress->total == 0u
        ? 0u
        : (unsigned long)((done * 100u) / progress->total);
    uint64_t elapsed_ms =
        (uint64_t)TICKS_TO_MS((uint64_t)timer_ticks() - progress->start_ticks);
    float rate_kb_s = 0.0f;
    unsigned long remaining_ms = 0u;

    if (percent > 100u) {
        percent = 100u;
    }
    filled = (unsigned int)((percent * PROGRESS_BAR_WIDTH) / 100u);
    for (index = 0u; index < PROGRESS_BAR_WIDTH; index++) {
        bar[index] = index < filled ? '#' : '-';
    }
    bar[PROGRESS_BAR_WIDTH] = '\0';

    if (done > 0u && elapsed_ms > 0u) {
        float rate = (float)done / (float)elapsed_ms; /* bytes per millisecond */
        rate_kb_s = rate * 1000.0f / 1024.0f;
        if (progress->total > done) {
            remaining_ms = (unsigned long)((float)(progress->total - done) / rate);
        }
    }

    console_clear();
    ui_draw_header();
    printf("\n%s\n\n", progress->operation);
    printf("%s\n", cart_title());
    printf("[%s] %3lu%%\n\n", bar, percent);
    printf("%lu / %lu KiB\n",
           (unsigned long)(done / 1024u),
           (unsigned long)(progress->total / 1024u));

    if (rate_kb_s > 0.0f) {
        printf("Speed: %.1f KiB/s\n", (double)rate_kb_s);
        printf("Remaining: %02lu:%02lu\n",
               remaining_ms / 60000u, (remaining_ms % 60000u) / 1000u);
    }

    printf("File: %s\n", progress->path);
    printf("\nDo not remove the cartridge or Transfer Pak.\n");
    ui_draw_footer("Please wait...");
    console_render();
}
