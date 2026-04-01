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

### Active high inputs

These are currently treated as active high in `Core/Src/main.c`:

- `ACK_LT`
- `SEL_P1`
- `SEL_P2`
- `AC1_IN`
- `AC2_IN`

### Active low inputs

These are currently treated as active low in `Core/Src/main.c`:

- `IN Pressure switch pump 1`
- `IN RPM switch pump 1`
- `IN Pressure switch pump 2`
- `IN RPM switch pump 2`

Reason:

- pressure and RPM inputs use pull-up resistors
- active state is the low state

Current polarity defines:

- `PRESSURE_ACTIVE_LEVEL = 0U`
- `RPM_ACTIVE_LEVEL = 0U`
- `AC_ACTIVE_LEVEL = 1U`
- `SELECTOR_ACTIVE_LEVEL = 1U`
- `ACK_LT_ACTIVE_LEVEL = 1U`

## Selector Wiring

- `PH1` is used as switch common source
- `PH0` = `ACK_LT`
- `PA10` = `SEL_P1`
- `PA11` = `SEL_P2`

Expected selector decode:

- `SEL_P1=0`, `SEL_P2=0` -> `OFF`
- `SEL_P1=1`, `SEL_P2=0` -> `PUMP 1`
- `SEL_P1=0`, `SEL_P2=1` -> `PUMP 2`
- `SEL_P1=1`, `SEL_P2=1` -> `INVALID`

GPIO assumptions:

- `PH1` starts high
- `PH0`, `PA10`, `PA11` use pull-down configuration

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

- `System ready` = always on
- `Pump ready` = no alarm latched
- `Pump ON` = pump command active
- `Pump standby` = opposite pump selected in dual mode
- `Pressure low` = demand active
- `Standby alarm` = latched alarm blinking

## State Machine Notes

Pump start sequence for selected pump:

1. demand detected
2. command pump ON
3. wait AC feedback
4. wait RPM feedback
5. wait pressure recovery
6. continue running until demand disappears
7. stop and latch alarm on failure

Fault causes include:

- AC timeout
- RPM timeout
- AC lost while running
- RPM lost while running
- pressure recovery timeout
- invalid selector state

ACK/Lamp test behavior:

- short press = ACK
- long press = lamp test

## Known Build Status

- project builds successfully with STM32CubeIDE bundled GNU toolchain
- one non-blocking warning remains:
  - `RunSinglePumpLogic()` unused while dual mode is selected

## Validation Still Required

Bench validation is still required for:

- selector decode on real hardware
- ACK short press behavior
- lamp test long press behavior
- real shift-register relay mapping
- real LED mapping
- AC timeout
- RPM timeout
- pressure recovery timeout
- loss of AC or RPM while running

## Datasheets Provided

Reference files shared during review:

- `74HC595.pdf`
- `ULN2803A.pdf`
- `stm32f407vgt6 datasheet.pdf`

These were used as reference for pin/function reasoning, but the board schematic remains the source of truth for final signal mapping.

