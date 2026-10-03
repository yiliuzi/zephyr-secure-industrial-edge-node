#include <errno.h>
#include <zephyr/ztest.h>

#include "sensor_data.h"

static void queue_before(void *fixture)
{
    ARG_UNUSED(fixture);
    k_msgq_purge(&sensor_queue);
}

static struct sensor_sample make_sample(uint32_t sequence)
{
    return (struct sensor_sample) {
        .sequence = sequence,
        .temperature_centi_c = 2500 + (int32_t)sequence,
        .vibration_mg = 200U + sequence,
        .supply_mv = 24000U - sequence,
        .timestamp_ms = 5000000000LL + sequence,
    };
}

static void assert_sample_equal(const struct sensor_sample *actual,
                                const struct sensor_sample *expected)
{
    zassert_equal(actual->sequence, expected->sequence,
                  "Sequence changed");
    zassert_equal(actual->temperature_centi_c,
                  expected->temperature_centi_c,
                  "Temperature changed");
    zassert_equal(actual->vibration_mg, expected->vibration_mg,
                  "Vibration changed");
    zassert_equal(actual->supply_mv, expected->supply_mv,
                  "Voltage changed");
    zassert_equal(actual->timestamp_ms, expected->timestamp_ms,
                  "64-bit timestamp changed");
}

ZTEST(sensor_queue_tests, test_empty_read)
{
    struct sensor_sample received = {0};

    zassert_equal(k_msgq_num_used_get(&sensor_queue), 0U);
    zassert_equal(k_msgq_num_free_get(&sensor_queue),
                  SENSOR_QUEUE_DEPTH);

    zassert_equal(k_msgq_get(&sensor_queue, &received, K_NO_WAIT),
                  -ENOMSG, "Empty queue must reject nonblocking read");
}

ZTEST(sensor_queue_tests, test_data_copied)
{
    struct sensor_sample sent = make_sample(7U);
    struct sensor_sample expected = sent;
    struct sensor_sample received = {0};

    sent.temperature_centi_c = -1250;
    expected = sent;

    zassert_equal(k_msgq_put(&sensor_queue, &sent, K_NO_WAIT), 0);

    /* Changing the sender buffer must not change queued data. */
    sent = make_sample(99U);

    zassert_equal(k_msgq_num_used_get(&sensor_queue), 1U);
    zassert_equal(k_msgq_get(&sensor_queue, &received, K_NO_WAIT), 0);

    assert_sample_equal(&received, &expected);
    zassert_equal(k_msgq_num_used_get(&sensor_queue), 0U);
}

ZTEST(sensor_queue_tests, test_fifo_order)
{
    for (uint32_t i = 0U; i < SENSOR_QUEUE_DEPTH; i++) {
        struct sensor_sample sent = make_sample(i);
        zassert_equal(k_msgq_put(&sensor_queue, &sent, K_NO_WAIT), 0);
    }

    for (uint32_t i = 0U; i < SENSOR_QUEUE_DEPTH; i++) {
        struct sensor_sample expected = make_sample(i);
        struct sensor_sample received = {0};

        zassert_equal(k_msgq_get(&sensor_queue, &received, K_NO_WAIT), 0);
        assert_sample_equal(&received, &expected);
    }
}

ZTEST(sensor_queue_tests, test_full_queue_rejects_new_sample)
{
    for (uint32_t i = 0U; i < SENSOR_QUEUE_DEPTH; i++) {
        struct sensor_sample sent = make_sample(i);
        zassert_equal(k_msgq_put(&sensor_queue, &sent, K_NO_WAIT), 0);
    }

    zassert_equal(k_msgq_num_used_get(&sensor_queue),
                  SENSOR_QUEUE_DEPTH);
    zassert_equal(k_msgq_num_free_get(&sensor_queue), 0U);

    struct sensor_sample extra = make_sample(999U);

    zassert_equal(k_msgq_put(&sensor_queue, &extra, K_NO_WAIT),
                  -ENOMSG, "Full queue must reject new sample");

    /* Rejection must preserve all previously queued samples. */
    for (uint32_t i = 0U; i < SENSOR_QUEUE_DEPTH; i++) {
        struct sensor_sample expected = make_sample(i);
        struct sensor_sample received = {0};

        zassert_equal(k_msgq_get(&sensor_queue, &received, K_NO_WAIT), 0);
        assert_sample_equal(&received, &expected);
    }

    struct sensor_sample received = {0};
    zassert_equal(k_msgq_get(&sensor_queue, &received, K_NO_WAIT),
                  -ENOMSG);
}

ZTEST(sensor_queue_tests, test_ring_buffer_wraparound)
{
    /* Fill, remove one, then refill to exercise buffer wraparound. */
    for (uint32_t i = 0U; i < SENSOR_QUEUE_DEPTH; i++) {
        struct sensor_sample sent = make_sample(i);
        zassert_equal(k_msgq_put(&sensor_queue, &sent, K_NO_WAIT), 0);
    }

    struct sensor_sample received = {0};
    struct sensor_sample expected = make_sample(0U);

    zassert_equal(k_msgq_get(&sensor_queue, &received, K_NO_WAIT), 0);
    assert_sample_equal(&received, &expected);

    struct sensor_sample next = make_sample(SENSOR_QUEUE_DEPTH);
    zassert_equal(k_msgq_put(&sensor_queue, &next, K_NO_WAIT), 0);

    for (uint32_t i = 1U; i <= SENSOR_QUEUE_DEPTH; i++) {
        expected = make_sample(i);
        zassert_equal(k_msgq_get(&sensor_queue, &received, K_NO_WAIT), 0);
        assert_sample_equal(&received, &expected);
    }

    zassert_equal(k_msgq_num_used_get(&sensor_queue), 0U);
    zassert_equal(k_msgq_num_free_get(&sensor_queue),
                  SENSOR_QUEUE_DEPTH);
}

ZTEST_SUITE(sensor_queue_tests, NULL, NULL, queue_before, NULL, NULL);
