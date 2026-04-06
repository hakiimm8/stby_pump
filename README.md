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
- There is no automatic transfer
- A fault latches the panel alarm
- The operator must acknowledge the alarm and manually select another pump if needed

The current source is configured for dual pump mode in [`Core/Src/main.c`](Core/Src/main.c).

## Control Logic

Current control logic in `Core/Src/main.c` uses these meanings:

- `AC1_IN` / `AC2_IN` = selected remote pump is available for module control
- `Pressure` input = low-pressure indication on the engine
- `RPM` input = engine can sustain itself without the standby pump

For the selected pump:

In `AUTO` mode for the selected pump:

1. A pump run request exists when `pressure_low` is active or `rpm` is inactive
2. The selected pump may run only if its `ACx_IN` ready input is active
3. If a run request exists while the selected pump is not ready, alarm latches and the pump stays off
4. The standby pump stops only when pressure low is inactive and RPM is active

In `MANUAL` mode:

1. Selecting `PUMP 1` forces Pump 1 on
2. Selecting `PUMP 2` forces Pump 2 on
3. Selecting `OFF` turns the outputs off
4. Pressure, RPM, AC, and feedback do not gate the output command
5. Manual mode is open loop and does not generate standby alarm

Common behavior:

1. `System ready` means the module is powered and running
2. In `AUTO`, if a commanded pump still has no matching feedback after `3 s`, the controller stops the pump and latches alarm
3. `IND13 Standby alarm` follows that latched `3 s` no-feedback alarm in `AUTO`

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
- `I3..I8` are active low and are expected to use external pull-up hardware
- `AC1_IN` / `AC2_IN` are active low and are expected to use external pull-up hardware

## GPIO Notes

Important generated GPIO startup states:

- `SR_LATCH` (`PA4`) starts low
- `SR_OE` (`PA6`) starts high
- `SEL_P1` and `SEL_P2` are configured with pull-ups and treated as active-low inputs
- `ACK_LT1` on `PH0` is configured with pull-up and treated as an active-low input
- `I3..I8` are treated as active-low inputs

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
- Verify `AUTO` starts on low pressure or inactive RPM only when the selected pump is ready
- Verify `AUTO` stops only when pressure low clears and RPM is active
- Verify `MANUAL` follows selector directly regardless of pressure / RPM / AC / feedback
- Verify alarm latch behavior for not-ready / invalid selector faults in `AUTO`
- Verify `IND13 Standby alarm` only turns on after `3 s` of missing feedback while the pump is commanded on
