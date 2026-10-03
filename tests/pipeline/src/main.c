#include <zephyr/ztest.h>

#include "sensor_data.h"
#include "safety_monitor.h"

static void pipeline_before(void *fixture)
{
    ARG_UNUSED(fixture);
    k_msgq_purge(&sensor_queue);
}

static struct sensor_sample make_sample(uint32_t sequence,
                                       int32_t temperature,
                                       uint32_t vibration,
                                       uint32_t voltage)
{
    return (struct sensor_sample) {
        .sequence = sequence,
        .temperature_centi_c = temperature,
        .vibration_mg = vibration,
        .supply_mv = voltage,
        .timestamp_ms = (int64_t)sequence * 200,
    };
}

static enum safety_state process_sample(
    const struct sensor_sample *sent,
    enum safety_state state,
    uint32_t *recovery_count)
{
    struct sensor_sample received = {0};

    zassert_equal(k_msgq_put(&sensor_queue, sent, K_NO_WAIT), 0,
                  "Failed to enqueue sample");
    zassert_equal(k_msgq_get(&sensor_queue, &received, K_NO_WAIT), 0,
                  "Failed to dequeue sample");
    zassert_equal(received.sequence, sent->sequence,
                  "Wrong sample received");

    return safety_update_state(
        state, safety_evaluate_sample(&received), recovery_count);
}

ZTEST(pipeline, test_complete_safety_cycle)
{
    enum safety_state state = SAFETY_NORMAL;
    uint32_t count = 0U;

    struct sensor_sample sample = make_sample(0U, 2500, 200U, 24000U);
    state = process_sample(&sample, state, &count);
    zassert_equal(state, SAFETY_NORMAL);

    sample = make_sample(1U, 7400, 1700U, 21800U);
    state = process_sample(&sample, state, &count);
    zassert_equal(state, SAFETY_WARNING);

    sample = make_sample(2U, 9200, 3200U, 20500U);
    state = process_sample(&sample, state, &count);
    zassert_equal(state, SAFETY_FAULT);

    for (uint32_t i = 1U; i <= 5U; i++) {
        sample = make_sample(2U + i, 4200, 500U, 23500U);
        state = process_sample(&sample, state, &count);

        zassert_equal(state,
                      i < 5U ? SAFETY_RECOVERY : SAFETY_NORMAL,
                      "Unexpected recovery state at sample %u", i);
        zassert_equal(count, i < 5U ? i : 0U,
                      "Unexpected recovery count");
    }

    zassert_equal(k_msgq_num_used_get(&sensor_queue), 0U);
}

ZTEST(pipeline, test_each_sensor_can_trigger_fault)
{
    const struct sensor_sample faults[] = {
        { .temperature_centi_c = 8500,
          .vibration_mg = 200U, .supply_mv = 24000U },
        { .temperature_centi_c = 2500,
          .vibration_mg = 2500U, .supply_mv = 24000U },
        { .temperature_centi_c = 2500,
          .vibration_mg = 200U, .supply_mv = 21000U },
    };

    for (unsigned int i = 0U; i < ARRAY_SIZE(faults); i++) {
        uint32_t count = 0U;
        enum safety_state state =
            process_sample(&faults[i], SAFETY_NORMAL, &count);

        zassert_equal(state, SAFETY_FAULT,
                      "Sensor case %u did not trigger FAULT", i);
    }
}

ZTEST(pipeline, test_buffered_fault_interrupts_recovery)
{
    enum safety_state state = SAFETY_FAULT;
    uint32_t count = 0U;

    /* Four normal samples, a fault, then three normal samples. */
    const int32_t temperatures[] = {
        2500, 2500, 2500, 2500, 8500, 2500, 2500, 2500
    };
    const enum safety_state expected_states[] = {
        SAFETY_RECOVERY, SAFETY_RECOVERY,
        SAFETY_RECOVERY, SAFETY_RECOVERY,
        SAFETY_FAULT,
        SAFETY_RECOVERY, SAFETY_RECOVERY, SAFETY_RECOVERY
    };
    const uint32_t expected_counts[] = {1U, 2U, 3U, 4U, 0U, 1U, 2U, 3U};

    for (uint32_t i = 0U; i < ARRAY_SIZE(temperatures); i++) {
        struct sensor_sample sample =
            make_sample(i, temperatures[i], 200U, 24000U);

        zassert_equal(k_msgq_put(&sensor_queue, &sample, K_NO_WAIT), 0);
    }

    for (uint32_t i = 0U; i < ARRAY_SIZE(temperatures); i++) {
        struct sensor_sample received = {0};

        zassert_equal(k_msgq_get(&sensor_queue, &received, K_NO_WAIT), 0);
        zassert_equal(received.sequence, i);

        state = safety_update_state(
            state, safety_evaluate_sample(&received), &count);

        zassert_equal(state, expected_states[i],
                      "Wrong state after buffered sample %u", i);
        zassert_equal(count, expected_counts[i],
                      "Wrong recovery count after sample %u", i);
    }

    /* Two more normal samples complete the restarted recovery. */
    for (uint32_t i = 8U; i < 10U; i++) {
        struct sensor_sample sample = make_sample(i, 2500, 200U, 24000U);
        state = process_sample(&sample, state, &count);

        zassert_equal(state, i == 8U ? SAFETY_RECOVERY : SAFETY_NORMAL);
        zassert_equal(count, i == 8U ? 4U : 0U);
    }

    zassert_equal(k_msgq_num_used_get(&sensor_queue), 0U);
}

ZTEST_SUITE(pipeline, NULL, NULL, pipeline_before, NULL, NULL);
