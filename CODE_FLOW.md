# Code Flow Note

This note describes the current firmware behavior at a system level.

It is intentionally written without code snippets so it can be used as an operational reference when checking the controller on hardware.

## Scope

- Main firmware file: `Core/Src/main.c`
- Current default build: `AUTO`
- Alternate modes in the source:
  - `MANUAL`
  - `TEST`

## Startup Sequence

At power-up the firmware performs these steps:

1. Initializes HAL, clock, GPIO, and SPI
2. Reads all inputs once
3. Primes the debounce state from those first input values
4. Writes a safe all-off frame to the shift-register outputs
5. Enters the cyclic control loop

The main loop then repeats continuously:

1. Read raw inputs
2. Debounce and normalize them into logic states
3. Run the selected control mode
4. Build the relay and indicator output image
5. Shift outputs to the register chain
6. Update the two board LEDs
7. Delay for the loop period

## Input Meaning

### Selector

- `SEL_P1` and `SEL_P2` are active low electrically
- Decode is:
  - normalized `00` = `OFF`
  - normalized `10` = `PUMP 1`
  - normalized `01` = `PUMP 2`
  - normalized `11` = `INVALID`

### Pressure

- Pressure inputs are active low electrically
- After normalization, `pressure active` means low pressure exists on the engine
- Pressure is treated as a demand signal

### RPM

- RPM inputs are active low electrically
- After normalization, `rpm active` means the engine can support itself using its own pump
- RPM is not used as proof that the standby pump itself is rotating

### AC Ready

- `AC1_IN` and `AC2_IN` are active low electrically
- After normalization, `AC active` means the corresponding remote pump is available for module control
- AC is treated as a ready / permission input, not as a run feedback

### ACK / Lamp Test

- `ACK_LT1` on `PH0` is active low
- Short press = `ACK`
- Long press = `ACK + lamp test`

Current implementation performs ACK on the press edge. If the press is held long enough, lamp test is also enabled.

### Pump Feedback

- `I5` is Pump 1 feedback and is active low
- `I8` is Pump 2 feedback and is active low
- pump feedback is used for the `3 s` feedback-timeout alarm
- each `Pump ON` indicator follows the corresponding pump command

## Operating Modes

## AUTO Mode

This is the current default mode.

### Pump selection

- The selector chooses which pump path is allowed to operate
- `OFF` means no pump should run
- `PUMP 1` allows only pump 1
- `PUMP 2` allows only pump 2
- `INVALID` is treated as a fault condition

### Run request

For the selected pump, a run request exists when either of these is true:

- pressure is low
- RPM is not active

This means the standby pump may be requested either because pressure is low, or because the engine still cannot support itself.

### Run permission

Even when there is a run request, the selected pump is only allowed to run if its AC ready input is active.

So:

- demand present + AC ready = pump may run
- demand present + AC not ready = pump stays off

### Stop condition

The selected pump stops only when both of these are true:

- pressure low is no longer active
- RPM is active

If RPM becomes active while pressure is still low, the standby pump continues to run.

### Selector change during operation

If the selector changes away from the currently active pump while the controller is running:

- the running pump is stopped
- the state returns to `OFF`
- there is no automatic transfer

The operator must deliberately choose the desired pump.

## MANUAL Mode

This mode uses the selector as a direct output command.

- selector `OFF` = both pumps off
- selector `PUMP 1` = Pump 1 forced on
- selector `PUMP 2` = Pump 2 forced on
- selector `INVALID` = fault

In manual mode:

- pressure does not decide start or stop
- RPM does not decide start or stop
- AC ready does not gate the output
- feedback is ignored for start, stop, and alarm
- standby alarm stays off

## Output Test Mode

This mode is for bench checking outputs and indicator mapping, not for control.

### Relay sequence

The relay outputs cycle in this order:

1. `Pump 1`
2. `Pump 2`
3. break

Each step uses the configured output-test repeat time.

### Indicator behavior

If no test input is active:

- panel LEDs step through the indicator sequence one at a time

If `ACK_LT1` is held active during output-test mode:

- all relay outputs are forced off
- panel LEDs switch to a grouped pattern test
- stage 1 shows one LED at a time
- stage 2 shows two LEDs at a time as groups
- stage 3 shows three LEDs at a time as groups
- this continues until all nine panel LEDs are on together
- each stage repeats two full passes before moving to the next stage

