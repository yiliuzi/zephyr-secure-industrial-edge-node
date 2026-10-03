#include <zephyr/ztest.h>

#include "safety_monitor.h"

static struct sensor_sample normal_sample(void)
{
    return (struct sensor_sample) {
        .sequence = 0U,
        .temperature_centi_c = 2500,
        .vibration_mg = 200U,
        .supply_mv = 24000U,
        .timestamp_ms = 0,
    };
}

ZTEST(safety, test_temperature_boundaries)
{
    const int32_t values[] = {6999, 7000, 8499, 8500};
    const enum sample_severity expected[] = {
        SEVERITY_NORMAL, SEVERITY_WARNING,
        SEVERITY_WARNING, SEVERITY_FAULT
    };

    for (unsigned int i = 0; i < ARRAY_SIZE(values); i++) {
        struct sensor_sample sample = normal_sample();
        sample.temperature_centi_c = values[i];

        zassert_equal(safety_evaluate_sample(&sample), expected[i],
                      "Temperature boundary failed: %d", (int)values[i]);
    }
}

ZTEST(safety, test_vibration_boundaries)
{
    const uint32_t values[] = {1499U, 1500U, 2499U, 2500U};
    const enum sample_severity expected[] = {
        SEVERITY_NORMAL, SEVERITY_WARNING,
        SEVERITY_WARNING, SEVERITY_FAULT
    };

    for (unsigned int i = 0; i < ARRAY_SIZE(values); i++) {
        struct sensor_sample sample = normal_sample();
        sample.vibration_mg = values[i];

        zassert_equal(safety_evaluate_sample(&sample), expected[i],
                      "Vibration boundary failed: %u", values[i]);
    }
}

ZTEST(safety, test_voltage_boundaries)
{
    const uint32_t values[] = {22001U, 22000U, 21001U, 21000U};
    const enum sample_severity expected[] = {
        SEVERITY_NORMAL, SEVERITY_WARNING,
        SEVERITY_WARNING, SEVERITY_FAULT
    };

    for (unsigned int i = 0; i < ARRAY_SIZE(values); i++) {
        struct sensor_sample sample = normal_sample();
        sample.supply_mv = values[i];

        zassert_equal(safety_evaluate_sample(&sample), expected[i],
                      "Voltage boundary failed: %u", values[i]);
    }
}

ZTEST(safety, test_fault_overrides_warning)
{
    struct sensor_sample sample = normal_sample();

    sample.temperature_centi_c = 7400;
    sample.supply_mv = 20500U;

    zassert_equal(safety_evaluate_sample(&sample), SEVERITY_FAULT,
                  "Fault must override warning");
}

ZTEST(safety, test_state_transition_table)
{
    const enum safety_state states[] = {
        SAFETY_NORMAL, SAFETY_WARNING, SAFETY_FAULT
    };
    const enum sample_severity severities[] = {
        SEVERITY_NORMAL, SEVERITY_WARNING, SEVERITY_FAULT
    };
    const enum safety_state expected[3][3] = {
        {SAFETY_NORMAL, SAFETY_WARNING, SAFETY_FAULT},
        {SAFETY_RECOVERY, SAFETY_WARNING, SAFETY_FAULT},
        {SAFETY_RECOVERY, SAFETY_FAULT, SAFETY_FAULT}
    };

    for (unsigned int i = 0; i < ARRAY_SIZE(states); i++) {
        for (unsigned int j = 0; j < ARRAY_SIZE(severities); j++) {
            uint32_t count = 0U;
            enum safety_state next =
                safety_update_state(states[i], severities[j], &count);

            zassert_equal(next, expected[i][j],
                          "Transition failed: state=%u severity=%u",
                          (unsigned int)states[i],
                          (unsigned int)severities[j]);

            if (next == SAFETY_RECOVERY) {
                zassert_equal(count, 1U,
                              "First normal sample must count as one");
            }
        }
    }
}

ZTEST(safety, test_five_normal_samples_required)
{
    uint32_t count = 0U;
    enum safety_state state = SAFETY_FAULT;

    for (uint32_t i = 1U; i <= 5U; i++) {
        state = safety_update_state(state, SEVERITY_NORMAL, &count);

        if (i < 5U) {
            zassert_equal(state, SAFETY_RECOVERY,
                          "Recovered too early at sample %u", i);
            zassert_equal(count, i, "Incorrect recovery count");
        } else {
            zassert_equal(state, SAFETY_NORMAL,
                          "Must recover on fifth normal sample");
            zassert_equal(count, 0U,
                          "Counter must reset after recovery");
        }
    }
}

ZTEST(safety, test_warning_interrupts_recovery)
{
    uint32_t count = 4U;
    enum safety_state state =
        safety_update_state(SAFETY_RECOVERY, SEVERITY_WARNING, &count);

    zassert_equal(state, SAFETY_WARNING, "Warning must interrupt recovery");
    zassert_equal(count, 0U, "Interrupted recovery must reset counter");

    state = safety_update_state(state, SEVERITY_NORMAL, &count);

    zassert_equal(state, SAFETY_RECOVERY, "Recovery must restart");
    zassert_equal(count, 1U, "Recovery must restart from one");
}

ZTEST(safety, test_fault_interrupts_recovery)
{
    uint32_t count = 4U;
    enum safety_state state =
        safety_update_state(SAFETY_RECOVERY, SEVERITY_FAULT, &count);

    zassert_equal(state, SAFETY_FAULT, "Fault must interrupt recovery");
    zassert_equal(count, 0U, "Interrupted recovery must reset counter");
}

ZTEST(safety, test_invalid_state_fails_safe)
{
    uint32_t count = 3U;
    enum safety_state state =
        safety_update_state((enum safety_state)99, SEVERITY_NORMAL, &count);

    zassert_equal(state, SAFETY_FAULT, "Invalid state must enter FAULT");
    zassert_equal(count, 0U, "Invalid state must reset counter");
}

ZTEST(safety, test_state_names)
{
    zassert_str_equal(safety_state_name(SAFETY_NORMAL), "NORMAL");
    zassert_str_equal(safety_state_name(SAFETY_WARNING), "WARNING");
    zassert_str_equal(safety_state_name(SAFETY_FAULT), "FAULT");
    zassert_str_equal(safety_state_name(SAFETY_RECOVERY), "RECOVERY");
    zassert_str_equal(safety_state_name((enum safety_state)99), "UNKNOWN");
}

ZTEST_SUITE(safety, NULL, NULL, NULL, NULL, NULL);
