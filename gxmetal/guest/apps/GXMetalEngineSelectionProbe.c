/*
 * GXMetal RAVE Selection Probe
 *
 * Records the RAVE engine order and QuickDraw 3D interactive-renderer choice
 * without calling QAEngineEnable or QAEngineDisable. The explicit BestChoice
 * request is confined to a newly-created renderer object.
 */

#include <Dialogs.h>
#include <Files.h>
#include <Folders.h>
#include <Fonts.h>
#include <Memory.h>
#include <QD3D.h>
#include <QD3DAcceleration.h>
#include <QD3DCamera.h>
#include <QD3DDrawContext.h>
#include <QD3DRenderer.h>
#include <QD3DView.h>
#include <Quickdraw.h>
#include <RAVE.h>
#include <TextEdit.h>
#include <Windows.h>

#include <stdint.h>
#include <string.h>

#include "GXMetalEngineSelectionProbeLogic.h"
#include "GXMetalVersion.h"

#define GXMETAL_SELECTION_ALERT_ID 128
#define GXMETAL_SELECTION_MAX_ENGINES 16u
#define GXMETAL_SELECTION_REPORT_CAPACITY 16384u

/* QAInit and QAExit remain exported by the classic RAVE manager even though
 * Universal Interfaces 3.4 omits their declarations. */
extern TQAError QAInit(void);
extern void QAExit(void);

static const unsigned char kGXMetalSelectionResultsName[] = {
    30, 'G', 'X', 'M', 'e', 't', 'a', 'l', ' ', 'R', 'A', 'V', 'E', ' ',
    'S', 'e', 'l', 'e', 'c', 't', 'i', 'o', 'n', ' ', 'R', 'e', 's', 'u',
    'l', 't', 's'
};

static const unsigned char kGXMetalSelectionWindowTitle[] = {
    22, 'G', 'X', 'M', 'e', 't', 'a', 'l', ' ', 'R', 'A', 'V', 'E', ' ',
    'S', 'e', 'l', 'e', 'c', 't', 'i', 'o', 'n'
};

typedef struct GXMetalEngineRecord {
    char name[64];
    uint32_t order;
    uint32_t nameLength;
    uint32_t vendorID;
    uint32_t engineID;
    uint32_t revision;
    uint32_t optionalFeatures;
    uint32_t fastFeatures;
    int32_t nameLengthStatus;
    int32_t nameStatus;
    int32_t vendorStatus;
    int32_t engineStatus;
    int32_t revisionStatus;
    int32_t optionalStatus;
    int32_t fastStatus;
} GXMetalEngineRecord;

typedef struct GXMetalInventoryRecord {
    uint32_t totalCount;
    uint32_t storedCount;
    int truncated;
    GXMetalEngineRecord engines[GXMETAL_SELECTION_MAX_ENGINES];
} GXMetalInventoryRecord;

typedef struct GXMetalRendererRecord {
    int rendererCreated;
    int setPreferenceAttempted;
    int selectionAttempted;
    int viewCreated;
    int drawContextCreated;
    int cameraCreated;
    int requestedVendor;
    int requestedEngine;
    int32_t setPreferenceStatus;
    int32_t getPreferenceStatus;
    int32_t effectiveVendor;
    int32_t effectiveEngine;
    int32_t setRendererStatus;
    int32_t setDrawContextStatus;
    int32_t setCameraStatus;
    int32_t startRenderingStatus;
    int32_t countContextsStatus;
    int32_t getContextsStatus;
    int32_t endRenderingStatus;
    uint32_t reportedContextCount;
    GXMetalInventoryRecord selectedEngines;
} GXMetalRendererRecord;

typedef struct GXMetalSelectionReport {
    int32_t qaInitStatus;
    int32_t q3InitializeStatus;
    int windowCreated;
    GXMetalInventoryRecord inventory;
    GXMetalRendererRecord untouched;
    GXMetalRendererRecord bestChoice;
} GXMetalSelectionReport;

static void GXMetalSelectionInitToolbox(void)
{
    InitGraf(&qd.thePort);
    InitFonts();
    InitWindows();
    InitMenus();
    TEInit();
    InitDialogs(NULL);
    InitCursor();
}

