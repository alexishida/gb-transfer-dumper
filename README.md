# GB Transfer Dumper

Created by Alex Ishida.

An N64 homebrew utility that copies ROM and RAM from a Game Boy or Game Boy
Color cartridge to a SummerCart64 microSD card through a Transfer Pak.

The menu provides:

- `Info`: re-reads the cartridge and displays its title, system, mapper, type,
  ROM/RAM capacities, and detected save, battery, RTC, and rumble support;
- `Dump ROM`: creates a `.gb` or `.gbc` dump, depending on the cartridge;
- `Backup Save`: creates a `.sav` backup when the cartridge has RAM;
- `Restore Save`: writes a compatible `.sav` file back to the cartridge after
  an explicit overwrite confirmation;
- `Exit`: asks for confirmation, powers down the Transfer Pak, and exits the
  program.

## Interface

The high-contrast console interface is designed for CRTs and modern displays.
The main screen shows cartridge and microSD status beside the available actions;
the `Info` screen is a cartridge card with title, system, mapper, ROM/RAM
capacity, and detected features. Press `A` in `Info` to start a ROM dump or
`START` to read the cartridge again. `START` from the main menu refreshes both
the cartridge and microSD state. All write operations require confirmation.

Dump, backup, and restore screens show a 20-segment progress bar, percentage,
transferred size, current transfer speed, estimated remaining time, and target
path. The interface identifies the application as **GB Transfer Dumper v1.0**
by Alex Ishida.

ROM dumps are written to `sd:/romdump` when that directory already exists;
save backups and restores use `sd:/savedump` when it exists. Current libdragon
SD filesystem builds cannot create directories, so an operation automatically
writes to or reads from `sd:/` when its respective directory is absent. Create
both directories on the microSD card to keep ROMs and saves organized.

Existing dumps are never overwritten: the program first tries `TITLE.ext`,
then `TITLE-01.ext`, and so on. A partial file is removed if reading or writing
fails. Save restoration scans the active save location for a `.sav` file,
verifies that its size matches the inserted cartridge's RAM, and then asks for
confirmation before overwriting the cartridge save. When more than one `.sav`
file exists, the first one returned by the filesystem is used.

## Safety and reliability

- Cartridge readiness is checked throughout each transfer, with a five-second
  readiness timeout;
- the application uses a 4 KiB streaming buffer, so it does not need to hold a
  complete ROM in N64 memory;
- failed ROM or save backups remove their incomplete output file whenever
  possible;
- save restore rejects cartridges without RAM and files whose size differs from
  the cartridge RAM capacity;
- debug logging for transfer, filesystem, and hardware errors is available
  when the program is built with `ENABLE_DEBUG`.

## Hardware

- Nintendo 64;
- SummerCart64 with a microSD card;
- Nintendo 64 controller connected to port 1;
- Transfer Pak (NUS-019);
- Game Boy or Game Boy Color cartridge.

Do not remove the cartridge or Transfer Pak while an operation is in progress.

## Stack and dependencies

- [libdragon](https://github.com/DragonMinded/libdragon), supplied by the N64
  toolchain;
- [libtrpak](https://github.com/alexishida/libtrpak), downloaded by the
  Makefile and pinned to commit `22aa35fb928247686b0f9dbcdf3f901768c96d70`.

The program uses libtrpak's public banking/block API and streams data to the
microSD card. It therefore does not need to keep an entire ROM in RDRAM and
does not require an Expansion Pak for 4 or 8 MiB dumps.

## Installing libdragon (Linux / WSL2)

This project expects a native libdragon installation with `N64_INST` set. The
following follows the official [libdragon installation guide](https://github.com/DragonMinded/libdragon/wiki/Installing-libdragon)
using its prebuilt MIPS64 toolchain.

1. Install the host build prerequisites:

   ```sh
   sudo apt-get update
   sudo apt-get install build-essential git
   ```

2. Download the current `gcc-toolchain-mips64-*.deb` package from the
   [libdragon releases page](https://github.com/DragonMinded/libdragon/releases),
   then install it:

   ```sh
   sudo dpkg -i /path/to/gcc-toolchain-mips64-*.deb
   ```

3. Open a new terminal, then build and install libdragon itself:

   ```sh
   git clone https://github.com/DragonMinded/libdragon.git
   cd libdragon
   ./build.sh
   ```

4. Confirm that the environment is available:

   ```sh
   echo "$N64_INST"
   test -f "$N64_INST/include/n64.mk"
   ```

The `N64_INST` variable is configured by the toolchain package. If either
verification command fails, open a new shell and consult the official guide.

## Building

```sh
cd /path/to/gb-transf-dumpper
make
```

The output will be created as `gb-transf-dumper.z64`. Copy it to the
SummerCart64 microSD card and run it from the flashcart menu.

To clean only build artifacts while keeping the downloaded dependency:

```sh
make clean
```

## libtrpak limitations

- MMM01, MBC4, TAMA5, and HuC3 are detected but do not have banking support;
- MBC1 cartridges above 32 banks and HuC1 cartridges above 64 banks are
  rejected to prevent incomplete dumps;
- Game Boy Camera and HuC1 remain experimental paths;
- RTC and rumble are detected only; they are not separately backed up or
  restored;
- `Restore Save` automatically chooses the first `.sav` file returned from
  `sd:/savedump` (or `sd:/` when that directory is absent), so keep only the
  intended save there before restoring;
- restoring a save overwrites cartridge RAM. Confirm that the displayed file
  and cartridge are correct before accepting the warning.
