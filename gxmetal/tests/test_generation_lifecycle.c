#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "GXMetalGenerationPolicy.h"

#define CHECK(expression) do {                                               \
    if (!(expression)) {                                                     \
        fprintf(stderr, "%s:%d: check failed: %s\n",                      \
                __FILE__, __LINE__, #expression);                            \
        exit(1);                                                             \
    }                                                                        \
} while (0)

typedef struct GenerationHarness {
    GXMetalGenerationOwner owner;
    GXMetalGenerationOwner contexts[8];
    int context_active[8];
    int initialized;
    unsigned int live_contexts;
    unsigned int live_resources;
    unsigned int resets;
} GenerationHarness;

static int same_process(const GXMetalGenerationOwner *owner,
                        int process_known, uint32_t high, uint32_t low)
{
    return owner->valid == process_known &&
           (!process_known || (owner->high == high && owner->low == low));
}

static int prepare_generation(GenerationHarness *harness, int process_known,
                              uint32_t high, uint32_t low)
{
    int reset = gxmetal_generation_should_reset(
        harness->initialized, harness->live_contexts != 0, &harness->owner,
        process_known, high, low);

    if (reset) {
        harness->initialized = 1;
        harness->live_resources = 0;
        harness->resets++;
        gxmetal_generation_note_owner(&harness->owner, process_known,
                                       high, low);
    } else if (harness->live_contexts == 0 && process_known &&
               !harness->owner.valid) {
        gxmetal_generation_note_owner(&harness->owner, 1, high, low);
    }
    return reset;
}

static int create_context(GenerationHarness *harness, int process_known,
                          uint32_t high, uint32_t low)
{
    unsigned int i;
    int reset = prepare_generation(harness, process_known, high, low);

    for (i = 0; i < 8; i++) {
        if (!harness->context_active[i]) {
            harness->context_active[i] = 1;
            gxmetal_generation_note_owner(&harness->contexts[i],
                                           process_known, high, low);
            break;
        }
    }
    CHECK(i != 8);
    harness->live_contexts++;
    return reset;
}

static int create_resource(GenerationHarness *harness, int process_known,
                           uint32_t high, uint32_t low)
{
    int reset = prepare_generation(harness, process_known, high, low);

    harness->live_resources++;
    return reset;
}

static void delete_context(GenerationHarness *harness, int process_known,
                           uint32_t high, uint32_t low)
{
    unsigned int i;

    CHECK(harness->live_contexts != 0);
    for (i = 0; i < 8; i++) {
        if (harness->context_active[i] &&
            same_process(&harness->contexts[i], process_known, high, low)) {
            harness->context_active[i] = 0;
            break;
        }
    }
    CHECK(i != 8);
    harness->live_contexts--;
    if (harness->live_contexts == 0 && process_known) {
        gxmetal_generation_note_owner(&harness->owner, process_known,
                                       high, low);
    }
}

static unsigned int retire_exited_process(GenerationHarness *harness,
                                          uint32_t high, uint32_t low)
{
    unsigned int retired = 0;
    unsigned int i;

    for (i = 0; i < 8; i++) {
        if (harness->context_active[i] &&
            gxmetal_generation_process_equal(&harness->contexts[i],
                                               high, low)) {
            harness->context_active[i] = 0;
            harness->live_contexts--;
            retired++;
        }
    }
    return retired;
}

static void test_first_context_and_same_process_recreation(void)
{
    GenerationHarness harness = {0};

    CHECK(create_context(&harness, 1, 0, 100) == 1);
    CHECK(harness.resets == 1);
    delete_context(&harness, 1, 0, 100);

    CHECK(create_context(&harness, 1, 0, 100) == 0);
    CHECK(harness.resets == 1);
    delete_context(&harness, 1, 0, 100);

    CHECK(create_context(&harness, 1, 0, 100) == 0);
    CHECK(harness.resets == 1);
}

static void test_resource_before_first_context_establishes_generation(void)
{
    GenerationHarness harness = {0};

    CHECK(create_resource(&harness, 1, 1, 90) == 1);
    CHECK(harness.resets == 1);
    CHECK(harness.live_resources == 1);

    CHECK(create_context(&harness, 1, 1, 90) == 0);
    CHECK(harness.resets == 1);
    CHECK(harness.live_resources == 1);
}