static void GXMetalSelectionCStringToPascal(const char *source,
                                            Str255 destination)
{
    size_t length = strlen(source);

    if (length > 255) {
        length = 255;
    }
    destination[0] = (unsigned char)length;
    memcpy(destination + 1, source, length);
}

static void GXMetalSelectionInitEngineRecord(GXMetalEngineRecord *record,
                                             uint32_t order)
{
    memset(record, 0, sizeof(*record));
    strcpy(record->name, "<unavailable>");
    record->order = order;
    record->nameLengthStatus = kQAError;
    record->nameStatus = kQAError;
    record->vendorStatus = kQAError;
    record->engineStatus = kQAError;
    record->revisionStatus = kQAError;
    record->optionalStatus = kQAError;
    record->fastStatus = kQAError;
}

static void GXMetalSelectionSnapshotEngine(TQAEngine *engine,
                                           uint32_t order,
                                           GXMetalEngineRecord *record)
{
    unsigned long value = 0;

    GXMetalSelectionInitEngineRecord(record, order);
    record->nameLengthStatus = (int32_t)QAEngineGestalt(
        engine, kQAGestalt_ASCIINameLength, &value);
    record->nameLength = (uint32_t)value;
    if (record->nameLengthStatus == kQANoErr &&
        value < sizeof(record->name)) {
        memset(record->name, 0, sizeof(record->name));
        record->nameStatus = (int32_t)QAEngineGestalt(
            engine, kQAGestalt_ASCIIName, record->name);
        if (record->nameStatus != kQANoErr) {
            strcpy(record->name, "<unavailable>");
        }
    } else {
        record->nameStatus = kQAParamErr;
    }

    value = 0;
    record->vendorStatus = (int32_t)QAEngineGestalt(
        engine, kQAGestalt_VendorID, &value);
    record->vendorID = (uint32_t)value;
    value = 0;
    record->engineStatus = (int32_t)QAEngineGestalt(
        engine, kQAGestalt_EngineID, &value);
    record->engineID = (uint32_t)value;
    value = 0;
    record->revisionStatus = (int32_t)QAEngineGestalt(
        engine, kQAGestalt_Revision, &value);
    record->revision = (uint32_t)value;
    value = 0;
    record->optionalStatus = (int32_t)QAEngineGestalt(
        engine, kQAGestalt_OptionalFeatures, &value);
    record->optionalFeatures = (uint32_t)value;
    value = 0;
    record->fastStatus = (int32_t)QAEngineGestalt(
        engine, kQAGestalt_FastFeatures, &value);
    record->fastFeatures = (uint32_t)value;
}

static void GXMetalSelectionRecordInventory(const TQADevice *device,
                                            GXMetalInventoryRecord *record)
{
    TQAEngine *engine;

    memset(record, 0, sizeof(*record));
    engine = QADeviceGetFirstEngine(device);
    while (engine != NULL) {
        if (record->storedCount < GXMETAL_SELECTION_MAX_ENGINES) {
            GXMetalSelectionSnapshotEngine(
                engine, record->totalCount,
                &record->engines[record->storedCount]);
            record->storedCount++;
        } else {
            record->truncated = 1;
        }
        record->totalCount++;
        engine = QADeviceGetNextEngine(device, engine);
    }
}

static void GXMetalSelectionInitRendererRecord(
    GXMetalRendererRecord *record)
{
    memset(record, 0, sizeof(*record));
    record->setPreferenceStatus = kQ3Failure;
    record->getPreferenceStatus = kQ3Failure;
    record->setRendererStatus = kQ3Failure;
    record->setDrawContextStatus = kQ3Failure;
    record->setCameraStatus = kQ3Failure;
    record->startRenderingStatus = kQ3Failure;
    record->countContextsStatus = kQ3Failure;
    record->getContextsStatus = kQ3Failure;
    record->endRenderingStatus = kQ3ViewStatusError;
}

