/*
 * GB Transfer Dumper - active Transfer Pak status compatibility filter.
 */

#ifndef TRANSFER_STATUS_H
#define TRANSFER_STATUS_H

#include <stdbool.h>
#include <stdint.h>

/** State needed to turn a sticky WAS_RESET level into one reset event. */
typedef struct {
    bool reset_high;
} transfer_status_filter_t;

/**
 * @brief Normalizes status quirks after strict cartridge initialization.
 *
 * @param filter           Per-transfer edge-detection state.
 * @param raw_status       Status byte read from Transfer Pak address 0xB000.
 * @param readiness_fixed  Receives true when READY/RESETTING was normalized.
 * @param reset_suppressed Receives true when a repeated WAS_RESET was cleared.
 * @return Status byte safe for libtrpak's active-transfer checks.
 */
uint8_t transfer_status_filter_active(transfer_status_filter_t *filter,
                                      uint8_t raw_status,
                                      bool *readiness_fixed,
                                      bool *reset_suppressed);

#endif /* TRANSFER_STATUS_H */
