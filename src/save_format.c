/*
 * GB Transfer Dumper - emulator save-file compatibility rules.
 */

#include "save_format.h"

/* Common MBC3 RTC footer sizes. The 48-byte form includes 4 bytes of padding
 * after the 44-byte payload and is what current mGBA writes. */
#define MBC3_RTC_TRAILER_SIZE        44u
#define MBC3_RTC_PADDED_TRAILER_SIZE 48u

bool save_format_size_is_compatible(uint64_t file_size, uint64_t ram_size,
                                    bool has_rtc)
{
    uint64_t trailer_size;

    if (file_size == ram_size) {
        return true;
    }
    if (!has_rtc || file_size < ram_size) {
        return false;
    }

    trailer_size = file_size - ram_size;
    return trailer_size == MBC3_RTC_TRAILER_SIZE ||
           trailer_size == MBC3_RTC_PADDED_TRAILER_SIZE;
}
