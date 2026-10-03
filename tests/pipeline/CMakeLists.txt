cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(pipeline_tests)

target_include_directories(app PRIVATE ../../app/include)
target_compile_definitions(app PRIVATE
    SENSOR_UNIT_TEST=1
    SAFETY_UNIT_TEST=1
)

target_sources(app PRIVATE
    src/main.c
    ../../app/src/sensor_simulator.c
    ../../app/src/safety_monitor.c
)