static TQ3DrawContextObject GXMetalSelectionCreateDrawContext(WindowPtr window)
{
    TQ3MacDrawContextData data;

    memset(&data, 0, sizeof(data));
    data.drawContextData.clearImageMethod = kQ3ClearMethodWithColor;
    data.drawContextData.clearImageColor.a = 1.0f;
    data.drawContextData.clearImageColor.r = 0.0f;
    data.drawContextData.clearImageColor.g = 0.0f;
    data.drawContextData.clearImageColor.b = 0.0f;
    data.drawContextData.paneState = kQ3False;
    data.drawContextData.maskState = kQ3False;
    data.drawContextData.doubleBufferState = kQ3False;
    data.window = (CWindowPtr)window;
    data.library = kQ3Mac2DLibraryQuickDraw;
    data.viewPort = NULL;
    data.grafPort = (CGrafPtr)window;
    return Q3MacDrawContext_New(&data);
}

static TQ3CameraObject GXMetalSelectionCreateCamera(void)
{
    TQ3ViewAngleAspectCameraData data;

    memset(&data, 0, sizeof(data));
    data.cameraData.placement.cameraLocation.x = 0.0f;
    data.cameraData.placement.cameraLocation.y = 0.0f;
    data.cameraData.placement.cameraLocation.z = 5.0f;
    data.cameraData.placement.pointOfInterest.x = 0.0f;
    data.cameraData.placement.pointOfInterest.y = 0.0f;
    data.cameraData.placement.pointOfInterest.z = 0.0f;
    data.cameraData.placement.upVector.x = 0.0f;
    data.cameraData.placement.upVector.y = 1.0f;
    data.cameraData.placement.upVector.z = 0.0f;
    data.cameraData.range.hither = 0.1f;
    data.cameraData.range.yon = 100.0f;
    data.cameraData.viewPort.origin.x = -1.0f;
    data.cameraData.viewPort.origin.y = 1.0f;
    data.cameraData.viewPort.width = 2.0f;
    data.cameraData.viewPort.height = 2.0f;
    data.fov = 0.75f;
    data.aspectRatioXToY = 1.0f;
    return Q3ViewAngleAspectCamera_New(&data);
}

static void GXMetalSelectionRAVEContextDestroyed(
    TQ3RendererObject renderer)
{
    (void)renderer;
}

static void GXMetalSelectionSnapshotSelectedContexts(
    TQ3RendererObject renderer, GXMetalRendererRecord *record)
{
    TQADrawContext **contexts = NULL;
    TQAEngine **engines = NULL;
    unsigned long count = 0;
    unsigned long returnedCount;
    unsigned long readableCount;
    unsigned long index;

    record->countContextsStatus = (int32_t)
        Q3InteractiveRenderer_CountRAVEDrawContexts(renderer, &count);
    record->reportedContextCount = (uint32_t)count;
    if (record->countContextsStatus != kQ3Success || count == 0 ||
        count > 64) {
        return;
    }

    contexts = (TQADrawContext **)NewPtrClear(
        (Size)(count * sizeof(*contexts)));
    engines = (TQAEngine **)NewPtrClear((Size)(count * sizeof(*engines)));
    if (contexts == NULL || engines == NULL) {
        if (contexts != NULL) {
            DisposePtr((Ptr)contexts);
        }
        if (engines != NULL) {
            DisposePtr((Ptr)engines);
        }
        return;
    }

    returnedCount = count;
    record->getContextsStatus = (int32_t)
        Q3InteractiveRenderer_GetRAVEDrawContexts(
            renderer, contexts, engines, &returnedCount,
            GXMetalSelectionRAVEContextDestroyed);
    if (record->getContextsStatus == kQ3Success) {
        record->selectedEngines.totalCount = (uint32_t)returnedCount;
        readableCount = returnedCount <= count ? returnedCount : count;
        if (returnedCount > count) {
            record->selectedEngines.truncated = 1;
        }
        for (index = 0; index < readableCount; index++) {
            if (index < GXMETAL_SELECTION_MAX_ENGINES &&
                engines[index] != NULL) {
                GXMetalSelectionSnapshotEngine(
                    engines[index], (uint32_t)index,
                    &record->selectedEngines.engines[
                        record->selectedEngines.storedCount]);
                record->selectedEngines.storedCount++;
            } else if (index >= GXMETAL_SELECTION_MAX_ENGINES) {
                record->selectedEngines.truncated = 1;
            }
        }
    }
    DisposePtr((Ptr)engines);
    DisposePtr((Ptr)contexts);
}

