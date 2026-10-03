# Safety Monitor Test Results

## Environment
- Zephyr: 4.4.2
- Board: mps2/an385
- Runtime: QEMU Cortex-M3
- Framework: Zephyr ztest

## Results
- Total: 10
- Passed: 10
- Failed: 0
- Skipped: 0

## Verified behavior
- Temperature, vibration and voltage threshold boundaries
- Fault priority over warning
- NORMAL, WARNING and FAULT state transitions
- Recovery requires five consecutive normal samples
- Warning or fault interrupts recovery and resets the counter
- Invalid state enters FAULT
- State name mapping

## Commands
west build -p always -b mps2/an385 -d build/safety_tests tests/safety
west build -d build/safety_tests -t run

## Scope
These tests verify safety decision functions.
They do not measure code coverage, thread timing or queue behavior.
