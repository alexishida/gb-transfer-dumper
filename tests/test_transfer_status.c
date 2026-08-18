#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "libtrpak.h"
#include "transfer_status.h"

static void test_readiness_quirks(void)
{
    transfer_status_filter_t filter = {0};
    bool readiness_fixed;
    bool reset_suppressed;
    uint8_t status;

    status = transfer_status_filter_active(
        &filter, TRPAK_STATUS_POWERED, &readiness_fixed, &reset_suppressed);
    assert(status == (TRPAK_STATUS_POWERED | TRPAK_STATUS_READY));
    assert(readiness_fixed);
    assert(!reset_suppressed);

    status = transfer_status_filter_active(
        &filter,
        TRPAK_STATUS_POWERED | TRPAK_STATUS_READY |
            TRPAK_STATUS_IS_RESETTING,
        &readiness_fixed, &reset_suppressed);
    assert(status == (TRPAK_STATUS_POWERED | TRPAK_STATUS_READY));
    assert(readiness_fixed);

    status = transfer_status_filter_active(
        &filter, TRPAK_STATUS_REMOVED, &readiness_fixed, &reset_suppressed);
    assert(status == TRPAK_STATUS_REMOVED);
    assert(!readiness_fixed);

    status = transfer_status_filter_active(
        &filter, 0u, &readiness_fixed, &reset_suppressed);
    assert(status == 0u);
    assert(!readiness_fixed);
}

static void test_sticky_reset_becomes_one_event(void)
{
    transfer_status_filter_t filter = {0};
    const uint8_t raw = TRPAK_STATUS_POWERED | TRPAK_STATUS_READY |
                        TRPAK_STATUS_WAS_RESET;
    bool readiness_fixed;
    bool reset_suppressed;
    uint8_t status;

    status = transfer_status_filter_active(
        &filter, raw, &readiness_fixed, &reset_suppressed);
    assert((status & TRPAK_STATUS_WAS_RESET) != 0u);
    assert(!reset_suppressed);

    status = transfer_status_filter_active(
        &filter, raw, &readiness_fixed, &reset_suppressed);
    assert((status & TRPAK_STATUS_WAS_RESET) == 0u);
    assert(reset_suppressed);

    status = transfer_status_filter_active(
        &filter, TRPAK_STATUS_POWERED | TRPAK_STATUS_READY,
        &readiness_fixed, &reset_suppressed);
    assert((status & TRPAK_STATUS_WAS_RESET) == 0u);

    status = transfer_status_filter_active(
        &filter, raw, &readiness_fixed, &reset_suppressed);
    assert((status & TRPAK_STATUS_WAS_RESET) != 0u);
    assert(!reset_suppressed);
}

static void test_combined_status_quirks(void)
{
    transfer_status_filter_t filter = {0};
    const uint8_t raw = TRPAK_STATUS_POWERED |
                        TRPAK_STATUS_IS_RESETTING |
                        TRPAK_STATUS_WAS_RESET;
    bool readiness_fixed;
    bool reset_suppressed;
    uint8_t status;

    status = transfer_status_filter_active(
        &filter, raw, &readiness_fixed, &reset_suppressed);
    assert(status == (TRPAK_STATUS_POWERED | TRPAK_STATUS_READY |
                      TRPAK_STATUS_WAS_RESET));
    assert(readiness_fixed);
    assert(!reset_suppressed);

    status = transfer_status_filter_active(
        &filter, raw, &readiness_fixed, &reset_suppressed);
    assert(status == (TRPAK_STATUS_POWERED | TRPAK_STATUS_READY));
    assert(readiness_fixed);
    assert(reset_suppressed);
}

int main(void)
{
    test_readiness_quirks();
    test_sticky_reset_becomes_one_event();
    test_combined_status_quirks();
    puts("transfer status tests: OK");
    return 0;
}