static void GXMetalSelectionProbeRenderer(TQ3RendererObject renderer,
                                          WindowPtr window,
                                          GXMetalRendererRecord *record)
{
    TQ3ViewObject view = NULL;
    TQ3DrawContextObject drawContext = NULL;
    TQ3CameraObject camera = NULL;

    record->selectionAttempted = 1;
    view = Q3View_New();
    record->viewCreated = view != NULL;
    drawContext = GXMetalSelectionCreateDrawContext(window);
    record->drawContextCreated = drawContext != NULL;
    camera = GXMetalSelectionCreateCamera();
    record->cameraCreated = camera != NULL;
    if (view != NULL && drawContext != NULL && camera != NULL) {
        record->setRendererStatus = (int32_t)Q3View_SetRenderer(
            view, renderer);
        record->setDrawContextStatus = (int32_t)Q3View_SetDrawContext(
            view, drawContext);
        record->setCameraStatus = (int32_t)Q3View_SetCamera(view, camera);
        if (record->setRendererStatus == kQ3Success &&
            record->setDrawContextStatus == kQ3Success &&
            record->setCameraStatus == kQ3Success) {
            record->startRenderingStatus = (int32_t)
                Q3View_StartRendering(view);
            if (record->startRenderingStatus == kQ3Success) {
                GXMetalSelectionSnapshotSelectedContexts(renderer, record);
                record->endRenderingStatus = (int32_t)
                    Q3View_EndRendering(view);
            }
        }
    }
    if (view != NULL) {
        (void)Q3Object_Dispose((TQ3Object)view);
    }
    if (camera != NULL) {
        (void)Q3Object_Dispose((TQ3Object)camera);
    }
    if (drawContext != NULL) {
        (void)Q3Object_Dispose((TQ3Object)drawContext);
    }
}

static void GXMetalSelectionCreateRendererRecord(
    WindowPtr window, int bestChoice, GXMetalRendererRecord *record)
{
    TQ3RendererObject renderer;
    long vendor = 0;
    long engine = 0;

    GXMetalSelectionInitRendererRecord(record);
    renderer = Q3Renderer_NewFromType(kQ3RendererTypeInteractive);
    record->rendererCreated = renderer != NULL;
    if (renderer == NULL) {
        return;
    }
    if (bestChoice) {
        record->setPreferenceAttempted = 1;
        record->requestedVendor = kQAVendor_BestChoice;
        record->requestedEngine = kQAVendor_BestChoice;
        record->setPreferenceStatus = (int32_t)
            Q3InteractiveRenderer_SetPreferences(
                renderer, kQAVendor_BestChoice, kQAVendor_BestChoice);
    }
    record->getPreferenceStatus = (int32_t)
        Q3InteractiveRenderer_GetPreferences(renderer, &vendor, &engine);
    record->effectiveVendor = (int32_t)vendor;
    record->effectiveEngine = (int32_t)engine;
    if (window != NULL) {
        GXMetalSelectionProbeRenderer(renderer, window, record);
    }
    (void)Q3Object_Dispose((TQ3Object)renderer);
}

static void GXMetalSelectionJSONStatus(GXMetalProbeTextBuffer *json,
                                       int32_t status)
{
    gxmetal_probe_i32(json, status);
}

static void GXMetalSelectionJSONHexField(GXMetalProbeTextBuffer *json,
                                         const char *key, uint32_t value)
{
    gxmetal_probe_json_string(json, key);
    gxmetal_probe_text(json, ":{");
    gxmetal_probe_text(json, "\"decimal\":");
    gxmetal_probe_u32(json, value);
    gxmetal_probe_text(json, ",\"hex\":\"");
    gxmetal_probe_hex32(json, value);
    gxmetal_probe_text(json, "\"}");
}

