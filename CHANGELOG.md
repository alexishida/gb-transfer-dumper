# Changelog

All notable changes to GB Transfer Dumper are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project follows [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

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
