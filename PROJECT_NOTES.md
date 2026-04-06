# Project Notes

Working notes for the STM32 standby pump controller project.

This file is intended to capture project knowledge that is easy to lose between sessions:

- hardware assumptions
- IO mapping decisions
- logic conventions
- known caveats
- validation still required

## Project Summary

- MCU: `STM32F405RGTx`
- IDE: `STM32CubeIDE 1.19.0`
- Framework: STM32 HAL / CubeMX generated project
- Main application file: `Core/Src/main.c`
- Current application mode in code: `dual pump mode`

## Control Philosophy

- Manual selector-based pump controller
- Modes supported by code:
  - single pump mode
  - dual pump mode
- Current build is configured for `dual pump mode`
- In dual mode:
  - selector chooses `OFF`, `PUMP 1`, or `PUMP 2`
  - only one pump may run at a time
  - no automatic transfer to the other pump
  - operator must acknowledge fault and manually select the other pump if needed

## Input Logic

### Active low inputs

These are currently treated as active low in `Core/Src/main.c`:

- `SEL_P1`
- `SEL_P2`
- `ACK_LT`
- `ACK_LT1`
- `AC1_IN`
- `AC2_IN`
- `IN Pressure switch pump 1`
- `IN RPM switch pump 1`
- `IN Pressure switch pump 2`
- `IN RPM switch pump 2`

Reason:

- ACK, pressure, and RPM inputs use pull-up resistors
- active state is the low state

Current polarity defines:

- `PRESSURE_ACTIVE_LEVEL = 0U`
- `RPM_ACTIVE_LEVEL = 0U`
- `AC_ACTIVE_LEVEL = 0U`
- `SELECTOR_ACTIVE_LEVEL = 0U`
- `ACK_LT_ACTIVE_LEVEL = 0U`
- `ACK_LT1_ACTIVE_LEVEL = 0U`

## Selector Wiring

- `PA10` = `SEL_P1`
- `PA11` = `SEL_P2`
- selector contacts now pull the input to `GND`
- selector inputs use internal pull-up and are active low

Expected selector decode:

- normalized `SEL_P1=0`, `SEL_P2=0` -> `OFF`
- normalized `SEL_P1=1`, `SEL_P2=0` -> `PUMP 1`
- normalized `SEL_P1=0`, `SEL_P2=1` -> `PUMP 2`
- normalized `SEL_P1=1`, `SEL_P2=1` -> `INVALID`

GPIO assumptions:

- `PA10`, `PA11` use pull-up configuration

## ACK Inputs

- `ACK_LT` on `PB7` is active low and uses external pull-up hardware
- `ACK_LT1` on `PH0` is active low and uses internal pull-up
- both inputs behave the same in firmware:
  - short press = ACK
  - long press = ACK + lamp test

## DC / Pump IO Mapping

### New DC order

- `Q1` = Output Failure AMS
- `Q2` = Output pump 1
- `Q3` = Output pump 2
- `I4` = IN Pressure switch pump 1
- `I5` = IN RPM switch pump 1
- `I6` = IN Pressure switch pump 2
- `I7` = IN RPM switch pump 2

### MCU direct input pins

- `I4` -> `PB3`
- `I5` -> `PB4`
- `I6` -> `PB5`
- `I7` -> `PB6`
- `ACK_LT` -> `PB7`
- `ACK_LT1` -> `PH0`
- `AC1_IN` -> `PB8`
- `AC2_IN` -> `PB9`

### Relay outputs

Relay outputs are not direct MCU pins.

They are driven through:

- SPI -> `74HC595`
- `74HC595` -> `ULN2803A`
- `ULN2803A` -> relay/load nets

MCU control pins for the shift register chain:

- `PA5` = `SPI1_SCK`
- `PA7` = `SPI1_MOSI`
- `PA4` = `SR_LATCH`
- `PA6` = `SR_OE`

## Shift Register Notes

