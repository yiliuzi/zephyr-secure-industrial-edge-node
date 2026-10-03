#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(edge_node, LOG_LEVEL_INF);

#define SENSOR_THREAD_STACK_SIZE 1024
#define SAFETY_THREAD_STACK_SIZE 1024

#define SENSOR_THREAD_PRIORITY 5
#define SAFETY_THREAD_PRIORITY 4

#define SENSOR_PERIOD_MS 1000
#define SAFETY_PERIOD_MS 2000

static void sensor_thread(void *arg1, void *arg2, void *arg3)
{
    ARG_UNUSED(arg1);
    ARG_UNUSED(arg2);
    ARG_UNUSED(arg3);

    uint32_t sample_number = 0U;

    while (true) {
        sample_number++;

        LOG_INF(
            "Sensor task: acquired sample %u",
            sample_number
        );

        k_msleep(SENSOR_PERIOD_MS);
    }
}

static void safety_thread(void *arg1, void *arg2, void *arg3)
{
    ARG_UNUSED(arg1);
    ARG_UNUSED(arg2);
    ARG_UNUSED(arg3);

    while (true) {
        LOG_INF("Safety task: system state is healthy");

        k_msleep(SAFETY_PERIOD_MS);
    }
}

K_THREAD_DEFINE(
    sensor_thread_id,
    SENSOR_THREAD_STACK_SIZE,
    sensor_thread,
    NULL,
    NULL,
    NULL,
    SENSOR_THREAD_PRIORITY,
    0,
    0
);

K_THREAD_DEFINE(
    safety_thread_id,
    SAFETY_THREAD_STACK_SIZE,
    safety_thread,
    NULL,
    NULL,
    NULL,
    SAFETY_THREAD_PRIORITY,
    0,
    0
);

int main(void)
{
    LOG_INF("Secure Industrial Edge Node starting");
    LOG_INF("RTOS services initialized");

    return 0;
}