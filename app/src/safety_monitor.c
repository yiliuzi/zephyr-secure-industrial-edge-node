#include <stdbool.h>
#include <stdint.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "safety_monitor.h"

LOG_MODULE_REGISTER(safety_monitor, LOG_LEVEL_INF);

#define SAFETY_STACK_SIZE 1536
#define SAFETY_PRIORITY 4
#ifndef SAFETY_PROCESS_DELAY_MS
#define SAFETY_PROCESS_DELAY_MS 0
#endif

#define RECOVERY_REQUIRED_SAMPLES 5U

#define TEMP_WARNING_CENTI_C 7000
#define TEMP_FAULT_CENTI_C 8500

#define VIBRATION_WARNING_MG 1500U
#define VIBRATION_FAULT_MG 2500U

#define VOLTAGE_WARNING_MV 22000U
#define VOLTAGE_FAULT_MV 21000U

const char *safety_state_name(enum safety_state state)
{
    switch (state) {
    case SAFETY_NORMAL:
        return "NORMAL";

    case SAFETY_WARNING:
        return "WARNING";

    case SAFETY_FAULT:
        return "FAULT";

    case SAFETY_RECOVERY:
        return "RECOVERY";

    default:
        return "UNKNOWN";
    }
}

enum sample_severity safety_evaluate_sample(
    const struct sensor_sample *sample
)
{
    bool fault_detected =
        sample->temperature_centi_c >= TEMP_FAULT_CENTI_C ||
        sample->vibration_mg >= VIBRATION_FAULT_MG ||
        sample->supply_mv <= VOLTAGE_FAULT_MV;

    if (fault_detected) {
        return SEVERITY_FAULT;
    }

    bool warning_detected =
        sample->temperature_centi_c >= TEMP_WARNING_CENTI_C ||
        sample->vibration_mg >= VIBRATION_WARNING_MG ||
        sample->supply_mv <= VOLTAGE_WARNING_MV;

    if (warning_detected) {
        return SEVERITY_WARNING;
    }

    return SEVERITY_NORMAL;
}

enum safety_state safety_update_state(
    enum safety_state current_state,
    enum sample_severity severity,
    uint32_t *recovery_samples
)
{
    switch (current_state) {
    case SAFETY_NORMAL:
        if (severity == SEVERITY_FAULT) {
            return SAFETY_FAULT;
        }

        if (severity == SEVERITY_WARNING) {
            return SAFETY_WARNING;
        }

        break;

    case SAFETY_WARNING:
        if (severity == SEVERITY_FAULT) {
            return SAFETY_FAULT;
        }

        if (severity == SEVERITY_NORMAL) {
            *recovery_samples = 1U;
            return SAFETY_RECOVERY;
        }

        break;

    case SAFETY_FAULT:
        if (severity == SEVERITY_NORMAL) {
            *recovery_samples = 1U;
            return SAFETY_RECOVERY;
        }

        break;

    case SAFETY_RECOVERY:
        if (severity == SEVERITY_FAULT) {
            *recovery_samples = 0U;
            return SAFETY_FAULT;
        }

        if (severity == SEVERITY_WARNING) {
            *recovery_samples = 0U;
            return SAFETY_WARNING;
        }

        (*recovery_samples)++;

        if (*recovery_samples >= RECOVERY_REQUIRED_SAMPLES) {
            *recovery_samples = 0U;
            return SAFETY_NORMAL;
        }

        break;

    default:
        *recovery_samples = 0U;
        return SAFETY_FAULT;
    }

    return current_state;
}

#ifndef SAFETY_UNIT_TEST

static void safety_thread(
    void *arg1,
    void *arg2,
    void *arg3
)
{
    ARG_UNUSED(arg1);
    ARG_UNUSED(arg2);
    ARG_UNUSED(arg3);

    enum safety_state state = SAFETY_NORMAL;
    uint32_t recovery_samples = 0U;

    while (true) {
        struct sensor_sample sample;

        int result = k_msgq_get(
            &sensor_queue,
            &sample,
            K_FOREVER
        );

        if (result != 0) {
            LOG_ERR("Failed to receive sensor sample");
            continue;
        }

        enum sample_severity severity =
            safety_evaluate_sample(&sample);

        enum safety_state next_state =
            safety_update_state(
                state,
                severity,
                &recovery_samples
            );

        LOG_INF(
            "sample=%u temp=%d cC vibration=%u mg voltage=%u mV",
            sample.sequence,
            (int)sample.temperature_centi_c,
            sample.vibration_mg,
            sample.supply_mv
        );

        if (next_state != state) {
            LOG_WRN(
                "Safety transition: %s -> %s",
                safety_state_name(state),
                safety_state_name(next_state)
            );

            state = next_state;
        }

#if SAFETY_PROCESS_DELAY_MS > 0
        k_msleep(SAFETY_PROCESS_DELAY_MS);
#endif
    }
}

K_THREAD_DEFINE(
    safety_thread_id,
    SAFETY_STACK_SIZE,
    safety_thread,
    NULL,
    NULL,
    NULL,
    SAFETY_PRIORITY,
    0,
    0
);
#endif /* SAFETY_UNIT_TEST */