# Changelog

All notable changes to GB Transfer Dumper are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project follows [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [1.0.1] - 2026-08-17

### Fixed

- A Transfer Pak reset in the middle of a dump, backup or restore no longer
  corrupts the result silently. A reset leaves the accessory powered, present
  and ready while the mapper returns to its power-on state, so the selected
  bank is gone and cartridge RAM re-locks; the affected block is now repeated
  after the bank is re-selected, and repeated resets end the operation with
  `TRPAK_ERR_TRANSFER_TIMEOUT` instead of a plausible-looking file.
- 1 MiB MBC1 multicarts are dumped with the MBC1M banking layout instead of
  conventional MBC1 wiring, which turned every bank above `0x0F` into a
  duplicate of lower content.
- Cartridges without an MBC that declare more than two banks are refused
  instead of being "dumped" from VRAM and cartridge RAM.
- A header that declares more banks than its mapper can select is refused
  before the first byte moves, rather than aborting from the middle of the
  traversal and leaving a partial file behind. This covers MBC2 above 16 ROM
  banks, the Game Boy Camera above 64, MBC3 above 8 RAM banks and rumble MBC5
  above 8, none of which were checked.
- A cartridge that declares RAM with a size code of `0` is dumped as a RAM-less
  cartridge instead of failing to initialize, which had also made its ROM
  undumpable.
- Modern Game Boy Color titles no longer carry the four-byte manufacturer code
  into the title, and therefore into the generated file name.
- Restoring a `.sav` to an MBC2 cartridge no longer fails verification. MBC2
  stores 4-bit cells, so the upper nibble of every byte written is discarded by
  the hardware; the read-back was masked but the source was not, which made any
  save whose bytes carry a non-zero upper nibble — the usual emulator output —
  abort with `TRPAK_ERR_VERIFY_FAILED` on the first block.
- `errno` is cleared before the `stat()` call that probes for `sd:/romdump` and
  `sd:/savedump`, so a value left behind by an unrelated call can no longer be
  mistaken for a real failure and turn the fallback to `sd:/` into an error.
- Corrected the debug logging format specifiers for `st_size`, `ramsize` and
  the bank offsets, which did not match the types being passed.

### Changed

- Updated libtrpak to commit
  `4a55f4d567ee0cbbf805982089fdd5d325a72617`, which is where the reset,
  MBC1M, bank-limit and header fixes above come from.
- Transfers are now performed by libtrpak's bulk helpers, with each 32-byte
  block streamed to or from the microSD card through the library's transfer
  callbacks. `src/transfer.c` no longer walks banks, masks MBC2 nibbles or
  re-checks cartridge readiness itself; that logic existed twice and only the
  copy inside libtrpak was fixed.
- Dumps and restores are slower than in 1.0.0: libtrpak reads the accessory
  status before and after every 32-byte transaction, which is what closes the
  window in which a reset went unnoticed. The progress screen is redrawn at
  most five times per second instead of once per bank.
- Removed the application's status cache. The reset-detected status bit is
  read-and-clear, so holding a status byte for 50 ms could report a stale
  reading and consume a reset that the transfer needed to see.
- `Restore Save` now prefers a `.sav` that this program created for the inserted
  cartridge (`TITLE.sav` or `TITLE-NN.sav`) and only falls back to the first
  `.sav` in the directory when no such file exists, which makes restoring the
  wrong game's save much less likely when several backups share a directory.
- `.sav` files are recognised case-insensitively, so `.SAV` is also offered.
- `debug_log()` carries a `printf` format attribute in every build, so format
  and argument mismatches are reported by the compiler even without
  `ENABLE_DEBUG`.
- Split the single `src/main.c` into `app`, `cart`, `storage`, `transfer` and
  `ui` modules; `src/main.c` now holds only the menu and the operation flows.
  The Makefile compiles every `src/*.c`.
- Removed the dead menu-selectability predicate that always returned true.

## [1.0.0] - 2026-08-16

### Added

- Game Boy and Game Boy Color ROM dumping to `.gb` and `.gbc` files.
- Save RAM backup to `.sav` files.
- Save RAM restoration with file-size validation, overwrite confirmation, and
  block-by-block readback verification.
- Cartridge information screen with title, system, mapper, ROM/RAM sizes, and
  battery, RTC, rumble, and save capabilities.
- High-contrast console interface with operation confirmations, progress bar,
  transfer speed, estimated remaining time, and destination path.
- Automatic unique filenames to prevent existing dumps from being overwritten.
- Automatic removal of incomplete ROM and save backup files after failures.
- USB and emulator diagnostic logging when built with `ENABLE_DEBUG`.

### Changed

- ROM dumps use `sd:/romdump` when it exists, falling back to `sd:/`.
- Save backups and restores use `sd:/savedump` when it exists, falling back to
  `sd:/`.
- Updated libtrpak to commit
  `22aa35fb928247686b0f9dbcdf3f901768c96d70`.
- MBC1 ROM dumping supports up to 128 banks; HuC1 remains limited to 64 banks.
- Transfer Pak readiness and reset handling are delegated to libtrpak during
  initialization, while active transfers monitor cartridge presence, power,
  and transport errors.
- Corrected N64 timer tick conversion used by progress calculations and status
  caching.
- Removed a redundant runtime readiness timeout that prevented transfers from
  advancing on some Transfer Pak revisions.
- Fixed save restoration buffer reuse and out-of-bounds access after 4 KiB.
- Fixed CRC32 calculation so large dumps are processed incrementally without
  reading beyond the transfer buffer.
- Replaced unsupported POSIX directory enumeration with libdragon's native
  directory API.
- Correctly initializes and shuts down the libdragon timer subsystem.
