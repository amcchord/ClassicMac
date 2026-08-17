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
    kGXMetalDiagnosticTransportConnectionFailed = 19
};

#define GXMETAL_DIAGNOSTIC_MAGIC UINT32_C(0x47584447) /* GXDG */
#define GXMETAL_DIAGNOSTIC_VERSION UINT32_C(0x00010000)
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
} GXMetalDiagnosticSnapshot;

int32_t GXMetalGetDiagnosticStatus(void);
int32_t GXMetalProbeTransport(void);
int32_t GXMetalCopyDiagnostics(GXMetalDiagnosticSnapshot *snapshot,
                               uint32_t snapshotBytes);

#endif
