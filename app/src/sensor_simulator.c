#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "sensor_data.h"

LOG_MODULE_REGISTER(sensor_simulator, LOG_LEVEL_INF);

#define SENSOR_STACK_SIZE 1536
#define SENSOR_PRIORITY 5
#define SENSOR_PERIOD_MS 200

K_MSGQ_DEFINE(
    sensor_queue,
    sizeof(struct sensor_sample),
    SENSOR_QUEUE_DEPTH,
    4
);
#ifndef SENSOR_UNIT_TEST

static struct sensor_sample generate_simulated_sample(uint32_t sequence)
{
    struct sensor_sample sample = {
        .sequence = sequence,
        .timestamp_ms = k_uptime_get(),
    };

    uint32_t phase = sequence % 40U;

    if (phase < 20U) {
        sample.temperature_centi_c =
            2500 + (int32_t)((sequence % 5U) * 10U);
        sample.vibration_mg =
            200U + (sequence % 4U) * 10U;
        sample.supply_mv = 24000U;
    } else if (phase < 28U) {
        sample.temperature_centi_c = 7400;
        sample.vibration_mg = 1700U;
        sample.supply_mv = 21800U;
    } else if (phase < 34U) {
        sample.temperature_centi_c = 9200;
        sample.vibration_mg = 3200U;
        sample.supply_mv = 20500U;
    } else {
        sample.temperature_centi_c = 4200;
        sample.vibration_mg = 500U;
        sample.supply_mv = 23500U;
    }

    return sample;
}

static void sensor_thread(
    void *arg1,
    void *arg2,
    void *arg3
)
{
    ARG_UNUSED(arg1);
    ARG_UNUSED(arg2);
    ARG_UNUSED(arg3);

    uint32_t sequence = 0U;

    while (true) {
        struct sensor_sample sample =
            generate_simulated_sample(sequence++);

        int result = k_msgq_put(
            &sensor_queue,
            &sample,
            K_NO_WAIT
        );

        if (result != 0) {
            LOG_WRN(
                "Queue full: dropped sample %u",
                sample.sequence
            );
        }

        k_msleep(SENSOR_PERIOD_MS);
    }
}

K_THREAD_DEFINE(
    sensor_thread_id,
    SENSOR_STACK_SIZE,
    sensor_thread,
    NULL,
    NULL,
    NULL,
    SENSOR_PRIORITY,
    0,
    0
);
#endif /* SENSOR_UNIT_TEST */