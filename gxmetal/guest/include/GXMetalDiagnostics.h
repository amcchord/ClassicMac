#ifndef GXMETAL_DIAGNOSTICS_H
#define GXMETAL_DIAGNOSTICS_H

#include <stdint.h>

enum GXMetalDiagnosticStatus {
    kGXMetalDiagnosticNotLoaded = 0,
    kGXMetalDiagnosticInitializing = 1,
    kGXMetalDiagnosticRegistrationFailed = 2,
    kGXMetalDiagnosticRegistered = 3,
    kGXMetalDiagnosticCheckingDevice = 10,
    kGXMetalDiagnosticTransportUnavailable = 11,
    kGXMetalDiagnosticInvalidDevice = 12,
    kGXMetalDiagnosticDisplayRejected = 13,
    kGXMetalDiagnosticDeviceAccepted = 14,
    kGXMetalDiagnosticRegistryUnavailable = 15,
    kGXMetalDiagnosticRegistryReady = 16,
    kGXMetalDiagnosticTransportReady = 17,
    kGXMetalDiagnosticTransportConnecting = 18,
    kGXMetalDiagnosticTransportConnectionFailed = 19,
    kGXMetalDiagnosticCreatingContext = 20,
    kGXMetalDiagnosticContextInvalidArguments = 21,
    kGXMetalDiagnosticContextUnsupportedFlags = 22,
    kGXMetalDiagnosticContextDisplayRejected = 23,
    kGXMetalDiagnosticContextClipRejected = 24,
    kGXMetalDiagnosticContextOutOfMemory = 25,
    kGXMetalDiagnosticContextPacketFailed = 26,
    kGXMetalDiagnosticContextTransportFault = 27,
    kGXMetalDiagnosticContextMethodFailed = 28,
    kGXMetalDiagnosticContextReady = 29
};

enum GXMetalDisplayRejectReason {
    kGXMetalDisplayAccepted = 0,
    kGXMetalDisplayInvalidArguments = 1,
    kGXMetalDisplayInvalidRectangle = 2,
    kGXMetalDisplayInvalidGDevice = 3,
    kGXMetalDisplayInvalidPixMap = 4,
    kGXMetalDisplayUnsupportedPixelSize = 5,
    kGXMetalDisplayInvalidMemoryRowBytes = 6,
    kGXMetalDisplayUnsupportedMemoryPixelType = 7,
    kGXMetalDisplayUnsupportedDeviceType = 8,
    kGXMetalDisplayRowBytesTooSmall = 9,
    kGXMetalDisplayBeforeFramebuffer = 10,
    kGXMetalDisplayAfterFramebuffer = 11
};

#define GXMETAL_DIAGNOSTIC_MAGIC UINT32_C(0x47584447) /* GXDG */
#define GXMETAL_DIAGNOSTIC_VERSION UINT32_C(0x0001001E)
#define GXMETAL_DIAGNOSTIC_PIXEL_TYPES 18u
#define GXMETAL_DIAGNOSTIC_PRIVATE_PIXEL_TYPES 16u
#define GXMETAL_DIAGNOSTIC_ATI_DRAW47_WORDS 30u
#define GXMETAL_DIAGNOSTIC_ATI_STATE20_WORDS 256u
#define GXMETAL_DIAGNOSTIC_ATI_PIXEL21_WORDS 32u
#define GXMETAL_DIAGNOSTIC_ATI_FILL_METHODS 4u
#define GXMETAL_DIAGNOSTIC_ATI_GEOMETRY_METHODS 20u
#define GXMETAL_DIAGNOSTIC_ATI_GEOMETRY_ARGUMENTS 160u
#define GXMETAL_DIAGNOSTIC_ATI_CAPTURE_VERTICES 4u
#define GXMETAL_DIAGNOSTIC_ATI_CAPTURE_WORDS_PER_VERTEX 13u
#define GXMETAL_DIAGNOSTIC_ATI_DRAW50_WORDS 39u
#define GXMETAL_DIAGNOSTIC_ATI_DRAW60_WORDS 52u
#define GXMETAL_DIAGNOSTIC_ATI_DRAW48_WORDS 39u
#define GXMETAL_DIAGNOSTIC_ATI_METHODS 64u
#define GXMETAL_DIAGNOSTIC_ATI_METHOD28_29_ARGUMENTS 16u
#define GXMETAL_DIAGNOSTIC_ATI_CLIP_MARKER_VALUES 64u
#define GXMETAL_DIAGNOSTIC_ATI_VERTEX_COUNT_BUCKETS 9u
#define GXMETAL_DIAGNOSTIC_ATI_ANOMALY_VERTICES 3u
#define GXMETAL_DIAGNOSTIC_ATI_ANOMALY_WORDS 39u
#define GXMETAL_DIAGNOSTIC_ATI_GEOMETRY_BURST_CALLS 256u
#define GXMETAL_DIAGNOSTIC_ATI_METHOD4_ARGUMENTS 8u
#define GXMETAL_DIAGNOSTIC_ATI_METHOD4_SNAPSHOT_WORDS 6u
#define GXMETAL_DIAGNOSTIC_PROPERTY "AAPL,GXMetalEngineDiagnostic"

