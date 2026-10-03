# Pipeline Integration Test Results

Environment: Zephyr 4.4.2, mps2/an385, QEMU Cortex-M3
Framework: Zephyr ztest

Results: 3 passed, 0 failed, 0 skipped

Verified:
- NORMAL -> WARNING -> FAULT -> RECOVERY -> NORMAL
- Each sensor independently triggers FAULT at its fault threshold
- Buffered fault interrupts recovery and requires five new normal samples

Commands:
west build -p always -b mps2/an385 -d build/pipeline_tests tests/pipeline
west build -d build/pipeline_tests -t run

Scope:
Tests connect the actual sensor queue with safety decision functions.
Background threads are disabled; concurrent scheduling is not tested.

Total across three suites: 18 passed, 0 failed, 0 skipped.