static void test_new_process_resource_resets_before_create(void)
{
    GenerationHarness harness = {0};

    CHECK(create_resource(&harness, 1, 2, 95) == 1);
    CHECK(create_resource(&harness, 1, 2, 95) == 0);
    CHECK(harness.live_resources == 2);

    CHECK(create_resource(&harness, 1, 2, 96) == 1);
    CHECK(harness.resets == 2);
    CHECK(harness.live_resources == 1);
    CHECK(create_context(&harness, 1, 2, 96) == 0);
    CHECK(harness.live_resources == 1);
}

static void test_new_process_after_idle_resets(void)
{
    GenerationHarness harness = {0};

    CHECK(create_context(&harness, 1, 3, 100) == 1);
    delete_context(&harness, 1, 3, 100);
    CHECK(create_context(&harness, 1, 3, 101) == 1);
    CHECK(harness.resets == 2);
    CHECK(gxmetal_generation_process_equal(&harness.owner, 3, 101));

    delete_context(&harness, 1, 3, 101);
    CHECK(create_context(&harness, 1, 3, 101) == 0);
    CHECK(harness.resets == 2);
}

static void test_concurrent_process_does_not_reset_live_contexts(void)
{
    GenerationHarness harness = {0};

    CHECK(create_context(&harness, 1, 8, 200) == 1);
    CHECK(create_context(&harness, 1, 8, 201) == 0);
    CHECK(harness.live_contexts == 2);
    CHECK(harness.resets == 1);
    CHECK(gxmetal_generation_process_equal(&harness.owner, 8, 200));

    delete_context(&harness, 1, 8, 200);
    CHECK(create_context(&harness, 1, 8, 202) == 0);
    CHECK(harness.live_contexts == 2);
    CHECK(harness.resets == 1);

    delete_context(&harness, 1, 8, 202);
    delete_context(&harness, 1, 8, 201);
    CHECK(gxmetal_generation_process_equal(&harness.owner, 8, 201));
    CHECK(create_context(&harness, 1, 8, 201) == 0);
    CHECK(harness.resets == 1);

    delete_context(&harness, 1, 8, 201);
    CHECK(create_context(&harness, 1, 8, 200) == 1);
    CHECK(harness.resets == 2);
}

static void test_unknown_process_never_forces_spurious_reset(void)
{
    GenerationHarness harness = {0};

    CHECK(create_context(&harness, 0, 0, 0) == 1);
    CHECK(!harness.owner.valid);
    delete_context(&harness, 0, 0, 0);

    CHECK(create_context(&harness, 0, 0, 0) == 0);
    CHECK(harness.resets == 1);
    delete_context(&harness, 0, 0, 0);
    CHECK(create_context(&harness, 1, 9, 300) == 0);
    CHECK(harness.resets == 1);
    CHECK(gxmetal_generation_process_equal(&harness.owner, 9, 300));
    delete_context(&harness, 1, 9, 300);
    CHECK(create_context(&harness, 1, 9, 301) == 1);
    CHECK(harness.resets == 2);
}

static void test_exited_process_stale_context_is_retired(void)
{
    GenerationHarness harness = {0};

    CHECK(create_context(&harness, 1, 10, 400) == 1);
    CHECK(harness.live_contexts == 1);
    CHECK(retire_exited_process(&harness, 10, 400) == 1);
    CHECK(harness.live_contexts == 0);

    CHECK(create_context(&harness, 1, 10, 401) == 1);
    CHECK(harness.resets == 2);
}

static void test_exited_context_does_not_retire_live_concurrent_context(void)
{
    GenerationHarness harness = {0};

    CHECK(create_context(&harness, 1, 11, 500) == 1);
    CHECK(create_context(&harness, 1, 11, 501) == 0);
    CHECK(retire_exited_process(&harness, 11, 500) == 1);
    CHECK(harness.live_contexts == 1);

    CHECK(create_context(&harness, 1, 11, 502) == 0);
    CHECK(harness.resets == 1);
    CHECK(harness.live_contexts == 2);
    delete_context(&harness, 1, 11, 501);
    delete_context(&harness, 1, 11, 502);

    CHECK(create_context(&harness, 1, 11, 503) == 1);
    CHECK(harness.resets == 2);
}

int main(void)
{
    test_first_context_and_same_process_recreation();
    test_resource_before_first_context_establishes_generation();
    test_new_process_resource_resets_before_create();
    test_new_process_after_idle_resets();
    test_concurrent_process_does_not_reset_live_contexts();
    test_unknown_process_never_forces_spurious_reset();
    test_exited_process_stale_context_is_retired();
    test_exited_context_does_not_retire_live_concurrent_context();
    puts("generation lifecycle tests passed");
    return 0;
}
