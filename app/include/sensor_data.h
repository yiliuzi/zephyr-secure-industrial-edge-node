#ifndef SENSOR_DATA_H
#define SENSOR_DATA_H

#include <stdint.h>
#include <zephyr/kernel.h>

#define SENSOR_QUEUE_DEPTH 8

struct sensor_sample {
    uint32_t sequence;
    int32_t temperature_centi_c;
    uint32_t vibration_mg;
    uint32_t supply_mv;
    int64_t timestamp_ms;
};

extern struct k_msgq sensor_queue;

#endif