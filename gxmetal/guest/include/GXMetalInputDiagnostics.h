/* SPDX-License-Identifier: MIT */

#ifndef GXMETAL_INPUT_DIAGNOSTICS_H
#define GXMETAL_INPUT_DIAGNOSTICS_H

#include <stdint.h>

#define GXMETAL_INPUT_TRACE_MAGIC UINT32_C(0x47584954) /* GXIT */
#define GXMETAL_INPUT_TRACE_VERSION UINT32_C(0x00010000)
#define GXMETAL_INPUT_TRACE_EVENT_CAPACITY 32u

enum GXMetalInputTraceEventKind {
    kGXMetalInputTraceCFMInitialize = 1,
    kGXMetalInputTraceCheckConfiguration = 2,
    kGXMetalInputTraceFindEnter = 3,
    kGXMetalInputTraceFindExit = 4,
    kGXMetalInputTraceDisposeEnter = 5,
    kGXMetalInputTraceDisposeExit = 6,
    kGXMetalInputTraceSetActiveEnter = 7,
    kGXMetalInputTraceSetActiveExit = 8,
    kGXMetalInputTraceStop = 9,
    kGXMetalInputTraceDeviceTickle = 10,
    kGXMetalInputTraceDriverTickle = 11,
    kGXMetalInputTraceReset = 12,
    kGXMetalInputTraceCreate = 13,
    kGXMetalInputTraceStaleCallback = 14
};

typedef struct GXMetalInputTraceEvent {
    uint32_t sequence;
    uint32_t kind;
    uint32_t ref_con;
    uint32_t argument;
    int32_t result;
    int32_t current_process_result;
    uint32_t current_process_high;
    uint32_t current_process_low;
    uint32_t owner_valid;
    int32_t owner_process_result;
    uint32_t owner_process_high;
    uint32_t owner_process_low;
    uint32_t find_count;
    uint32_t set_active_count;
    uint32_t device_tickle_count;
    uint32_t poll_count;
    uint32_t push_count;
} GXMetalInputTraceEvent;

typedef struct GXMetalInputTraceSnapshot {
    uint32_t magic;
    uint32_t version;
    uint32_t snapshot_bytes;
    uint32_t event_capacity;
    uint32_t event_sequence;
    uint32_t cfm_initialize_count;
    uint32_t check_configuration_count;
    uint32_t find_count;
    uint32_t dispose_count;
    uint32_t driver_tickle_count;
    uint32_t device_tickle_count;
    uint32_t set_active_count;
    uint32_t set_active_true_count;
    uint32_t set_active_false_count;
    uint32_t stop_count;
    uint32_t reset_count;
    uint32_t owner_handoff_count;
    uint32_t dead_owner_forget_count;
    uint32_t live_owner_dispose_count;
    uint32_t stale_callback_count;
    uint32_t current_process_query_count;
    uint32_t current_process_success_count;
    uint32_t current_process_failure_count;
    int32_t last_current_process_result;
    uint32_t last_current_process_high;
    uint32_t last_current_process_low;
    uint32_t owner_process_query_count;
    uint32_t owner_process_not_found_count;
    uint32_t owner_process_other_failure_count;
    int32_t last_owner_process_result;
    uint32_t device_new_attempt_count;
    uint32_t device_new_success_count;
    uint32_t device_dispose_count;
    uint32_t element_new_attempt_count;
    uint32_t element_new_success_count;
    uint32_t element_dispose_count;
    uint32_t bridge_resolve_attempt_count;
    uint32_t bridge_resolve_success_count;
    uint32_t bridge_resolve_failure_count;
    uint32_t bridge_close_count;
    uint32_t host_mode_enable_attempt_count;
    uint32_t host_mode_disable_attempt_count;
    uint32_t host_mode_success_count;
    uint32_t host_mode_failure_count;
    uint32_t timer_start_count;
    uint32_t timer_stop_count;
    uint32_t timer_poll_count;
    uint32_t tickle_only_diagnostic;
    uint32_t timer_start_suppressed_count;
    uint32_t poll_count;
    uint32_t active_poll_count;
    uint32_t inactive_poll_count;
    uint32_t host_event_read_attempt_count;
    uint32_t host_event_read_success_count;
    uint32_t host_event_read_failure_count;
    uint32_t fallback_button_read_count;
    uint32_t delta_x_push_count;
    uint32_t delta_y_push_count;
    uint32_t button_1_push_count;
    uint32_t button_2_push_count;
    uint32_t button_3_push_count;
    uint32_t push_failure_count;
    uint32_t cfrag_context_id;
    uint32_t cfrag_closure_id;
    uint32_t cfrag_connection_id;
    uint32_t trace_load_count;
    uint32_t trace_load_valid_count;
    uint32_t trace_persist_attempt_count;
    uint32_t trace_persist_success_count;
    uint32_t trace_persist_failure_count;
    GXMetalInputTraceEvent events[GXMETAL_INPUT_TRACE_EVENT_CAPACITY];
} GXMetalInputTraceSnapshot;

#endif