Physical chain order from MCU:

- `MCU -> U1 -> U3 -> U6`

Intended function:

- `U1` = relay byte
- `U3` = LED byte 1
- `U6` = LED byte 2

Write sequence required by hardware:

1. `OE HIGH`
2. SPI transmit 3 bytes
3. latch pulse
4. `OE LOW`

That sequence is implemented in `SR_Write24()`.

## Important Hardware Mapping Decision

### Relay/DC board is reversed

Based on the relay board schematic review:

- `74HC595 QA..QH` do not land on `Q1..Q8` in ascending order
- relay outputs are effectively reversed through the board wiring

Effective mapping for the relay byte:

- `bit 7` -> `Q1`
- `bit 6` -> `Q2`
- `bit 5` -> `Q3`
- `bit 4` -> `Q4`
- `bit 3` -> `Q5`
- `bit 2` -> `Q6`
- `bit 1` -> `Q7`
- `bit 0` -> `Q8`

Current code was updated to match this for the used outputs:

- `Failure AMS` -> `bit 7`
- `Pump 1` -> `bit 6`
- `Pump 2` -> `bit 5`

### LED boards are straight

The LED banks were reviewed separately and are not reversed.

For LED bank 1:

- `bit 0` -> `IND1`
- `bit 1` -> `IND2`
- `bit 2` -> `IND3`
- `bit 3` -> `IND4`

For LED bank 2:

- `bit 0` -> `IND9`
- `bit 1` -> `IND10`
- `bit 2` -> `IND11`
- `bit 3` -> `IND12`
- `bit 4` -> `IND13`

## LED Meaning

- `IND9` = System ready
- `IND1` = Pump 1 ready
- `IND10` = Pump 1 ON
- `IND2` = Pump 1 standby
- `IND11` = Pump 2 ready
- `IND3` = Pump 2 ON
- `IND12` = Pump 2 standby
- `IND4` = Pressure low
- `IND13` = Standby alarm

Current logic meaning:

- `System ready` = module powered and firmware running
- `Pump ready` = `ACx_IN` ready input is active
- `Pump ON` = pump command active
- `Pump standby` = in normal mode, opposite pump selected; in bypass mode, pump ready and not currently on
- `Pressure low` = demand active
- `Standby alarm` = same latched `10 s` pressure-timeout alarm as `Failure AMS`, blinking on the module indicator
- `Failure AMS` = live control uses it only for the `10 s` pressure-timeout fault; output test still drives it in sequence

## State Machine Notes

Current run/stop rules for the selected pump:

1. run request exists when `pressure_low` is active or `rpm` is inactive
2. pump may run only if `ACx_IN` for that pump is active
3. if run is requested while the selected pump is not ready, alarm latches and the pump stays off
4. pump stops only when `pressure_low` is inactive and `rpm` is active
5. if a running pump sees `pressure_low` stay active for `10 s`, the pump stops, alarm latches, and `Failure AMS` turns on
6. ACK clears the latched alarm and returns the module to normal operation again

Fault causes include:

- run requested while selected pump is not ready
- invalid selector state

ACK/Lamp test behavior:

- short press on either ACK input = ACK
- long press on either ACK input = ACK + lamp test

## Known Build Status

- project builds successfully with STM32CubeIDE bundled GNU toolchain
- no blocking build warnings were observed in the latest verified build

## Validation Still Required

Bench validation is still required for:

- selector decode on real hardware
- ACK short press behavior
- lamp test long press behavior
- real shift-register relay mapping
- real LED mapping
- `AC1_IN` / `AC2_IN` ready behavior
- run and stop behavior from pressure / RPM inputs

## Datasheets Provided

Reference files shared during review:

- `74HC595.pdf`
- `ULN2803A.pdf`
- `stm32f407vgt6 datasheet.pdf`

These were used as reference for pin/function reasoning, but the board schematic remains the source of truth for final signal mapping.
