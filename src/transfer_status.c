/*
 * GB Transfer Dumper - active Transfer Pak status compatibility filter.
 */

#include "transfer_status.h"

#include "libtrpak.h"

uint8_t transfer_status_filter_active(transfer_status_filter_t *filter,
                                      uint8_t raw_status,
                                      bool *readiness_fixed,
                                      bool *reset_suppressed)
{
    uint8_t status = raw_status;

    if (readiness_fixed != NULL) {
        *readiness_fixed = false;
    }
    if (reset_suppressed != NULL) {
        *reset_suppressed = false;
    }
    if (filter == NULL) {
        return status;
    }

    /* WAS_RESET is documented as read-and-clear, but some real hardware keeps
     * it high across consecutive reads. Preserve the rising edge and suppress
     * only repetitions of the same level. A low reading arms the next edge. */
    if ((raw_status & TRPAK_STATUS_WAS_RESET) != 0u) {
        if (filter->reset_high) {
            status &= (uint8_t)~TRPAK_STATUS_WAS_RESET;
            if (reset_suppressed != NULL) {
                *reset_suppressed = true;
            }
        } else {
            filter->reset_high = true;
        }
    } else {
        filter->reset_high = false;
    }

    /* Initialization already performed the strict readiness handshake.
     * During active I/O, POWERED and REMOVED remain authoritative while these
     * two readiness bits are advisory on affected Transfer Pak revisions. */
    if ((raw_status & TRPAK_STATUS_POWERED) != 0u &&
        (raw_status & TRPAK_STATUS_REMOVED) == 0u &&
        ((raw_status & TRPAK_STATUS_READY) == 0u ||
         (raw_status & TRPAK_STATUS_IS_RESETTING) != 0u)) {
        status |= TRPAK_STATUS_READY;
        status &= (uint8_t)~TRPAK_STATUS_IS_RESETTING;
        if (readiness_fixed != NULL) {
            *readiness_fixed = true;
        }
    }

    return status;
}
