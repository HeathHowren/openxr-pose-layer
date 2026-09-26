#include "Support.h"

#include "layer/Layer.h"

#include "Version.h"

#include <cmath>
#include <cstring>
#include <type_traits>

namespace poselayer::support {

namespace {

// Dummy dispatchable handles. The fake ignores them; the layer only forwards
// them, so any non-null value works.
XrSession kSession = reinterpret_cast<XrSession>(static_cast<uintptr_t>(0x5011));

const XrFovf kFov = {-0.7f, 0.7f, 0.7f, -0.7f};

XRAPI_ATTR XrResult XRAPI_CALL fakeWaitFrame(XrSession, const XrFrameWaitInfo*, XrFrameState* frameState) {
    FakeRuntime& f = fake();
    f.served = f.waitFrameCalls;
    ++f.waitFrameCalls;
    f.viewOrdinal = f.spaceOrdinal = f.spacesGroupOrdinal = 0;
    f.poseOrdinal = f.booleanOrdinal = f.floatOrdinal = f.vector2fOrdinal = 0;
    const FrameRecord* fr = f.servedFrame();
    if (frameState) {
        frameState->predictedDisplayTime = fr ? fr->predictedDisplayTime : 0;
        frameState->predictedDisplayPeriod = 11111111;
        frameState->shouldRender = XR_TRUE;
    }
    return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL fakeLocateViews(XrSession, const XrViewLocateInfo*, XrViewState* viewState, uint32_t viewCapacityInput,
                                               uint32_t* viewCountOutput, XrView* views) {
    FakeRuntime& f = fake();
    const FrameRecord* fr = f.servedFrame();
    const ViewSet* set = (fr && f.viewOrdinal < fr->viewSets.size()) ? &fr->viewSets[f.viewOrdinal] : nullptr;
    const uint32_t available = set ? static_cast<uint32_t>(set->views.size()) : 0;
    if (viewCapacityInput == 0 || views == nullptr) {
        if (viewCountOutput) {
            *viewCountOutput = available;
        }
        return XR_SUCCESS;
    }
    const uint32_t n = viewCapacityInput < available ? viewCapacityInput : available;
    if (viewCountOutput) {
        *viewCountOutput = available;
    }
    if (viewState) {
        viewState->viewStateFlags = set ? set->viewStateFlags : 0;
    }
    for (uint32_t i = 0; i < n; ++i) {
        views[i].type = XR_TYPE_VIEW;
        views[i].next = nullptr;
        views[i].pose = set->views[i].pose;
        views[i].fov = set->views[i].fov;
    }
    ++f.viewOrdinal;
    return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL fakeLocateSpace(XrSpace, XrSpace, XrTime, XrSpaceLocation* location) {
    FakeRuntime& f = fake();
    const FrameRecord* fr = f.servedFrame();
    const SpaceLocation* rec = (fr && f.spaceOrdinal < fr->spaces.size()) ? &fr->spaces[f.spaceOrdinal] : nullptr;
    if (location) {
        location->locationFlags = rec ? rec->locationFlags : 0;
        location->pose = rec ? rec->pose : kPoseIdentity;
    }
    ++f.spaceOrdinal;
    return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL fakeLocateSpaces(XrSession, const XrSpacesLocateInfo*, XrSpaceLocations* spaceLocations) {
    FakeRuntime& f = fake();
    const FrameRecord* fr = f.servedFrame();
    const SpacesLocation* rec = (fr && f.spacesGroupOrdinal < fr->spacesGroups.size()) ? &fr->spacesGroups[f.spacesGroupOrdinal] : nullptr;
    if (spaceLocations && spaceLocations->locations) {
        for (uint32_t i = 0; i < spaceLocations->locationCount; ++i) {
            if (rec && i < rec->locations.size()) {
                spaceLocations->locations[i].locationFlags = rec->locations[i].locationFlags;
                spaceLocations->locations[i].pose = rec->locations[i].pose;
            } else {
                spaceLocations->locations[i].locationFlags = 0;
                spaceLocations->locations[i].pose = kPoseIdentity;
            }
        }
    }
    ++f.spacesGroupOrdinal;
    return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL fakeSyncActions(XrSession, const XrActionsSyncInfo*) {
    return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL fakeGetActionStatePose(XrSession, const XrActionStateGetInfo*, XrActionStatePose* state) {
    FakeRuntime& f = fake();
    const FrameRecord* fr = f.servedFrame();
    const PoseInput* rec = (fr && f.poseOrdinal < fr->poseInputs.size()) ? &fr->poseInputs[f.poseOrdinal] : nullptr;
    if (state) {
        state->isActive = (rec && rec->isActive) ? XR_TRUE : XR_FALSE;
    }
    ++f.poseOrdinal;
    return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL fakeGetActionStateBoolean(XrSession, const XrActionStateGetInfo*, XrActionStateBoolean* state) {
    FakeRuntime& f = fake();
    const FrameRecord* fr = f.servedFrame();
    const BooleanInput* rec = (fr && f.booleanOrdinal < fr->booleanInputs.size()) ? &fr->booleanInputs[f.booleanOrdinal] : nullptr;
    if (state) {
        state->isActive = (rec && rec->isActive) ? XR_TRUE : XR_FALSE;
        state->currentState = (rec && rec->currentState) ? XR_TRUE : XR_FALSE;
        state->changedSinceLastSync = (rec && rec->changedSinceLastSync) ? XR_TRUE : XR_FALSE;
        state->lastChangeTime = rec ? rec->lastChangeTime : 0;
    }
    ++f.booleanOrdinal;
    return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL fakeGetActionStateFloat(XrSession, const XrActionStateGetInfo*, XrActionStateFloat* state) {
    FakeRuntime& f = fake();
    const FrameRecord* fr = f.servedFrame();
    const FloatInput* rec = (fr && f.floatOrdinal < fr->floatInputs.size()) ? &fr->floatInputs[f.floatOrdinal] : nullptr;
    if (state) {
        state->isActive = (rec && rec->isActive) ? XR_TRUE : XR_FALSE;
        state->changedSinceLastSync = (rec && rec->changedSinceLastSync) ? XR_TRUE : XR_FALSE;
        state->currentState = rec ? rec->currentState : 0.0f;
        state->lastChangeTime = rec ? rec->lastChangeTime : 0;
    }
    ++f.floatOrdinal;
    return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL fakeGetActionStateVector2f(XrSession, const XrActionStateGetInfo*, XrActionStateVector2f* state) {
    FakeRuntime& f = fake();
    const FrameRecord* fr = f.servedFrame();
    const Vector2fInput* rec = (fr && f.vector2fOrdinal < fr->vector2fInputs.size()) ? &fr->vector2fInputs[f.vector2fOrdinal] : nullptr;
    if (state) {
        state->isActive = (rec && rec->isActive) ? XR_TRUE : XR_FALSE;
        state->changedSinceLastSync = (rec && rec->changedSinceLastSync) ? XR_TRUE : XR_FALSE;
        state->currentState = rec ? rec->currentState : XrVector2f{0.0f, 0.0f};
        state->lastChangeTime = rec ? rec->lastChangeTime : 0;
    }
    ++f.vector2fOrdinal;
    return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL fakeEndFrame(XrSession, const XrFrameEndInfo* frameEndInfo) {
    FakeRuntime& f = fake();
    std::vector<XrPosef> poses;
    if (frameEndInfo && frameEndInfo->layers) {
        for (uint32_t i = 0; i < frameEndInfo->layerCount; ++i) {
            const XrCompositionLayerBaseHeader* header = frameEndInfo->layers[i];
            if (header && header->type == XR_TYPE_COMPOSITION_LAYER_PROJECTION) {
                const auto* proj = reinterpret_cast<const XrCompositionLayerProjection*>(header);
                for (uint32_t j = 0; j < proj->viewCount; ++j) {
                    poses.push_back(proj->views[j].pose);
                }
            }
        }
    }
    f.submittedViewPoses.push_back(std::move(poses));
    ++f.endFrameCalls;
    return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL fakeDestroyInstance(XrInstance) {
    return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL fakeGetInstanceProcAddr(XrInstance, const char* name, PFN_xrVoidFunction* function) {
    if (!function) {
        return XR_ERROR_VALIDATION_FAILURE;
    }
    const std::string n = name ? name : "";
    auto as = [](auto fn) { return reinterpret_cast<PFN_xrVoidFunction>(fn); };
    *function = nullptr;
    if (n == "xrGetInstanceProcAddr") {
        *function = as(&fakeGetInstanceProcAddr);
    } else if (n == "xrDestroyInstance") {
        *function = as(&fakeDestroyInstance);
    } else if (n == "xrWaitFrame") {
        *function = as(&fakeWaitFrame);
    } else if (n == "xrLocateViews") {
        *function = as(&fakeLocateViews);
    } else if (n == "xrLocateSpace") {
        *function = as(&fakeLocateSpace);
    } else if (n == "xrLocateSpaces") {
        *function = as(&fakeLocateSpaces);
    } else if (n == "xrSyncActions") {
        *function = as(&fakeSyncActions);
    } else if (n == "xrGetActionStatePose") {
        *function = as(&fakeGetActionStatePose);
    } else if (n == "xrGetActionStateBoolean") {
        *function = as(&fakeGetActionStateBoolean);
    } else if (n == "xrGetActionStateFloat") {
        *function = as(&fakeGetActionStateFloat);
    } else if (n == "xrGetActionStateVector2f") {
        *function = as(&fakeGetActionStateVector2f);
    } else if (n == "xrEndFrame") {
        *function = as(&fakeEndFrame);
    }
    return *function ? XR_SUCCESS : XR_ERROR_FUNCTION_UNSUPPORTED;
}

XRAPI_ATTR XrResult XRAPI_CALL fakeCreateApiLayerInstance(const XrInstanceCreateInfo*, const XrApiLayerCreateInfo*, XrInstance* instance) {
    // A fake dispatchable instance handle: the address of the active fake.
    *instance = reinterpret_cast<XrInstance>(&fake());
    return XR_SUCCESS;
}

} // namespace

void FakeRuntime::reset() {
    submittedViewPoses.clear();
    waitFrameCalls = 0;
    endFrameCalls = 0;
    served = 0;
    viewOrdinal = spaceOrdinal = spacesGroupOrdinal = 0;
    poseOrdinal = booleanOrdinal = floatOrdinal = vector2fOrdinal = 0;
}

const FrameRecord* FakeRuntime::servedFrame() const {
    return served < script.frames.size() ? &script.frames[served] : nullptr;
}

FakeRuntime& fake() {
    static FakeRuntime instance;
    return instance;
}

LayerHarness::LayerHarness(FakeRuntime& runtime, const Config& config) : runtime_(runtime), config_(config) {}

LayerHarness::~LayerHarness() {
    if (created_) {
        destroy();
    }
    poselayer::testing::setConfigOverride(nullptr);
}

bool LayerHarness::create() {
    poselayer::testing::setConfigOverride(&config_);

    nextInfo_ = {};
    nextInfo_.structType = XR_LOADER_INTERFACE_STRUCT_API_LAYER_NEXT_INFO;
    nextInfo_.structVersion = XR_API_LAYER_NEXT_INFO_STRUCT_VERSION;
    nextInfo_.structSize = sizeof(XrApiLayerNextInfo);
    {
        const char* name = POSELAYER_LAYER_NAME;
        size_t n = std::strlen(name);
        if (n > XR_MAX_API_LAYER_NAME_SIZE - 1) {
            n = XR_MAX_API_LAYER_NAME_SIZE - 1;
        }
        std::memcpy(nextInfo_.layerName, name, n);
        nextInfo_.layerName[n] = '\0';
    }
    nextInfo_.nextGetInstanceProcAddr = &fakeGetInstanceProcAddr;
    nextInfo_.nextCreateApiLayerInstance = &fakeCreateApiLayerInstance;
    nextInfo_.next = nullptr;

    XrApiLayerCreateInfo createInfo = {};
    createInfo.structType = XR_LOADER_INTERFACE_STRUCT_API_LAYER_CREATE_INFO;
    createInfo.structVersion = XR_API_LAYER_CREATE_INFO_STRUCT_VERSION;
    createInfo.structSize = sizeof(XrApiLayerCreateInfo);
    createInfo.loaderInstance = nullptr;
    createInfo.settings_file_location[0] = '\0';
    createInfo.nextInfo = &nextInfo_;

    XrInstanceCreateInfo instanceInfo = {};
    instanceInfo.type = XR_TYPE_INSTANCE_CREATE_INFO;

    const XrResult r = poselayer::createApiLayerInstance(&instanceInfo, &createInfo, &instance_);
    if (XR_FAILED(r)) {
        return false;
    }

    auto get = [&](const char* name, auto& out) {
        PFN_xrVoidFunction fn = nullptr;
        poselayer::getInstanceProcAddr(instance_, name, &fn);
        out = reinterpret_cast<std::remove_reference_t<decltype(out)>>(fn);
    };
    get("xrGetInstanceProcAddr", getProc_);
    get("xrDestroyInstance", destroyInstance_);
    get("xrWaitFrame", waitFrame_);
    get("xrLocateViews", locateViews_);
    get("xrLocateSpace", locateSpace_);
    get("xrLocateSpaces", locateSpaces_);
    get("xrSyncActions", syncActions_);
    get("xrGetActionStatePose", getPose_);
    get("xrGetActionStateBoolean", getBoolean_);
    get("xrGetActionStateFloat", getFloat_);
    get("xrGetActionStateVector2f", getVector2f_);
    get("xrEndFrame", endFrame_);

    created_ = true;
    return true;
}

void LayerHarness::driveFrame(const FrameOptions& options) {
    XrFrameWaitInfo waitInfo = {XR_TYPE_FRAME_WAIT_INFO, nullptr};
    lastFrameState = {XR_TYPE_FRAME_STATE, nullptr, 0, 0, XR_FALSE};
    waitFrame_(kSession, &waitInfo, &lastFrameState);

    if (options.locateViews) {
        XrViewLocateInfo locateInfo = {XR_TYPE_VIEW_LOCATE_INFO, nullptr, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,
                                       lastFrameState.predictedDisplayTime, XR_NULL_HANDLE};
        uint32_t count = 0;
        locateViews_(kSession, &locateInfo, nullptr, 0, &count, nullptr);
        lastViews.assign(count, XrView{XR_TYPE_VIEW, nullptr, kPoseIdentity, kFov});
        lastViewState = {XR_TYPE_VIEW_STATE, nullptr, 0};
        locateViews_(kSession, &locateInfo, &lastViewState, count, &count, lastViews.data());
    }

    if (options.locateSpace) {
        lastSpace = {XR_TYPE_SPACE_LOCATION, nullptr, 0, kPoseIdentity};
        XrSpace space = reinterpret_cast<XrSpace>(static_cast<uintptr_t>(0x6001));
        XrSpace base = reinterpret_cast<XrSpace>(static_cast<uintptr_t>(0x6002));
        locateSpace_(space, base, lastFrameState.predictedDisplayTime, &lastSpace);
    }

    if (options.locateSpaces && locateSpaces_) {
        const FrameRecord* fr = runtime_.servedFrame();
        const uint32_t groupSize =
            (fr && !fr->spacesGroups.empty()) ? static_cast<uint32_t>(fr->spacesGroups.front().locations.size()) : 0;
        if (groupSize > 0) {
            lastSpacesGroup.assign(groupSize, XrSpaceLocation{XR_TYPE_SPACE_LOCATION, nullptr, 0, kPoseIdentity});
            std::vector<XrSpaceLocationData> data(groupSize);
            XrSpacesLocateInfo info = {XR_TYPE_SPACES_LOCATE_INFO, nullptr, XR_NULL_HANDLE, lastFrameState.predictedDisplayTime, 0,
                                       nullptr};
            XrSpaceLocations out = {XR_TYPE_SPACE_LOCATIONS, nullptr, groupSize, data.data()};
            locateSpaces_(kSession, &info, &out);
            for (uint32_t i = 0; i < groupSize; ++i) {
                lastSpacesGroup[i].locationFlags = data[i].locationFlags;
                lastSpacesGroup[i].pose = data[i].pose;
            }
        }
    }

    if (options.syncActions) {
        XrActionsSyncInfo syncInfo = {XR_TYPE_ACTIONS_SYNC_INFO, nullptr, 0, nullptr};
        syncActions_(kSession, &syncInfo);
    }

    XrActionStateGetInfo getInfo = {XR_TYPE_ACTION_STATE_GET_INFO, nullptr, XR_NULL_HANDLE, XR_NULL_PATH};
    if (options.getPose) {
        lastPose = {XR_TYPE_ACTION_STATE_POSE, nullptr, XR_FALSE};
        getPose_(kSession, &getInfo, &lastPose);
    }
    if (options.getBoolean) {
        lastBoolean = {XR_TYPE_ACTION_STATE_BOOLEAN, nullptr, XR_FALSE, XR_FALSE, 0, XR_FALSE};
        getBoolean_(kSession, &getInfo, &lastBoolean);
    }
    if (options.getFloat) {
        lastFloat = {XR_TYPE_ACTION_STATE_FLOAT, nullptr, 0.0f, XR_FALSE, 0, XR_FALSE};
        getFloat_(kSession, &getInfo, &lastFloat);
    }
    if (options.getVector2f) {
        lastVector2f = {XR_TYPE_ACTION_STATE_VECTOR2F, nullptr, {0.0f, 0.0f}, XR_FALSE, 0, XR_FALSE};
        getVector2f_(kSession, &getInfo, &lastVector2f);
    }

    if (options.endFrame) {
        // Submit a projection layer whose view poses are a sentinel, distinct
        // from anything recorded, so a replay test can prove xrEndFrame rewrote
        // them to the recorded poses.
        const XrPosef sentinel = {{0.0f, 0.0f, 0.0f, 1.0f}, {9.0f, 9.0f, 9.0f}};
        std::vector<XrCompositionLayerProjectionView> projViews(
            options.projectionViewCount, XrCompositionLayerProjectionView{XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW, nullptr, sentinel,
                                                                          kFov, XrSwapchainSubImage{}});
        XrCompositionLayerProjection projection = {XR_TYPE_COMPOSITION_LAYER_PROJECTION,
                                                   nullptr,
                                                   0,
                                                   XR_NULL_HANDLE,
                                                   static_cast<uint32_t>(projViews.size()),
                                                   projViews.data()};
        const XrCompositionLayerBaseHeader* layer = reinterpret_cast<const XrCompositionLayerBaseHeader*>(&projection);
        XrFrameEndInfo endInfo = {XR_TYPE_FRAME_END_INFO,
                                  nullptr,
                                  lastFrameState.predictedDisplayTime,
                                  XR_ENVIRONMENT_BLEND_MODE_OPAQUE,
                                  1,
                                  &layer};
        endFrame_(kSession, &endInfo);
    }
}

void LayerHarness::destroy() {
    if (created_ && destroyInstance_) {
        destroyInstance_(instance_);
    }
    created_ = false;
    poselayer::testing::setConfigOverride(nullptr);
}

Recording makeSampleRecording(uint32_t frameCount) {
    Recording rec;
    rec.frames.reserve(frameCount);
    const int64_t period = 11111111; // ~90 fps in nanoseconds
    const int64_t base = 1000000000; // arbitrary start time
    const uint64_t located = XR_SPACE_LOCATION_POSITION_VALID_BIT | XR_SPACE_LOCATION_ORIENTATION_VALID_BIT |
                             XR_SPACE_LOCATION_POSITION_TRACKED_BIT | XR_SPACE_LOCATION_ORIENTATION_TRACKED_BIT;

    for (uint32_t f = 0; f < frameCount; ++f) {
        const float t = static_cast<float>(f);
        FrameRecord frame;
        frame.frameIndex = f;
        frame.predictedDisplayTime = base + static_cast<int64_t>(f) * period;

        // Head: a slow sway and bob, turning to look around.
        const float headX = 0.03f * std::sin(t * 0.10f);
        const float headY = 1.60f + 0.05f * std::sin(t * 0.05f);
        const float headZ = 0.02f * std::cos(t * 0.10f);
        const XrQuaternionf headRot = quatFromYawDegrees(20.0f * std::sin(t * 0.03f));

        ViewSet views;
        views.viewStateFlags = XR_VIEW_STATE_POSITION_VALID_BIT | XR_VIEW_STATE_ORIENTATION_VALID_BIT |
                               XR_VIEW_STATE_POSITION_TRACKED_BIT | XR_VIEW_STATE_ORIENTATION_TRACKED_BIT;
        const float ipdHalf = 0.032f;
        for (int eye = 0; eye < 2; ++eye) {
            RecordedView view;
            const float sign = eye == 0 ? -1.0f : 1.0f;
            const XrVector3f local = {sign * ipdHalf, 0.0f, 0.0f};
            const XrVector3f offset = quatRotate(headRot, local);
            view.pose.orientation = headRot;
            view.pose.position = {headX + offset.x, headY + offset.y, headZ + offset.z};
            view.fov = {-0.785f, 0.785f, 0.785f, -0.785f};
            views.views.push_back(view);
        }
        frame.viewSets.push_back(std::move(views));

        // A controller a little in front of and below the head.
        SpaceLocation controller;
        controller.locationFlags = located;
        controller.pose.orientation = headRot;
        controller.pose.position = {headX + 0.20f, headY - 0.30f, headZ - 0.25f};
        frame.spaces.push_back(controller);

        // A trigger, pressed for a stretch in the middle.
        const bool pressed = f >= frameCount / 3 && f < frameCount / 3 + 10;
        BooleanInput trigger;
        trigger.isActive = true;
        trigger.currentState = pressed;
        trigger.changedSinceLastSync = (f == frameCount / 3) || (f == frameCount / 3 + 10);
        trigger.lastChangeTime = frame.predictedDisplayTime;
        frame.booleanInputs.push_back(trigger);

        FloatInput squeeze;
        squeeze.isActive = true;
        squeeze.currentState = pressed ? 1.0f : 0.0f;
        squeeze.changedSinceLastSync = trigger.changedSinceLastSync;
        squeeze.lastChangeTime = frame.predictedDisplayTime;
        frame.floatInputs.push_back(squeeze);

        rec.frames.push_back(std::move(frame));
    }
    return rec;
}

} // namespace poselayer::support
