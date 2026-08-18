/*
 * GB Transfer Dumper - emulator save-file compatibility rules.
 */

#ifndef SAVE_FORMAT_H
#define SAVE_FORMAT_H

#include <stdbool.h>
#include <stdint.h>

/**
 * @brief Checks whether a save file contains compatible cartridge SRAM.
 *
 * Plain saves must match the cartridge RAM size exactly. RTC cartridges also
 * accept the 44-byte and 48-byte MBC3 RTC trailers used by emulators such as
 * mGBA; those trailers follow the SRAM payload and are not written to RAM.
 *
 * @param file_size Entire `.sav` size in bytes.
 * @param ram_size  Inserted cartridge's SRAM capacity.
 * @param has_rtc   Whether the inserted cartridge declares an RTC.
 * @return true when the leading @p ram_size bytes are a compatible payload.
 */
bool save_format_size_is_compatible(uint64_t file_size, uint64_t ram_size,
                                    bool has_rtc);

#endif /* SAVE_FORMAT_H */
