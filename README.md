# STM32 Standby Pump Controller

STM32F405RGTx firmware for a manual selector-based standby pump controller built with STM32CubeIDE and HAL.

## Overview

This project controls an engine support pump system with two supported operating modes:

- Single pump mode
- Dual pump mode

In dual pump mode:

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

1. A pump run request exists when `pressure_low` is active or `rpm` is inactive
2. The selected pump may run only if its `ACx_IN` ready input is active
3. If a run request exists while the selected pump is not ready, alarm latches and the pump stays off
4. The standby pump stops only when pressure low is inactive and RPM is active
5. `System ready` means the module is powered and running
6. If a pump is running and pressure stays low for `10 s`, the controller stops the pump, latches alarm, and turns on `Failure AMS` until `ACK`
7. `IND13 Standby alarm` follows the same latched `10 s` pressure-timeout alarm as `Failure AMS`

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

- Relay bit 7 = `Failure AMS`
- Relay bit 6 = `Pump 1 command`
- Relay bit 5 = `Pump 2 command`
- Relay bits are reversed at the board interface because the relay `74HC595` / `ULN2803A` path lands on `Q8..Q1`

Inputs:

- `I4` = Pressure switch pump 1
- `I5` = RPM switch pump 1
- `I6` = Pressure switch pump 2
- `I7` = RPM switch pump 2
- `PB7` = `CONTACTOR_FB`
- `PH0` = `ACK_LT1`
- `AC1_IN` = pump 1 ready / remote available
- `AC2_IN` = pump 2 ready / remote available
- `SEL_P1` = `PA10`
- `SEL_P2` = `PA11`

Switch wiring:

- `PA10` and `PA11` use pull-up configuration and are active low
- `ACK_LT1` on `PH0` uses pull-up configuration and is active low
- `CONTACTOR_FB` on `PB7` is active low and is expected to use external pull-up hardware
- `AC1_IN` / `AC2_IN` are active low and are expected to use external pull-up hardware

## GPIO Notes

Important generated GPIO startup states:

- `SR_LATCH` (`PA4`) starts low
- `SR_OE` (`PA6`) starts high
- `SEL_P1` and `SEL_P2` are configured with pull-ups and treated as active-low inputs
- `ACK_LT1` on `PH0` is configured with pull-up and treated as an active-low input
- `CONTACTOR_FB` on `PB7` is treated as an active-low input

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

Bypass mode indicator meaning:

- `Pump 1 standby` = pump 1 ready and not currently running
- `Pump 2 standby` = pump 2 ready and not currently running

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
- contactor feedback behavior on `PB7`
- Shift register byte order on the real PCB
- Relay and LED bit mapping on hardware
- `AC1_IN` / `AC2_IN` ready behavior
- pressure-timeout `Failure AMS` behavior
- run/stop behavior from pressure and RPM inputs

## Bench Checklist

- Verify `OFF / PUMP 1 / PUMP 2 / INVALID` selector decoding
- Verify short press on `ACK_LT1` clears the latched alarm
- Verify long press on `ACK_LT1` clears alarm and runs the grouped lamp test
- Verify `CONTACTOR_FB` on `PB7` lights `Pump ON` indicators only when the commanded contactor closes
- Verify `SR_OE` prevents relay glitching during shift register writes
- Verify only one pump output can be active at a time
- Verify pump starts on low pressure or inactive RPM only when the selected pump is ready
- Verify pump stops only when pressure low clears and RPM is active
- Verify alarm latch behavior for not-ready / invalid selector faults
- Verify `Failure AMS` and `IND13 Standby alarm` only turn on after a `10 s` low-pressure timeout while the pump is running
