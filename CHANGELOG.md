# Changelog

All notable changes to GB Transfer Dumper are documented in this file.
The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [1.0.1] - 2026-08-17

### Fixed

- **Transfer Pak resets:** Dumps, save backups, and restores now retry the
  affected block after a reset reselects the cartridge bank and re-enables RAM.
  Repeated resets fail safely with `TRPAK_ERR_TRANSFER_TIMEOUT`.
- **Mapper safeguards:** Unsupported bank layouts are rejected before a
  transfer starts, preventing incomplete, duplicated, or invalid ROM and save
  dumps. This covers ROM-only cartridges, MBC2, MBC3, MBC5 with rumble, the
  Game Boy Camera, HuC1, and MBC1M multicarts.
- **MBC1M multicarts:** 1 MiB MBC1 multicarts now use MBC1M banking, rather
  than duplicating banks above `0x0F`.
- **Cartridge metadata:** ROM-only cartridges above two banks are refused;
  cartridges that claim RAM with a zero RAM-size code remain ROM-dumpable; and
  modern Game Boy Color titles no longer include the manufacturer code in file
  names.
- **MBC2 save restore:** Verification now accounts for MBC2's 4-bit save cells,
  so standard emulator `.sav` files restore successfully.
- **Storage and diagnostics:** The directory probe clears stale `errno` values,
  and debug format specifiers now match their arguments.

### Changed

- **Dependency:** Updated libtrpak to
  [`4a55f4d`](https://github.com/alexishida/libtrpak/commit/4a55f4d567ee0cbbf805982089fdd5d325a72617),
  which provides reset recovery, MBC1M support, mapper-limit validation, and
  corrected cartridge-header parsing.
- **Transfer implementation:** ROM and save operations now use libtrpak's
  streaming bulk helpers. The application streams 32-byte blocks through a
  4 KiB file buffer instead of duplicating mapper and readiness logic.
- **Restore-file selection:** A save created for the inserted cartridge
  (`TITLE.sav` or `TITLE-NN.sav`) is preferred; `.sav` extensions are matched
  case-insensitively.
- **Application structure:** Split `src/main.c` into focused app, cart,
  storage, transfer, and UI modules. The Makefile now compiles every `src/*.c`.
- **Performance:** The extra status checks make transfers slower than 1.0.0,
  but prevent resets from silently corrupting output. Progress redraws are
  limited to five per second.

## [1.0.0] - 2026-08-16

### Added

- ROM dumping for Game Boy (`.gb`) and Game Boy Color (`.gbc`) cartridges.
- Save RAM backup and verified restoration with size validation and overwrite
  confirmation.
- Cartridge information, high-contrast operation screens, progress metrics,
  unique output names, and cleanup of incomplete dump files.
- USB and emulator diagnostics when built with `ENABLE_DEBUG`.

### Changed

- ROM dumps use `sd:/romdump` and save operations use `sd:/savedump`, falling
  back to `sd:/` when their respective directory is unavailable.
- Updated libtrpak to commit `22aa35fb928247686b0f9dbcdf3f901768c96d70`.
- Improved mapper limits, Transfer Pak readiness handling, timer conversion,
  save-restoration buffering, incremental CRC32 processing, and libdragon
  directory and timer integration.