typedef struct GXMetalDiagnosticSnapshot {
    uint32_t magic;
    uint32_t version;
    int32_t status;
    int32_t registration_error;
    uint32_t initialize_count;
    uint32_t check_device_count;
    uint32_t device_type;
    uint32_t device_address;
    uint32_t pixmap_address;
    uint32_t base_address;
    uint32_t row_bytes;
    uint32_t pixel_size;
    uint32_t bounds_left;
    uint32_t bounds_top;
    uint32_t bounds_right;
    uint32_t bounds_bottom;
    uint32_t registry_framebuffer_address;
    uint32_t registry_framebuffer_bytes;
    uint32_t target_address;
    uint32_t target_end;
    uint32_t transport_features;
    uint32_t transport_status;
    uint32_t get_method_count;
    uint32_t method_mask;
    uint32_t gestalt_count;
    uint32_t last_gestalt_selector;
    uint32_t display_reject_reason;
    uint32_t draw_private_new_count;
    uint32_t context_flags;
    int32_t context_error;
    uint32_t context_width;
    uint32_t context_height;
    uint32_t context_row_bytes;
    uint32_t context_pixel_format;
    uint32_t context_framebuffer_offset;
    uint32_t context_clip_left;
    uint32_t context_clip_top;
    uint32_t context_clip_right;
    uint32_t context_clip_bottom;
    uint32_t resource_stage;
    uint32_t texture_new_count;
    uint32_t texture_delete_count;
    uint32_t color_table_new_count;
    uint32_t color_table_delete_count;
    uint32_t texture_bind_color_table_count;
    uint32_t last_texture_flags;
    uint32_t last_texture_pixel_type;
    uint32_t last_texture_width;
    uint32_t last_texture_height;
    uint32_t last_texture_levels;
    int32_t last_texture_error;
    uint32_t last_color_table_type;
    uint32_t last_color_table_transparent;
    int32_t last_color_table_error;
    int32_t last_texture_bind_error;
    uint32_t draw_method_stage;
    uint32_t set_float_count;
    uint32_t last_set_float_tag;
    uint32_t last_set_float_value;
    uint32_t set_int_count;
    uint32_t last_set_int_tag;
    uint32_t last_set_int_value;
    uint32_t set_ptr_count;
    uint32_t last_set_ptr_tag;
    uint32_t last_set_ptr_value;
    uint32_t get_float_count;
    uint32_t last_get_float_tag;
    uint32_t last_get_float_value;
    uint32_t get_int_count;
    uint32_t last_get_int_tag;
    uint32_t last_get_int_value;
    uint32_t get_ptr_count;
    uint32_t last_get_ptr_tag;
    uint32_t last_get_ptr_value;
    uint32_t render_start_count;
    uint32_t render_end_count;
    uint32_t render_abort_count;
    uint32_t flush_count;
    uint32_t sync_count;
    uint32_t draw_call_count;
    uint32_t last_draw_method;
    uint32_t draw_method_mask;
    uint32_t texture_new_success_by_type[GXMETAL_DIAGNOSTIC_PIXEL_TYPES];
    uint32_t texture_new_attempt_by_type[GXMETAL_DIAGNOSTIC_PIXEL_TYPES];
    uint32_t bitmap_new_success_by_type[GXMETAL_DIAGNOSTIC_PIXEL_TYPES];
    uint32_t bitmap_new_attempt_by_type[GXMETAL_DIAGNOSTIC_PIXEL_TYPES];
    uint32_t bitmap_new_count;
    uint32_t bitmap_delete_count;
    uint32_t bitmap_bind_color_table_count;
    uint32_t last_bitmap_flags;
    uint32_t last_bitmap_pixel_type;
    uint32_t last_bitmap_width;
    uint32_t last_bitmap_height;
    int32_t last_bitmap_error;
    int32_t last_bitmap_bind_error;
    uint32_t draw_tri_gouraud_count;
    uint32_t draw_tri_texture_count;
    uint32_t draw_tri_texture_reject_count;
    uint32_t draw_bitmap_count;
    uint32_t draw_bitmap_reject_count;
    uint32_t current_texture_resource_id;
    uint32_t current_texture_flags;
    uint32_t current_texture_pixel_type;
    uint32_t current_texture_width;
    uint32_t current_texture_height;
    uint32_t current_texture_palette_bound;
    uint32_t current_state_texture_op;
    uint32_t current_state_texture_filter;
    uint32_t current_state_blend;
    uint32_t current_state_z_function;
    uint32_t current_state_z_buffer_mask;
    uint32_t current_state_perspective_z;
    uint32_t current_state_fog_mode;
    uint32_t current_state_alpha_test;
    uint32_t current_state_alpha_reference;
    uint32_t current_state_wrap_u;
    uint32_t current_state_wrap_v;
    uint32_t last_texture_vertex_x;
    uint32_t last_texture_vertex_y;
    uint32_t last_texture_vertex_z;
    uint32_t last_texture_vertex_inv_w;
    uint32_t last_texture_vertex_a;
    uint32_t last_texture_vertex_u_over_w;
    uint32_t last_texture_vertex_v_over_w;
    uint32_t last_texture_vertex_kd_r;
    uint32_t last_texture_vertex_kd_g;
    uint32_t last_texture_vertex_kd_b;
    uint32_t set_texture_count;
    uint32_t set_texture_null_count;
    uint32_t set_texture_valid_count;
    uint32_t set_texture_invalid_count;
    uint32_t last_set_texture_magic;
    uint32_t draw_texture_null_state_count;
    uint32_t draw_texture_null_resource_count;
    uint32_t draw_texture_invalid_resource_count;
    uint32_t draw_texture_null_vertex_count;
    uint32_t draw_texture_unbound_fallback_count;
    uint32_t last_texture_image_width;
    uint32_t last_texture_image_height;
    uint32_t last_texture_image_row_bytes;
    uint32_t last_texture_image_pixels;
    uint32_t last_texture_output_pointer;
    uint32_t private_texture_attempt_count;
    uint32_t private_texture_success_count;
    uint32_t private_texture_refresh_check_count;
    uint32_t private_texture_refresh_upload_count;
    uint32_t private_texture_refresh_error_count;
    uint32_t private_texture_attempt_by_type[
        GXMETAL_DIAGNOSTIC_PRIVATE_PIXEL_TYPES];
    uint32_t private_texture_success_by_type[
        GXMETAL_DIAGNOSTIC_PRIVATE_PIXEL_TYPES];
    uint32_t private_texture_flags_or;
    uint32_t private_texture_attempt_nocopy_count;
    uint32_t private_texture_success_nocopy_count;
    uint32_t private_texture_success_small_count;
    uint32_t private_texture_success_large_count;
    uint32_t texture_reject_invalid_image_count;
    uint32_t texture_reject_unsupported_format_count;
    uint32_t texture_reject_transport_count;
    uint32_t rejected_int_state_count;
    uint32_t last_rejected_int_state_tag;
    uint32_t last_rejected_int_state_value;
    uint32_t draw_private_new_success_count;
    uint32_t draw_private_delete_count;
    uint32_t current_state_fog_color_a;
    uint32_t current_state_fog_color_r;
    uint32_t current_state_fog_color_g;
    uint32_t current_state_fog_color_b;
    uint32_t current_state_fog_start;
    uint32_t current_state_fog_end;
    uint32_t current_state_fog_density;
    uint32_t current_state_fog_max_depth;
    uint32_t ati_private_call_count;
    uint32_t ati_private_method_mask_low;
    uint32_t ati_private_method_mask_high;
    uint32_t ati_private_last_method;
    uint32_t ati_private_last_arg0;
    uint32_t ati_private_last_arg1;
    uint32_t ati_private_last_arg2;
    uint32_t ati_private_last_arg3;
    uint32_t ati_private_last_arg4;
    uint32_t ati_private_last_arg5;
    uint32_t ati_private_last_arg6;
    uint32_t ati_private_last_arg7;
    uint32_t ati_private_draw47_call_count;
    uint32_t ati_private_draw47_arg0;
    uint32_t ati_private_draw47_arg1;
    uint32_t ati_private_draw47_arg2;
    uint32_t ati_private_draw47_arg3;
    uint32_t ati_private_draw47_arg4;
    uint32_t ati_private_draw47_arg5;
    uint32_t ati_private_draw47_arg6;
    uint32_t ati_private_draw47_arg7;
    uint32_t ati_private_draw47_vertex_snapshot_valid;
    uint32_t ati_private_draw47_vertex_words[
        GXMETAL_DIAGNOSTIC_ATI_DRAW47_WORDS];
    uint32_t ati_private_state20_call_count;
    uint32_t ati_private_state20_arg0;
    uint32_t ati_private_state20_arg1;
    uint32_t ati_private_state20_arg2;
    uint32_t ati_private_state20_arg3;
    uint32_t ati_private_state20_arg4;
    uint32_t ati_private_state20_arg5;
    uint32_t ati_private_state20_arg6;
    uint32_t ati_private_state20_arg7;
    uint32_t ati_private_state20_snapshot_valid;
    uint32_t ati_private_state20_words[
        GXMETAL_DIAGNOSTIC_ATI_STATE20_WORDS];
    uint32_t ati_private_clear27_call_count;
    uint32_t ati_private_clear27_arg0;
    uint32_t ati_private_clear27_arg1;
    uint32_t ati_private_clear27_arg2;
    uint32_t ati_private_clear27_arg3;
    uint32_t ati_private_clear27_arg4;
    uint32_t ati_private_clear27_arg5;
    uint32_t ati_private_clear27_arg6;
    uint32_t ati_private_clear27_arg7;
    uint32_t ati_private_clear27_rect_snapshot_valid;
    uint32_t ati_private_clear27_rect_words[4];
    uint32_t ati_private_pixel21_call_count;
    uint32_t ati_private_pixel21_arg0;
    uint32_t ati_private_pixel21_arg1;
    uint32_t ati_private_pixel21_arg2;
    uint32_t ati_private_pixel21_arg3;
    uint32_t ati_private_pixel21_arg4;
    uint32_t ati_private_pixel21_arg5;
    uint32_t ati_private_pixel21_arg6;
    uint32_t ati_private_pixel21_arg7;
    uint32_t ati_private_pixel21_arg1_snapshot_valid;
    uint32_t ati_private_pixel21_arg1_words[
        GXMETAL_DIAGNOSTIC_ATI_PIXEL21_WORDS];
    uint32_t ati_private_pixel21_arg4_snapshot_valid;
    uint32_t ati_private_pixel21_arg4_words[
        GXMETAL_DIAGNOSTIC_ATI_PIXEL21_WORDS];
    uint32_t ati_private_draw49_call_count;
    uint32_t ati_private_draw49_last_vertex_count;
    uint32_t ati_private_draw49_max_vertex_count;
    uint32_t ati_private_draw50_call_count;
    uint32_t ati_private_draw50_last_vertex_count;
    uint32_t ati_private_draw50_max_vertex_count;
    uint32_t ati_private_draw49_last_primitive;
    uint32_t ati_private_draw49_primitive_mask;
    uint32_t ati_private_draw50_last_primitive;
    uint32_t ati_private_draw50_primitive_mask;
    uint32_t ati_private_draw51_call_count;
    uint32_t ati_private_draw51_last_vertex_count;
    uint32_t ati_private_draw51_max_vertex_count;
    uint32_t ati_private_draw51_last_primitive;
    uint32_t ati_private_draw51_primitive_mask;
    uint32_t ati_private_draw52_call_count;
    uint32_t ati_private_draw52_last_vertex_count;
    uint32_t ati_private_draw52_max_vertex_count;
    uint32_t ati_private_draw52_last_primitive;
    uint32_t ati_private_draw52_primitive_mask;
    uint32_t ati_private_fill41_44_call_count[
        GXMETAL_DIAGNOSTIC_ATI_FILL_METHODS];
    uint32_t ati_private_fill41_44_last_vertex_count[
        GXMETAL_DIAGNOSTIC_ATI_FILL_METHODS];
    uint32_t ati_private_fill41_44_max_vertex_count[
        GXMETAL_DIAGNOSTIC_ATI_FILL_METHODS];
    uint32_t ati_private_fill41_44_last_primitive[
        GXMETAL_DIAGNOSTIC_ATI_FILL_METHODS];
    uint32_t ati_private_fill41_44_primitive_mask[
        GXMETAL_DIAGNOSTIC_ATI_FILL_METHODS];
    uint32_t ati_private_geometry_call_count[
        GXMETAL_DIAGNOSTIC_ATI_GEOMETRY_METHODS];
    uint32_t ati_private_geometry_last_args[
        GXMETAL_DIAGNOSTIC_ATI_GEOMETRY_ARGUMENTS];
    uint32_t ati_private_draw50_vertex_snapshot_valid_mask;
    uint32_t ati_private_draw50_vertex_words[
        GXMETAL_DIAGNOSTIC_ATI_DRAW50_WORDS];
    uint32_t ati_private_draw60_pointer_snapshot_valid;
    uint32_t ati_private_draw60_pointer_count;
    uint32_t ati_private_draw60_vertex_snapshot_valid_mask;
    uint32_t ati_private_draw60_vertex_pointers[
        GXMETAL_DIAGNOSTIC_ATI_CAPTURE_VERTICES];
    uint32_t ati_private_draw60_vertex_words[
        GXMETAL_DIAGNOSTIC_ATI_DRAW60_WORDS];
    uint32_t ati_private_draw48_vertex_snapshot_valid_mask;
    uint32_t ati_private_draw48_vertex_words[
        GXMETAL_DIAGNOSTIC_ATI_DRAW48_WORDS];
    uint32_t ati_private_draw60_zero_clip_marker_call_count;
    uint32_t ati_private_draw60_nonzero_clip_marker_call_count;
    uint32_t ati_private_draw60_clip_marker_or;
    uint32_t ati_private_draw60_clip_marker_low_value_count[
        GXMETAL_DIAGNOSTIC_ATI_CLIP_MARKER_VALUES];
    uint32_t ati_private_draw60_clip_marker_high_value_call_count;
    uint32_t ati_private_draw60_vertex_count_buckets[
        GXMETAL_DIAGNOSTIC_ATI_VERTEX_COUNT_BUCKETS];
    uint32_t ati_private_draw60_nonzero_last_args[8];
    uint32_t ati_private_draw60_nonzero_pointer_snapshot_valid;
    uint32_t ati_private_draw60_nonzero_pointer_count;
    uint32_t ati_private_draw60_nonzero_vertex_snapshot_valid_mask;
    uint32_t ati_private_draw60_nonzero_vertex_pointers[
        GXMETAL_DIAGNOSTIC_ATI_CAPTURE_VERTICES];
    uint32_t ati_private_draw60_nonzero_vertex_words[
        GXMETAL_DIAGNOSTIC_ATI_DRAW60_WORDS];
    uint32_t ati_private_method_call_count[
        GXMETAL_DIAGNOSTIC_ATI_METHODS];
    uint32_t ati_private_method28_29_last_args[
        GXMETAL_DIAGNOSTIC_ATI_METHOD28_29_ARGUMENTS];
    uint32_t ati_private_frame_sequence;
    uint32_t ati_private_state20_dirty_mask_or;
    uint32_t ati_private_state20_word53_last;
    uint32_t ati_private_state20_word53_or;
    uint32_t ati_private_state20_word53_change_count;
    uint32_t ati_private_state20_word53_nonzero_call_count;
    uint32_t ati_private_state20_word53_first_nonzero_frame;
    uint32_t ati_private_context_resolve_count;
    uint32_t ati_private_context_fallback_count;
    uint32_t ati_private_context_last_renderer;
    uint32_t ati_private_context_last_draw_context;
    uint32_t ati_private_draw48_vertex_count_buckets[
        GXMETAL_DIAGNOSTIC_ATI_VERTEX_COUNT_BUCKETS];
    uint32_t ati_private_draw48_max_vertex_count;
    uint32_t ati_private_draw48_invalid_vertex_count_call_count;
    uint32_t ati_private_draw50_pointer_call_count;
    uint32_t ati_private_draw50_strip_call_count;
    uint32_t ati_private_geometry_triangle_attempt_count[
        GXMETAL_DIAGNOSTIC_ATI_GEOMETRY_METHODS];
    uint32_t ati_private_geometry_triangle_queued_count[
        GXMETAL_DIAGNOSTIC_ATI_GEOMETRY_METHODS];
    uint32_t ati_private_geometry_triangle_rejected_count[
        GXMETAL_DIAGNOSTIC_ATI_GEOMETRY_METHODS];
    uint32_t ati_private_geometry_input_rejected_call_count[
        GXMETAL_DIAGNOSTIC_ATI_GEOMETRY_METHODS];
    uint32_t ati_private_geometry_anomaly_count;
    uint32_t ati_private_geometry_anomaly_flags_or;
    uint32_t ati_private_geometry_first_anomaly_method;
    uint32_t ati_private_geometry_first_anomaly_frame;
    uint32_t ati_private_geometry_first_anomaly_flags;
    uint32_t ati_private_geometry_first_anomaly_vertex_addresses[
        GXMETAL_DIAGNOSTIC_ATI_ANOMALY_VERTICES];
    uint32_t ati_private_geometry_first_anomaly_vertex_words[
        GXMETAL_DIAGNOSTIC_ATI_ANOMALY_WORDS];
    uint32_t ati_private_geometry_current_frame_call_count[
        GXMETAL_DIAGNOSTIC_ATI_GEOMETRY_METHODS];
    uint32_t ati_private_geometry_max_frame_call_count[
        GXMETAL_DIAGNOSTIC_ATI_GEOMETRY_METHODS];
    uint32_t ati_private_geometry_max_frame_call_frame[
        GXMETAL_DIAGNOSTIC_ATI_GEOMETRY_METHODS];
    uint32_t ati_private_geometry_first_burst_method;
    uint32_t ati_private_geometry_first_burst_frame;
    uint32_t ati_private_geometry_first_burst_call_count;
    uint32_t ati_private_geometry_first_burst_viewport_width;
    uint32_t ati_private_geometry_first_burst_viewport_height;
    uint32_t ati_private_geometry_first_burst_vertex_addresses[
        GXMETAL_DIAGNOSTIC_ATI_ANOMALY_VERTICES];
    uint32_t ati_private_geometry_first_burst_vertex_words[
        GXMETAL_DIAGNOSTIC_ATI_ANOMALY_WORDS];
    uint32_t ati_private_texture_update_arg0;
    uint32_t ati_private_texture_update_arg1;
    uint32_t ati_private_texture_update_arg2;
    uint32_t ati_private_texture_update_arg3;
    uint32_t ati_private_texture_update_image_snapshot_valid;
    uint32_t ati_private_texture_update_image_pixmap;
    uint32_t ati_private_texture_update_image_width;
    uint32_t ati_private_texture_update_image_height;
    uint32_t ati_private_texture_update_image_row_bytes;
    uint32_t ati_private_texture_update_texture_snapshot_valid;
    uint32_t ati_private_texture_update_texture_magic;
    uint32_t ati_private_texture_update_resource_id;
    uint32_t ati_private_texture_update_source_pixel_type;
    uint32_t ati_private_texture_update_pixel_format;
    uint32_t ati_private_texture_update_texture_width;
    uint32_t ati_private_texture_update_texture_height;
    uint32_t ati_private_texture_update_texture_levels;
    uint32_t ati_private_texture_update_source_flags;
    uint32_t ati_private_texture_update_access_active;
    uint32_t ati_private_texture_update_stage;
    uint32_t ati_private_texture_update_reject_reason;
    int32_t ati_private_texture_update_result;
    uint32_t ati_private_method4_args[
        GXMETAL_DIAGNOSTIC_ATI_METHOD4_ARGUMENTS];
    uint32_t ati_private_method4_before_snapshot_valid;
    uint32_t ati_private_method4_before_snapshot_words[
        GXMETAL_DIAGNOSTIC_ATI_METHOD4_SNAPSHOT_WORDS];
    uint32_t ati_private_method4_after_snapshot_valid;
    uint32_t ati_private_method4_after_snapshot_words[
        GXMETAL_DIAGNOSTIC_ATI_METHOD4_SNAPSHOT_WORDS];
    int32_t ati_private_method4_result;
} GXMetalDiagnosticSnapshot;

int32_t GXMetalGetDiagnosticStatus(void);
int32_t GXMetalProbeTransport(void);
int32_t GXMetalCopyDiagnostics(GXMetalDiagnosticSnapshot *snapshot,
                               uint32_t snapshotBytes);

#endif
