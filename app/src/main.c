#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(edge_node, LOG_LEVEL_INF);

int main(void)
{
    LOG_INF("Secure Industrial Edge Node starting");
    LOG_INF("Sensor simulator initialized");
    LOG_INF("Safety monitor initialized");

    return 0;
}