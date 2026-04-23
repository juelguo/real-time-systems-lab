# real-time-system

Embedded TinyTimber application for the MD407 (STM32F4, Cortex-M4) board using CAN and SCI (serial).

## What this project does
- Initializes CAN and SCI peripherals
- Runs in `conductor` or `musician` role
- In conductor role: keyboard commands control local playback and broadcast CAN control commands
- In musician role: playback/settings are applied only when CAN messages are received

## Repository layout
- `src/` application sources (`App.c`, `objects.c`)
- `inc/` application headers (`App.h`)
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

## How To Control The Software (SCI Helper)
When the app starts, it prints a helper menu in the serial console.
Type a command, then press `Enter`.

### Playback commands
- `p` or `play`: play the melody
- `q` or `stop`: stop the melody

### Role commands
- `c` or `conductor`: switch to conductor role
- `u` or `musician`: switch to musician role

In musician role, keyboard commands are sent as CAN control messages (for loopback testing), but are not applied directly unless received over CAN.

### Network commands
- `node <id>`: set this board's node id (`1-14`) and start discovery
- `claim`: claim conductor role
- `m`, `member`, or `membership`: show all boards membership status
- `R`: reset key and tempo to defaults
- `I`: show local node/rank/role info
- `T`: toggle local audio mute
- `P`: toggle periodic tempo/MUTED printing

### Settings commands
- `t` or `tempo`: enter tempo-setting mode (valid range `60-240` BPM)
- `k` or `key`: enter key-setting mode (valid range `-5` to `+5`)
- `v` or `volume`: enter volume-setting mode (`0-20`)

### Hardware commands
- `s` or `mute`: mute tone output
- `r` or `unmute`: resume tone output
- `h` or `help`: print the helper menu again

### Settings mode behavior
After `tempo`, `key`, or `volume`:
- Type a number and press `Enter` to apply
- Type `e` to cancel and return to main control mode

Input behavior and limits:
- Commands are case-sensitive
- Uppercase commands such as `P`, `R`, `I`, and `T` are distinct from lowercase commands like `p`, `r`, `i`, and `t`
- `tempo` values are clamped to `60-240`
- `key` values are clamped to `-5..5`

## Notes
- CAN message ID `1` is used for player control broadcasts.
- CAN messages are logged in the console as both `CAN TX` and `CAN RX` (msgId/nodeId/len/cmd/value).
- The project is configured for STM32F40/41 (`-D STM32F40_41xxx`) and uses hard-float ABI.

## Clean

```sh
make clean
```
