# STM32 Standby Pump Controller

STM32F405RGTx firmware for a manual selector-based standby pump controller built with STM32CubeIDE and HAL.

## Overview

This project supports three control behaviors in the source:

- `AUTO`
- `MANUAL`
- `TEST`

In dual-pump builds:

- The operator selects `OFF`, `PUMP 1`, or `PUMP 2`
- Only one pump may run at a time
- `AUTO` tries the selected pump first, then automatically tries the other pump if feedback fails
- Alarm latches only after all available pumps have failed feedback
- ACK resets the automatic controller and lets it start fresh

The current source is configured for dual pump mode in [`Core/Src/main.c`](Core/Src/main.c).

## Control Logic

Current control logic in `Core/Src/main.c` uses these meanings:

- `AC1_IN` / `AC2_IN` = selected remote pump is available for module control
- `Pressure` input = low-pressure indication on the engine
- `RPM` input = engine can sustain itself without the standby pump

For the selected pump:

In `AUTO` mode:

1. A run request exists when the selected pump sees `pressure_low` active or `rpm` inactive
2. The selected pump is the primary pump; the other pump is the secondary pump
3. On demand, the controller tries the primary pump first if it is ready
4. If the primary has no feedback after `3 s`, the controller tries the secondary pump if it is ready
5. If all available pumps fail feedback, both outputs turn off and alarm latches
6. `ACx_IN` not ready is never a fault; that pump is simply skipped
7. Selector `OFF` or `INVALID` stops the automatic controller without alarm

In `MANUAL` mode:

1. Selecting `PUMP 1` forces Pump 1 on
2. Selecting `PUMP 2` forces Pump 2 on
3. Selecting `OFF` turns the outputs off
4. Pressure, RPM, AC, and feedback do not gate the output command
5. Manual mode is open loop and does not generate standby alarm

Common behavior:

1. `System ready` means the module is powered and running
2. In `AUTO`, `IND13 Standby alarm` follows the latched `3 s` no-feedback alarm
3. `Pump 1 ON` and `Pump 2 ON` always follow live feedback, even if the module output is off

## Alarm / ACK / Lamp Test

- Alarm is latched
- `ACK_LT1` short press clears the alarm latch
- `ACK_LT1` long press activates lamp test and also clears the alarm latch
- Lamp test affects display LEDs only
- Alarm indication blinks at 1 Hz
- Current implementation performs ACK on the press edge, so long press = ACK + lamp test

## Hardware Summary

MCU:

- STM32F405RGTx

Shift register chain:

- `MCU -> U1 -> U3 -> U6`
- `U1` = relay outputs
- `U3` = LED bank 1
- `U6` = LED bank 2

Relay/DC mapping:

- Relay bit 7 = `Pump 1 command`
- Relay bit 6 = `Pump 2 command`
- Relay bit 5 = unused / reserved
- Relay bits are reversed at the board interface because the relay `74HC595` / `ULN2803A` path lands on `Q8..Q1`

Inputs:

- `I3` = Pressure switch pump 1
- `I4` = RPM switch pump 1
- `I5` = Feedback pump 1
- `I6` = Pressure switch pump 2
- `I7` = RPM switch pump 2
- `I8` = Feedback pump 2
- `PH0` = `ACK_LT1`
- `AC1_IN` = pump 1 ready / remote available
- `AC2_IN` = pump 2 ready / remote available
- `SEL_P1` = `PA10`
- `SEL_P2` = `PA11`

Switch wiring:

- `PA10` and `PA11` use pull-up configuration and are active low
- `ACK_LT1` on `PH0` uses pull-up configuration and is active low
- `I3`, `I5`, `I6`, and `I8` are active low and are expected to use external pull-up hardware
- `I4` and `I7` use external pull-up hardware and are treated as active-high RPM inputs in firmware
- `AC1_IN` / `AC2_IN` are active low and are expected to use external pull-up hardware

## GPIO Notes

Important generated GPIO startup states:

- `SR_LATCH` (`PA4`) starts low
- `SR_OE` (`PA6`) starts high
- `SEL_P1` and `SEL_P2` are configured with pull-ups and treated as active-low inputs
- `ACK_LT1` on `PH0` is configured with pull-up and treated as an active-low input
- `I3`, `I5`, `I6`, and `I8` are treated as active-low inputs
- `I4` and `I7` are treated as active-high RPM inputs with `2 s` debounce in firmware

## LED Mapping

`U3 / led_byte_1`

- Bit 0 = `IND1` Pump 1 ready
- Bit 1 = `IND2` Pump 1 standby
- Bit 2 = `IND3` Pump 2 on
- Bit 3 = `IND4` Pressure low

`U6 / led_byte_2`

- Bit 0 = `IND9` System ready
- Bit 1 = `IND10` Pump 1 on
- Bit 2 = `IND11` Pump 2 ready
- Bit 3 = `IND12` Pump 2 standby
- Bit 4 = `IND13` Standby alarm

Mode indicator meaning:

- `Pump 1 standby` = selector is on Pump 1
- `Pump 2 standby` = selector is on Pump 2

## Build / Import

This repository contains the STM32CubeIDE project files and can be imported directly.

1. Download or clone the repository
2. Open STM32CubeIDE
3. Use `File > Open Projects from File System`
4. Select the extracted project folder
5. Build the project

Notes:

- `Debug/` build artifacts are intentionally not tracked
- STM32CubeIDE will regenerate output files on build
- The `.ioc` file is included, so CubeMX settings can be reopened and regenerated

## Repository Contents

- `Core/` application and generated Cube source
- `Drivers/` STM32 HAL and CMSIS
- `stby_pump.ioc` CubeMX project file
- `.project`, `.cproject`, `.mxproject` CubeIDE project files
- `STM32F405RGTX_FLASH.ld`, `STM32F405RGTX_RAM.ld` linker scripts

## Validation Status

The project currently builds successfully with STM32CubeIDE 1.19.0 and the bundled GNU Arm toolchain.

Hardware validation is still required for:

- Selector decode
- ACK versus lamp test timing on `ACK_LT1`
- pump feedback behavior on `I5` / `I8`
- Shift register byte order on the real PCB
- Relay and LED bit mapping on hardware
- `AC1_IN` / `AC2_IN` ready behavior
- no-feedback standby alarm behavior
- run/stop behavior from pressure and RPM inputs

## Bench Checklist

- Verify `OFF / PUMP 1 / PUMP 2 / INVALID` selector decoding
- Verify short press on `ACK_LT1` clears the latched alarm
- Verify long press on `ACK_LT1` clears alarm and runs the grouped lamp test
- Verify `Pump 1 ON` follows Pump 1 feedback, including local running
- Verify `Pump 2 ON` follows Pump 2 feedback, including local running
- Verify `SR_OE` prevents relay glitching during shift register writes
- Verify only one pump output can be active at a time in `AUTO`
- Verify `AUTO` tries the selected pump first, then fails over to the other ready pump on feedback loss/timeout
- Verify `AUTO` stays off without alarm when demand exists but neither pump is ready
- Verify `MANUAL` follows selector directly regardless of pressure / RPM / AC / feedback
- Verify `IND13 Standby alarm` only turns on after all available pumps have failed feedback in `AUTO`
- Verify ACK from `AUTO` alarm resets the controller and starts a fresh cycle