static void GXMetalSelectionJSONEngine(GXMetalProbeTextBuffer *json,
                                       const GXMetalEngineRecord *record)
{
    gxmetal_probe_text(json, "{\"order\":");
    gxmetal_probe_u32(json, record->order);
    gxmetal_probe_text(json, ",\"name\":");
    gxmetal_probe_json_string(json, record->name);
    gxmetal_probe_text(json, ",\"name_length\":");
    gxmetal_probe_u32(json, record->nameLength);
    gxmetal_probe_text(json, ",\"statuses\":{");
    gxmetal_probe_text(json, "\"name_length\":");
    GXMetalSelectionJSONStatus(json, record->nameLengthStatus);
    gxmetal_probe_text(json, ",\"name\":");
    GXMetalSelectionJSONStatus(json, record->nameStatus);
    gxmetal_probe_text(json, ",\"vendor\":");
    GXMetalSelectionJSONStatus(json, record->vendorStatus);
    gxmetal_probe_text(json, ",\"engine\":");
    GXMetalSelectionJSONStatus(json, record->engineStatus);
    gxmetal_probe_text(json, ",\"revision\":");
    GXMetalSelectionJSONStatus(json, record->revisionStatus);
    gxmetal_probe_text(json, ",\"optional\":");
    GXMetalSelectionJSONStatus(json, record->optionalStatus);
    gxmetal_probe_text(json, ",\"fast\":");
    GXMetalSelectionJSONStatus(json, record->fastStatus);
    gxmetal_probe_text(json, "},");
    GXMetalSelectionJSONHexField(json, "vendor", record->vendorID);
    gxmetal_probe_text_char(json, ',');
    GXMetalSelectionJSONHexField(json, "engine", record->engineID);
    gxmetal_probe_text_char(json, ',');
    GXMetalSelectionJSONHexField(json, "revision", record->revision);
    gxmetal_probe_text_char(json, ',');
    GXMetalSelectionJSONHexField(
        json, "optional_features", record->optionalFeatures);
    gxmetal_probe_text_char(json, ',');
    GXMetalSelectionJSONHexField(json, "fast_features",
                                 record->fastFeatures);
    gxmetal_probe_text_char(json, '}');
}

static void GXMetalSelectionJSONInventory(
    GXMetalProbeTextBuffer *json, const GXMetalInventoryRecord *record)
{
    uint32_t index;

    gxmetal_probe_text(json, "{\"total_count\":");
    gxmetal_probe_u32(json, record->totalCount);
    gxmetal_probe_text(json, ",\"stored_count\":");
    gxmetal_probe_u32(json, record->storedCount);
    gxmetal_probe_text(json, ",\"truncated\":");
    gxmetal_probe_text(json, record->truncated ? "true" : "false");
    gxmetal_probe_text(json, ",\"engines\":[");
    for (index = 0; index < record->storedCount; index++) {
        if (index != 0) {
            gxmetal_probe_text_char(json, ',');
        }
        GXMetalSelectionJSONEngine(json, &record->engines[index]);
    }
    gxmetal_probe_text(json, "]}");
}

