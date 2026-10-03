#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(edge_node, LOG_LEVEL_INF);

#define SENSOR_STACK_SIZE 1536
#define SAFETY_STACK_SIZE 1536

#define SENSOR_PRIORITY 5
#define SAFETY_PRIORITY 4

#define SENSOR_PERIOD_MS 200
#define SENSOR_QUEUE_DEPTH 8
#define RECOVERY_REQUIRED_SAMPLES 5

#define TEMP_WARNING_CENTI_C 7000
#define TEMP_FAULT_CENTI_C 8500

#define VIBRATION_WARNING_MG 1500U
#define VIBRATION_FAULT_MG 2500U

#define VOLTAGE_WARNING_MV 22000U
#define VOLTAGE_FAULT_MV 21000U

struct sensor_sample {
    uint32_t sequence;
    int32_t temperature_centi_c;
    uint32_t vibration_mg;
    uint32_t supply_mv;
    int64_t timestamp_ms;
};

enum sample_severity {
    SEVERITY_NORMAL,
    SEVERITY_WARNING,
    SEVERITY_FAULT,
};

enum safety_state {
    SAFETY_NORMAL,
    SAFETY_WARNING,
    SAFETY_FAULT,
    SAFETY_RECOVERY,
};

K_MSGQ_DEFINE(
    sensor_queue,
    sizeof(struct sensor_sample),
    SENSOR_QUEUE_DEPTH,
    4
);

static const char *safety_state_name(enum safety_state state)
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
        sample.vibration_mg = 200U + (sequence % 4U) * 10U;
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

static enum sample_severity evaluate_sample(
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

static enum safety_state update_safety_state(
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

static void sensor_thread(void *arg1, void *arg2, void *arg3)
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
                "Sensor queue full: dropped sample %u",
                sample.sequence
            );
        }

        k_msleep(SENSOR_PERIOD_MS);
    }
}

static void safety_thread(void *arg1, void *arg2, void *arg3)
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
            evaluate_sample(&sample);

        enum safety_state next_state =
            update_safety_state(
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

int main(void)
{
    LOG_INF("Secure Industrial Edge Node starting");
    LOG_INF("Sensor message queue initialized");
    LOG_INF("Safety state machine initialized");

    return 0;
}