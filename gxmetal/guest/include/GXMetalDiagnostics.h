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
#define GXMETAL_DIAGNOSTIC_VERSION UINT32_C(0x0001000a)
#define GXMETAL_DIAGNOSTIC_PIXEL_TYPES 18u
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
} GXMetalDiagnosticSnapshot;

int32_t GXMetalGetDiagnosticStatus(void);
int32_t GXMetalProbeTransport(void);
int32_t GXMetalCopyDiagnostics(GXMetalDiagnosticSnapshot *snapshot,
                               uint32_t snapshotBytes);

#endif