static void GXMetalSelectionJSONRenderer(
    GXMetalProbeTextBuffer *json, const GXMetalRendererRecord *record)
{
    gxmetal_probe_text(json, "{\"renderer_created\":");
    gxmetal_probe_text(json, record->rendererCreated ? "true" : "false");
    gxmetal_probe_text(json, ",\"set_preference_attempted\":");
    gxmetal_probe_text(json,
                       record->setPreferenceAttempted ? "true" : "false");
    gxmetal_probe_text(json, ",\"requested_preference\":");
    if (record->setPreferenceAttempted) {
        gxmetal_probe_text(json, "{\"vendor\":");
        gxmetal_probe_i32(json, (int32_t)record->requestedVendor);
        gxmetal_probe_text(json, ",\"engine\":");
        gxmetal_probe_i32(json, (int32_t)record->requestedEngine);
        gxmetal_probe_text_char(json, '}');
    } else {
        gxmetal_probe_text(json, "null");
    }
    gxmetal_probe_text(json, ",\"set_preference_status\":");
    if (record->setPreferenceAttempted) {
        GXMetalSelectionJSONStatus(json, record->setPreferenceStatus);
    } else {
        gxmetal_probe_text(json, "null");
    }
    gxmetal_probe_text(json, ",\"get_preference_status\":");
    GXMetalSelectionJSONStatus(json, record->getPreferenceStatus);
    gxmetal_probe_text(json, ",\"effective_preference\":{");
    gxmetal_probe_text(json, "\"vendor\":");
    gxmetal_probe_i32(json, record->effectiveVendor);
    gxmetal_probe_text(json, ",\"engine\":");
    gxmetal_probe_i32(json, record->effectiveEngine);
    gxmetal_probe_text(json, "},\"selection_attempted\":");
    gxmetal_probe_text(json, record->selectionAttempted ? "true" : "false");
    gxmetal_probe_text(json, ",\"setup\":{");
    gxmetal_probe_text(json, "\"view_created\":");
    gxmetal_probe_text(json, record->viewCreated ? "true" : "false");
    gxmetal_probe_text(json, ",\"draw_context_created\":");
    gxmetal_probe_text(json,
                       record->drawContextCreated ? "true" : "false");
    gxmetal_probe_text(json, ",\"camera_created\":");
    gxmetal_probe_text(json, record->cameraCreated ? "true" : "false");
    gxmetal_probe_text(json, ",\"set_renderer_status\":");
    GXMetalSelectionJSONStatus(json, record->setRendererStatus);
    gxmetal_probe_text(json, ",\"set_draw_context_status\":");
    GXMetalSelectionJSONStatus(json, record->setDrawContextStatus);
    gxmetal_probe_text(json, ",\"set_camera_status\":");
    GXMetalSelectionJSONStatus(json, record->setCameraStatus);
    gxmetal_probe_text(json, ",\"start_rendering_status\":");
    GXMetalSelectionJSONStatus(json, record->startRenderingStatus);
    gxmetal_probe_text(json, ",\"end_rendering_status\":");
    GXMetalSelectionJSONStatus(json, record->endRenderingStatus);
    gxmetal_probe_text(json, "},\"rave_contexts\":{");
    gxmetal_probe_text(json, "\"count_status\":");
    GXMetalSelectionJSONStatus(json, record->countContextsStatus);
    gxmetal_probe_text(json, ",\"get_status\":");
    GXMetalSelectionJSONStatus(json, record->getContextsStatus);
    gxmetal_probe_text(json, ",\"reported_count\":");
    gxmetal_probe_u32(json, record->reportedContextCount);
    gxmetal_probe_text(json, ",\"selected_engines\":");
    GXMetalSelectionJSONInventory(json, &record->selectedEngines);
    gxmetal_probe_text(json, "}}");
}

static void GXMetalSelectionBuildJSON(const GXMetalSelectionReport *report,
                                      char *result, size_t capacity,
                                      int *truncated)
{
    GXMetalProbeTextBuffer json;

    gxmetal_probe_text_init(&json, result, capacity);
    gxmetal_probe_text(&json,
        "{\"schema\":1,\"probe\":\"GXMetal RAVE Selection\","
        "\"version\":\"");
    gxmetal_probe_text(&json, GXMETAL_PRODUCT_VERSION_STRING);
    gxmetal_probe_text(&json,
        "\",\"global_engine_enablement_changed\":false,"
        "\"qa_engine_enable_called\":false,"
        "\"qa_engine_disable_called\":false,\"qa_init_status\":");
    GXMetalSelectionJSONStatus(&json, report->qaInitStatus);
    gxmetal_probe_text(&json, ",\"q3_initialize_status\":");
    GXMetalSelectionJSONStatus(&json, report->q3InitializeStatus);
    gxmetal_probe_text(&json, ",\"window_created\":");
    gxmetal_probe_text(&json, report->windowCreated ? "true" : "false");
    gxmetal_probe_text(&json, ",\"main_display_inventory\":");
    GXMetalSelectionJSONInventory(&json, &report->inventory);
    gxmetal_probe_text(&json, ",\"untouched_renderer\":");
    GXMetalSelectionJSONRenderer(&json, &report->untouched);
    gxmetal_probe_text(&json, ",\"best_choice_renderer\":");
    GXMetalSelectionJSONRenderer(&json, &report->bestChoice);
    gxmetal_probe_text(&json, "}\n");
    *truncated = json.truncated;
}

