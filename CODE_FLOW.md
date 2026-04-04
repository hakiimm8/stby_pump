# Code Flow Note

This note describes the current firmware behavior at a system level.

It is intentionally written without code snippets so it can be used as an operational reference when checking the controller on hardware.

## Scope

- Main firmware file: `Core/Src/main.c`
- Current default build: selector-driven dual-pump mode
- Alternate modes still available in the source:
  - bypass dual mode
  - output test mode

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

- `SEL_P1` and `SEL_P2` are active high
- Decode is:
  - `00` = `OFF`
  - `10` = `PUMP 1`
  - `01` = `PUMP 2`
  - `11` = `INVALID`

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

- `ACK_LT` is active low
- Short press = `ACK`
- Long press = `ACK + lamp test`

Current implementation performs ACK on the press edge. If the press is held long enough, lamp test is also enabled.

## Operating Modes

## Normal Mode

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

## Bypass Dual Mode

This mode ignores the selector and treats both pump channels independently.

Each pump channel uses its own:

- pressure input
- RPM input
- AC ready input

In bypass mode:

- pump 1 can run independently from pump 2
- pump 2 can run independently from pump 1
- both pumps may run at the same time

The same demand and permission logic is used per channel:

- run request = pressure low active or RPM inactive
- run allowed only if AC ready is active
- stop when pressure low is inactive and RPM is active

## Output Test Mode

This mode is for bench checking outputs and indicator mapping, not for control.

### Relay sequence

The relay outputs cycle in this order:

1. `Failure AMS`
2. `Pump 1`
3. `Pump 2`
4. break

Each step uses the configured output-test repeat time.

### Indicator behavior

If no test input is active:

- panel LEDs step through the indicator sequence one at a time

If any of the test inputs `I4..I7` is active:

- the sequence is overridden
- a single corresponding indicator is shown

Output test mode is only for mapping verification. It does not represent normal controller behavior.

## Alarm Behavior

There are currently two different alarm concepts in the firmware:

### General latched alarm

This is the main latched alarm state used for system health.

It can be caused by:

- invalid selector
- demand exists while the selected pump is not ready
- pressure-timeout fault

This alarm affects:

- `System ready` indicator
- internal alarm state
- ACK handling

### Pressure-timeout alarm

This is the specific alarm that drives:

- `Failure AMS`
- `Standby alarm` indicator

It is a subset of the general alarm behavior.

## Pressure-Timeout Logic

This is the current rule for `Failure AMS` and `Standby alarm`:

1. A pump starts running
2. The firmware begins counting from the pump run start time
3. If low pressure is still active after `10 s`, a pressure-timeout fault occurs
4. The pump is turned off
5. The alarm is latched
6. `Failure AMS` turns on
7. `Standby alarm` blinks on the module front panel

This timeout is checked from pump run start, not from a separate background timer.

If pressure clears before the `10 s` expires:

- the pressure-timeout fault does not occur

If the pump is not allowed to start because AC ready is absent:

- the low-pressure indicator still works normally
- the pump stays off
- `Failure AMS` does not turn on from that condition alone

## ACK Behavior

ACK clears the latched alarm state.

For the pressure-timeout case specifically:

- ACK clears the latched timeout alarm
- `Failure AMS` turns off
- the module returns to normal operation again

After ACK, if demand still exists and the selected pump is ready, the system is allowed to start the pump again as a fresh cycle.

This also means the `10 s` timeout starts again from the new pump start, not from the old failed attempt.

## Indicator Behavior

### Panel indicators

- `IND9 System ready`
  - on when the general alarm is not latched

- `IND1 Pump 1 ready`
  - follows pump 1 AC ready input

- `IND10 Pump 1 ON`
  - on when pump 1 command is active

- `IND2 Pump 1 standby`
  - on when selector is on pump 2 in normal dual mode

- `IND11 Pump 2 ready`
  - follows pump 2 AC ready input

- `IND3 Pump 2 ON`
  - on when pump 2 command is active

- `IND12 Pump 2 standby`
  - on when selector is on pump 1 in normal dual mode

- `IND4 Pressure low`
  - shows active demand
  - in normal mode it follows the selected pump path
  - in bypass mode it reflects combined channel demand

- `IND13 Standby alarm`
  - follows the same latched pressure-timeout fault as `Failure AMS`
  - blinks using the common blink timing

### Lamp test

Lamp test forces all panel indicators on together.

Intended panel indicators during lamp test:

- `System ready`
- `Pump 1 ready`
- `Pump 1 ON`
- `Pump 1 standby`
- `Pump 2 ready`
- `Pump 2 ON`
- `Pump 2 standby`
- `Pressure low`
- `Standby alarm`

If some indicators still do not light during lamp test on real hardware, that points to mapping or hardware path issues rather than the intended lamp-test behavior.

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

- relay bit 7 = `Q1 = Failure AMS`
- relay bit 6 = `Q2 = Pump 1`
- relay bit 5 = `Q3 = Pump 2`

### LED outputs

LED banks are straight mapped:

- LED byte 1 = `IND1..IND8`
- LED byte 2 = `IND9..IND16`

## Current Practical Summary

The current firmware behaves as a selector-driven standby pump controller with these main rules:

- demand comes from pressure low or RPM inactive
- a selected pump can only run if its AC ready input is active
- the pump stops when pressure low clears and RPM is active
- invalid selector and not-ready demand latch the general alarm
- `Failure AMS` and `Standby alarm` are reserved specifically for the `10 s` pressure-timeout failure after a pump has been running
- ACK returns the controller to normal operation again
