#ifndef SAFETY_MONITOR_H
#define SAFETY_MONITOR_H

#include "sensor_data.h"

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

const char *safety_state_name(enum safety_state state);

enum sample_severity safety_evaluate_sample(
    const struct sensor_sample *sample
);

enum safety_state safety_update_state(
    enum safety_state current_state,
    enum sample_severity severity,
    uint32_t *recovery_samples
);

#endif