static OSErr GXMetalSelectionWriteResults(const char *result)
{
    FSSpec file;
    short volume = 0;
    long directory = 0;
    short refNum = -1;
    long length = (long)strlen(result);
    OSErr error;

    error = FindFolder(kOnSystemDisk, kPreferencesFolderType, false,
                       &volume, &directory);
    if (error != noErr) {
        return error;
    }
    (void)FSMakeFSSpec(volume, directory, kGXMetalSelectionResultsName,
                       &file);
    (void)FSpDelete(&file);
    error = FSpCreate(&file, 'GXMS', 'TEXT', smSystemScript);
    if (error != noErr) {
        return error;
    }
    error = FSpOpenDF(&file, fsWrPerm, &refNum);
    if (error != noErr) {
        return error;
    }
    error = FSWrite(refNum, &length, result);
    if (error == noErr) {
        error = SetEOF(refNum, length);
    }
    (void)FSClose(refNum);
    return error;
}

static void GXMetalSelectionShowResult(Boolean success)
{
    Str255 message;
    Str255 empty = {0};

    GXMetalSelectionCStringToPascal(success ?
        "GXMetal RAVE selection inventory was written to Preferences."
        : "GXMetal RAVE selection could not write a complete result.",
        message);
    ParamText(message, empty, empty, empty);
    (void)StopAlert(GXMETAL_SELECTION_ALERT_ID, NULL);
}

int main(void)
{
    GXMetalSelectionReport report;
    TQADevice device;
    Rect windowRect;
    WindowPtr window = NULL;
    char *json = NULL;
    OSErr writeError = noErr;
    int jsonTruncated = 0;
    int raveInitialized = 0;
    int qd3dInitialized = 0;

    GXMetalSelectionInitToolbox();
    memset(&report, 0, sizeof(report));
    GXMetalSelectionInitRendererRecord(&report.untouched);
    GXMetalSelectionInitRendererRecord(&report.bestChoice);

    report.qaInitStatus = (int32_t)QAInit();
    raveInitialized = report.qaInitStatus == kQANoErr;
    if (raveInitialized) {
        memset(&device, 0, sizeof(device));
        device.deviceType = kQADeviceGDevice;
        device.device.gDevice = GetMainDevice();
        GXMetalSelectionRecordInventory(&device, &report.inventory);
    }

    report.q3InitializeStatus = (int32_t)Q3Initialize();
    qd3dInitialized = report.q3InitializeStatus == kQ3Success;
    if (qd3dInitialized) {
        SetRect(&windowRect, 92, 72, 412, 312);
        window = NewCWindow(NULL, &windowRect,
                            kGXMetalSelectionWindowTitle, true,
                            documentProc, (WindowPtr)-1, false, 0);
        report.windowCreated = window != NULL;
        if (window != NULL) {
            SetPort(window);
        }
        GXMetalSelectionCreateRendererRecord(
            window, 0, &report.untouched);
        GXMetalSelectionCreateRendererRecord(
            window, 1, &report.bestChoice);
    }

    json = NewPtrClear(GXMETAL_SELECTION_REPORT_CAPACITY);
    if (json == NULL) {
        writeError = memFullErr;
    } else {
        GXMetalSelectionBuildJSON(
            &report, json, GXMETAL_SELECTION_REPORT_CAPACITY,
            &jsonTruncated);
        if (jsonTruncated) {
            writeError = memFullErr;
        } else {
            writeError = GXMetalSelectionWriteResults(json);
        }
        DisposePtr(json);
    }

    if (window != NULL) {
        DisposeWindow(window);
    }
    if (qd3dInitialized) {
        Q3Exit();
    }
    if (raveInitialized) {
        QAExit();
    }
    GXMetalSelectionShowResult(writeError == noErr && !jsonTruncated);
    return writeError == noErr && !jsonTruncated ? 0 : 1;
}
