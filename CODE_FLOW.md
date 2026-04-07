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
- each `Pump ON` indicator follows the corresponding pump feedback input

## Operating Modes

## AUTO Mode

This is the current default mode.

### Pump selection

- The selector chooses the primary pump in `AUTO`
- `OFF` means no pump should run
- `PUMP 1` means Pump 1 is primary and Pump 2 is secondary
- `PUMP 2` means Pump 2 is primary and Pump 1 is secondary
- `INVALID` is treated the same as `OFF`

### Run request

For the selected pump, a run request exists when either of these is true:

- pressure is low
- RPM is not active

This means the standby pump may be requested either because pressure is low, or because the engine still cannot support itself.

### Failover behavior

- On demand, `AUTO` tries the selected primary pump first if it is ready
- If the primary has no feedback after `3 s`, `AUTO` tries the secondary pump if it is ready
- If the secondary also fails feedback, the controller enters `ALARM`
- `AC` not ready is never a fault; that pump is just skipped
- If neither pump is ready, the controller stays `OFF` with no alarm
- If selector changes while `AUTO` is trying or running a pump, the controller returns to `OFF`
- If demand disappears while `AUTO` is trying or running a pump, the controller returns to `OFF`

## MANUAL Mode

This mode uses the selector as a direct output command.

- selector `OFF` = both pumps off
- selector `PUMP 1` = Pump 1 forced on
- selector `PUMP 2` = Pump 2 forced on
- selector `INVALID` = outputs off

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

It is caused only by feedback failure in `AUTO` after all available pumps have failed.

This alarm affects:

- internal alarm state
- ACK handling

### Feedback-timeout alarm

This is the alarm that drives:

- `Standby alarm` indicator

It is a subset of the general alarm behavior.

## Feedback-Timeout Logic

This is the current `AUTO` rule for the latched `Standby alarm`:

1. Demand exists for the selected primary pump
2. The controller tries the primary if ready, otherwise it may skip straight to the secondary if ready
3. Each attempted pump gets `3 s` to produce matching feedback
4. If one pump fails and the other ready pump is available, the controller fails over automatically
5. If all available pumps fail feedback, both outputs turn off
6. `Standby alarm` then blinks on the module front panel and stays latched until `ACK`

## ACK Behavior

ACK clears the latched `AUTO` alarm and resets the controller to a fresh `OFF` state.

After ACK, if demand still exists, the controller starts again from the primary/secondary decision with no remembered failures.

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
- in `AUTO`, the selected pump is tried first and the other pump is automatic failover
- `AC` not ready only causes a pump to be skipped
- `Standby alarm` is reserved specifically for the latched `3 s` feedback failures after all available pumps have been tried
- ACK resets `AUTO` and starts a fresh cycle
