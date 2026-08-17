# GB Transfer Dumper

An N64 homebrew utility that copies ROM and RAM from a Game Boy or Game Boy
Color cartridge to a SummerCart64 microSD card through a Transfer Pak.

The menu provides:

- `Info`: re-reads the cartridge and displays its title, system, mapper, type,
  sizes, and capabilities;
- `Save ROM`: creates a `.gb` or `.gbc` dump;
- `Save RAM`: creates a `.sav` backup when the cartridge has RAM;
- `Restore RAM (off)`: shown as disabled and has no write implementation;
- `Exit`: powers down the Transfer Pak and exits the program.

Files are written to `sd:/gbdump`. Existing dumps are never overwritten: the
program first tries `TITLE.ext`, then `TITLE-01.ext`, and so on. A partial file
is removed if reading or writing fails.

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
  Makefile and pinned to commit `eb740fc2ab602c1be6e541ce8bd85c7026eb478e`.

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
- RTC and rumble are detected only.

RAM restore is deliberately excluded from this build. No menu option calls
`trpak_write_save` or `trpak_write_ram_block`.
