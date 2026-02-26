# real-time-system

Embedded TinyTimber application for the MD407 (STM32F4, Cortex-M4) board using CAN and SCI (serial).

## What this project does
- Initializes CAN and SCI peripherals
- Starts a 1 kHz tone generator task
- Supports runtime control over SCI console commands
- Supports mute/unmute, volume control, background load tuning, and deadline mode toggling
- Prints received CAN payloads over SCI

## Repository layout
- `lab1_group_6/src/` application sources (`App.c`, `objects.c`)
- `lab1_group_6/inc/` application headers (`App.h`)
- `lib/tinytimber/` TinyTimber framework sources/headers
- `lib/md407/` MD407 board support package
- `bin/` host tools (used by `make console`)
- `build/` build outputs
- `md407-ram.x` linker script
- `Makefile` build rules

## Prerequisites
- ARM GCC toolchain (arm-none-eabi-gcc)
- Make
- MD407 board connection for `make console`

## Build
Set `GCC_PATH` to the directory containing the ARM toolchain binaries (note the trailing slash):

```sh
export GCC_PATH=/path/to/gcc-arm-none-eabi/bin/
```

Build the ELF and S19 outputs:

```sh
make
```

Outputs:
- `build/<app-name>.elf`
- `build/<app-name>.s19`

`<app-name>` defaults to the folder name, with spaces replaced by `-`.

## Run / Console
To send the S19 to the MD407 board and open the console:

```sh
make console
```

This uses `bin/console` with the built S19 file.

## Console help (from `print_helper`)

When the app starts, it prints:

```text
=== Tone Generator Console ===
Enter a command and press Enter.

Main commands:
  v | volume            Set volume (0-20)
  s | mute              Mute tone output
  r | unmute            Resume tone output
  b | bg | background   Adjust background load
  d | deadline          Enable deadline mode
  e | nd | nodeadline   Disable deadline mode
  h | help              Show this help

In volume mode:
  Enter a number 0-20, then Enter
  e               Return to main menu

In background mode:
  +               Increase background load
  -               Decrease background load
  e               Return to main menu
```

Notes:
- Commands are case-insensitive.
- Pressing Enter on an empty command prints the help menu again.
- Invalid commands print an error and then the help menu.

## Notes
- The CAN receive handler prints `msg.buff` as a C string; if payloads are not null-terminated, output may include extra bytes.
- The project is configured for STM32F40/41 (`-D STM32F40_41xxx`) and uses hard-float ABI.

## Clean

```sh
make clean
```