If any of the test inputs `I4..I7` is active:

- the sequence is overridden
- a single corresponding indicator is shown

Output test mode is only for mapping verification. It does not represent normal controller behavior.

## Alarm Behavior

There are currently two different alarm concepts in the firmware:

### General latched alarm

This is the main latched alarm state used for system health.

It can be caused by:

- invalid selector in `AUTO`
- in `AUTO`, demand exists while the selected pump is not ready
- feedback-timeout fault in `AUTO`

This alarm affects:

- internal alarm state
- ACK handling

### Pressure-timeout alarm

This is the specific alarm that drives:

- `Standby alarm` indicator

It is a subset of the general alarm behavior.

## Feedback-Timeout Logic

This is the current `AUTO` rule for the latched `Standby alarm`:

1. A pump starts running
2. The firmware begins counting from the pump run start time
3. If matching pump feedback is still missing after `3 s`, a feedback-timeout fault occurs
4. The pump is turned off
5. The alarm is latched
6. `Standby alarm` blinks on the module front panel

This timeout is checked from pump command start, not from a separate background timer.

If feedback appears before the `3 s` expires:

- the feedback-timeout fault does not occur

If feedback appears after the fault has latched:

- the feedback-timeout latch clears automatically
- `Standby alarm` turns off
- the controller can return to normal operation without `ACK`

If the pump is not allowed to start because AC ready is absent:

- the low-pressure indicator still works normally
- the pump stays off
- no extra relay output turns on from that condition alone

## ACK Behavior

ACK clears the latched alarm state.

For the `AUTO` feedback-timeout case specifically:

- ACK still clears the latched timeout alarm manually
- `Standby alarm` turns off
- the module returns to normal operation again

After ACK, if demand still exists and the selected pump is ready, the system is allowed to start the pump again as a fresh cycle.

This also means the `3 s` timeout starts again from the new pump start, not from the old failed attempt.

## Indicator Behavior

### Panel indicators

- `IND9 System ready`
  - on whenever the module is powered and the firmware is running

- `IND1 Pump 1 ready`
  - follows pump 1 AC ready input

- `IND10 Pump 1 ON`
  - on when pump 1 command is active

- `IND2 Pump 1 standby`
  - on when selector is on pump 1

- `IND11 Pump 2 ready`
  - follows pump 2 AC ready input

- `IND3 Pump 2 ON`
  - on when pump 2 command is active

- `IND12 Pump 2 standby`
  - on when selector is on pump 2

- `IND4 Pressure low`
  - shows active demand
  - reflects either pump pressure input

- `IND13 Standby alarm`
  - in `AUTO`, follows the latched feedback-timeout fault
  - in `MANUAL`, stays off
  - blinks using the common blink timing

### Lamp test

Lamp test does not force all indicators on together.

Instead it cycles these 3-indicator groups repeatedly:

- `IND9 + IND1 + IND10`
- `IND2 + IND11 + IND3`
- `IND12 + IND4 + IND13`

This grouped behavior is used in all modes once the long-press threshold is reached.

## Board LED Behavior

- `SYS_LED1`
  - forced off in normal operation

- `SYS_LED2`
  - heartbeat blink to show MCU is running normally

In output test mode, the board LEDs stay under test-mode control rather than the normal heartbeat behavior.

## Output Mapping Summary

### Relay outputs

The relay board is reversed relative to the `74HC595` bit order.

Current effective mapping:

- relay bit 7 = `Q1 = Pump 1`
- relay bit 6 = `Q2 = Pump 2`
- relay bit 5 = `Q3 = unused`

### LED outputs

LED banks are straight mapped:

- LED byte 1 = `IND1..IND8`
- LED byte 2 = `IND9..IND16`

## Current Practical Summary

The current firmware behaves as a selector-driven standby pump controller with these main rules:

- in `AUTO`, demand comes from pressure low or RPM inactive
- in `MANUAL`, selector directly forces the pump output
- in `MANUAL`, feedback is ignored completely
- a selected pump can only run if its AC ready input is active
- the pump stops when pressure low clears and RPM is active
- invalid selector and not-ready demand latch the general alarm
- `Standby alarm` is reserved specifically for the `3 s` no-feedback failure after a pump has been commanded on
- ACK returns the controller to normal operation again
