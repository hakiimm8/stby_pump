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
- A fault latches the alarm and activates the `Failure AMS` output
- The operator must acknowledge the alarm and manually select another pump if needed

The current source is configured for dual pump mode in [`Core/Src/main.c`](Core/Src/main.c).

## Control Sequence

For the selected pump:

1. Low pressure creates demand
2. Pump output turns on
3. AC feedback must appear within `T_AC_TIMEOUT_MS`
4. RPM feedback must appear within `T_RPM_TIMEOUT_MS`
5. Pressure must recover within `T_PRESSURE_RECOVER_MS`
6. If pressure recovers, the pump remains on until demand clears
7. If AC or RPM is lost while running, the pump stops and the alarm latches

## Alarm / ACK / Lamp Test

- Alarm is latched
- `ACK_LT` short press clears the alarm latch
- `ACK_LT` long press activates lamp test
- Lamp test affects display LEDs only
- Alarm indication blinks at 1 Hz

## Hardware Summary

MCU:

- STM32F405RGTx

Shift register chain:

- `MCU -> U1 -> U3 -> U6`
- `U1` = relay outputs
- `U3` = LED bank 1
- `U6` = LED bank 2

Relay/DC mapping:

- Relay bit 0 = `Failure AMS`
- Relay bit 1 = `Pump 1 command`
- Relay bit 2 = `Pump 2 command`

Inputs:

- `I4` = Pressure switch pump 1
- `I5` = RPM switch pump 1
- `I6` = Pressure switch pump 2
- `I7` = RPM switch pump 2
- `AC1_IN` = AC feedback pump 1
- `AC2_IN` = AC feedback pump 2
- `ACK_LT` = `PH0`
- `SEL_P1` = `PA10`
- `SEL_P2` = `PA11`
- `SW_COMMON` = `PH1`

Switch wiring:

- `PH1` drives the common source for selector and ACK wiring
- `PH0`, `PA10`, and `PA11` use pull-down configuration
- Selector and ACK logic are active high

## GPIO Notes

Important generated GPIO startup states:

- `SW_COMMON` (`PH1`) starts high
- `SR_LATCH` (`PA4`) starts low
- `SR_OE` (`PA6`) starts high
- `ACK_LT`, `SEL_P1`, and `SEL_P2` are configured with pull-downs

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
- ACK versus lamp test timing
- Shift register byte order on the real PCB
- Relay and LED bit mapping on hardware
- AC timeout, RPM timeout, and pressure recovery timeout behavior

## Bench Checklist

- Verify `OFF / PUMP 1 / PUMP 2 / INVALID` selector decoding
- Verify short press `ACK_LT` clears the latched alarm
- Verify long press `ACK_LT` lights the display LEDs only
- Verify `SR_OE` prevents relay glitching during shift register writes
- Verify only one pump output can be active at a time
- Verify startup with demand already present
- Verify alarm latch and `Failure AMS` output behavior

