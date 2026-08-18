/*
 * GB Transfer Dumper - console rendering and controller input.
 *
 * Every screen is drawn into libdragon's manual-render console: clear, print,
 * render. The blocking helpers here own the only input loops in the program.
 */

#ifndef UI_H
#define UI_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/**
 * @brief State for one long-running transfer's progress screen.
 *
 * Created with ui_progress_begin() and passed to ui_progress_draw() as the
 * transfer advances, so the streaming loops do not have to carry the label,
 * destination and start time separately.
 */
typedef struct {
    const char *operation; /**< Headline, e.g. "Dumping ROM...". */
    const char *path;      /**< File being written or read. */
    size_t total;          /**< Expected byte count, for the bar and the ETA. */
    uint64_t start_ticks;  /**< Value of timer_ticks() when the transfer began. */
} ui_progress_t;

/** @brief Draws the two-line title bar shown on every screen. */
void ui_draw_header(void);

/**
 * @brief Draws the closing lines of a screen.
 *
 * @param controls Button legend for the current screen.
 */
void ui_draw_footer(const char *controls);

/** @brief Renders "Detected" or "--" for a feature flag. */
const char *ui_yes_no(bool value);

/**
 * @brief Shows a message and waits for A, B or START.
 *
 * @param title Headline.
 * @param line1 First body line, or NULL.
 * @param line2 Second body line, or NULL.
 */
void ui_show_message(const char *title, const char *line1, const char *line2);

/**
 * @brief Shows a prompt and waits for a decision.
 *
 * @param title Headline.
 * @param line1 First body line.
 * @param line2 Second body line, or NULL.
 * @return true when A or START was pressed, false on B.
 */
bool ui_confirm(const char *title, const char *line1, const char *line2);

/**
 * @brief Initialises a progress screen and draws it at 0%.
 *
 * @param progress  Receives the progress state.
 * @param operation Headline for the screen.
 * @param path      Destination or source path to display.
 * @param total     Expected byte count.
 */
void ui_progress_begin(ui_progress_t *progress, const char *operation,
                       const char *path, size_t total);

/**
 * @brief Redraws a progress screen.
 *
 * @param progress State from ui_progress_begin().
 * @param done     Bytes transferred so far.
 */
void ui_progress_draw(const ui_progress_t *progress, size_t done);

#endif /* UI_H */
