#include "layer/Layer.h"

#include "core/Recording.h"
#include "core/Replay.h"
#include "core/Transform.h"

#include "Version.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <fstream>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <ostream>
#include <string>
#include <unordered_map>
#include <vector>

namespace poselayer {

namespace {

using Clock = std::chrono::steady_clock;

// The subset of downstream entry points the layer forwards to. Resolved once,
// at instance creation, through the next layer's xrGetInstanceProcAddr.
struct DownstreamTable {
    PFN_xrGetInstanceProcAddr getInstanceProcAddr = nullptr;
    PFN_xrDestroyInstance destroyInstance = nullptr;
    PFN_xrWaitFrame waitFrame = nullptr;
    PFN_xrLocateViews locateViews = nullptr;
    PFN_xrLocateSpace locateSpace = nullptr;
    PFN_xrLocateSpaces locateSpaces = nullptr;
    PFN_xrSyncActions syncActions = nullptr;
    PFN_xrGetActionStatePose getActionStatePose = nullptr;
    PFN_xrGetActionStateBoolean getActionStateBoolean = nullptr;
    PFN_xrGetActionStateFloat getActionStateFloat = nullptr;
    PFN_xrGetActionStateVector2f getActionStateVector2f = nullptr;
    PFN_xrEndFrame endFrame = nullptr;
};

const char* resultName(XrResult r) {
    switch (r) {
    case XR_SUCCESS:
        return "XR_SUCCESS";
    case XR_SESSION_LOSS_PENDING:
        return "XR_SESSION_LOSS_PENDING";
    case XR_FRAME_DISCARDED:
        return "XR_FRAME_DISCARDED";
    case XR_ERROR_VALIDATION_FAILURE:
        return "XR_ERROR_VALIDATION_FAILURE";
    case XR_ERROR_HANDLE_INVALID:
        return "XR_ERROR_HANDLE_INVALID";
    case XR_ERROR_FUNCTION_UNSUPPORTED:
        return "XR_ERROR_FUNCTION_UNSUPPORTED";
    default:
        return nullptr;
    }
}

// One XrInstance's worth of state: how to forward, what mode it is in, and the
// recording being written or played back.
struct InstanceState {
    XrInstance instance = XR_NULL_HANDLE;
    DownstreamTable down;
    Config config;

    // record
    Recording recording;
    size_t currentFramePos = 0;
    bool haveCurrentFrame = false;
    uint32_t frameCounter = 0;

    // replay
    std::optional<Recording> loaded;
    std::optional<ReplayCursor> cursor;

    // log
    std::ofstream logFile;
    std::ostream* log = nullptr;
    Clock::time_point started = Clock::now();

    std::mutex mutex;

    FrameRecord* currentFrame() {
        if (!haveCurrentFrame || currentFramePos >= recording.frames.size()) {
            return nullptr;
        }
        return &recording.frames[currentFramePos];
    }
};

// The layer assumes one active XrInstance, which every shipping OpenXR app
// creates. Session- and space-scoped calls do not carry an XrInstance, so they
// resolve against the most recently created instance.
class Registry {
public:
    static Registry& get() {
        static Registry registry;
        return registry;
    }

    InstanceState* create(XrInstance instance) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto state = std::make_unique<InstanceState>();
        state->instance = instance;
        InstanceState* raw = state.get();
        instances_[instance] = std::move(state);
        active_ = raw;
        return raw;
    }

    InstanceState* forInstance(XrInstance instance) {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto it = instances_.find(instance);
        return it == instances_.end() ? nullptr : it->second.get();
    }

    InstanceState* active() {
        std::lock_guard<std::mutex> lock(mutex_);
        return active_;
    }

    std::unique_ptr<InstanceState> remove(XrInstance instance) {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto it = instances_.find(instance);
        if (it == instances_.end()) {
            return nullptr;
        }
        std::unique_ptr<InstanceState> state = std::move(it->second);
        instances_.erase(it);
        if (active_ == state.get()) {
            active_ = instances_.empty() ? nullptr : instances_.begin()->second.get();
        }
        return state;
    }

    PFN_xrGetInstanceProcAddr fallbackGetProcAddr() {
        std::lock_guard<std::mutex> lock(mutex_);
        return fallbackGetProcAddr_;
    }
    void setFallbackGetProcAddr(PFN_xrGetInstanceProcAddr fn) {
        std::lock_guard<std::mutex> lock(mutex_);
        fallbackGetProcAddr_ = fn;
    }

private:
    std::mutex mutex_;
    std::unordered_map<XrInstance, std::unique_ptr<InstanceState>> instances_;
    InstanceState* active_ = nullptr;
    PFN_xrGetInstanceProcAddr fallbackGetProcAddr_ = nullptr;
};

const Config* g_configOverride = nullptr;

void logCall(InstanceState& s, const char* fn, XrResult r, Clock::time_point start, long long frame) {
    if (s.config.mode != Mode::Log || !s.log) {
        return;
    }
    const auto now = Clock::now();
    const double sinceStartMs = std::chrono::duration<double, std::milli>(now - s.started).count();
    const double durUs = std::chrono::duration<double, std::micro>(now - start).count();
    std::lock_guard<std::mutex> lock(s.mutex);
    std::ostream& out = *s.log;
    char buf[64];
    std::snprintf(buf, sizeof(buf), "[pose-layer] +%9.3fms ", sinceStartMs);
    out << buf;
    if (frame >= 0) {
        out << "f" << frame << ' ';
    }
    out << fn << " -> ";
    if (const char* name = resultName(r)) {
        out << name;
    } else {
        out << static_cast<int>(r);
    }
    char dbuf[48];
    std::snprintf(dbuf, sizeof(dbuf), " (%.1f us)\n", durUs);
    out << dbuf;
    out.flush();
}

// ---------------------------------------------------------------------------
// Hooked entry points
// ---------------------------------------------------------------------------

XRAPI_ATTR XrResult XRAPI_CALL hookWaitFrame(XrSession session, const XrFrameWaitInfo* frameWaitInfo, XrFrameState* frameState) {
    InstanceState* s = Registry::get().active();
    if (!s || !s->down.waitFrame) {
        return XR_ERROR_HANDLE_INVALID;
    }
    const auto start = Clock::now();
    const XrResult r = s->down.waitFrame(session, frameWaitInfo, frameState);

    long long frameIndex = -1;
    if (XR_SUCCEEDED(r)) {
        std::lock_guard<std::mutex> lock(s->mutex);
        const uint32_t idx = s->frameCounter++;
        frameIndex = static_cast<long long>(idx);
        if (s->config.mode == Mode::Record) {
            FrameRecord frame;
            frame.frameIndex = idx;
            frame.predictedDisplayTime = frameState ? frameState->predictedDisplayTime : 0;
            s->recording.frames.push_back(std::move(frame));
            s->currentFramePos = s->recording.frames.size() - 1;
            s->haveCurrentFrame = true;
        } else if (s->config.mode == Mode::Replay && s->cursor) {
            s->cursor->advance();
            // Timing (predictedDisplayTime) stays whatever the runtime returned,
            // so the compositor is happy; only poses come from the recording.
        }
    }
    logCall(*s, "xrWaitFrame", r, start, frameIndex);
    return r;
}

XRAPI_ATTR XrResult XRAPI_CALL hookLocateViews(XrSession session, const XrViewLocateInfo* viewLocateInfo, XrViewState* viewState,
                                               uint32_t viewCapacityInput, uint32_t* viewCountOutput, XrView* views) {
    InstanceState* s = Registry::get().active();
    if (!s || !s->down.locateViews) {
        return XR_ERROR_HANDLE_INVALID;
    }
    const auto start = Clock::now();
    const XrResult r = s->down.locateViews(session, viewLocateInfo, viewState, viewCapacityInput, viewCountOutput, views);
    logCall(*s, "xrLocateViews", r, start, -1);

    const bool dataCall = views != nullptr && viewCapacityInput > 0;
    if (XR_SUCCEEDED(r) && dataCall) {
        std::lock_guard<std::mutex> lock(s->mutex);
        const uint32_t written = viewCountOutput ? std::min(*viewCountOutput, viewCapacityInput) : 0;
        if (s->config.mode == Mode::Record) {
            if (FrameRecord* frame = s->currentFrame()) {
                ViewSet set;
                set.viewStateFlags = viewState ? viewState->viewStateFlags : 0;
                set.views.reserve(written);
                for (uint32_t i = 0; i < written; ++i) {
                    set.views.push_back({views[i].pose, views[i].fov});
                }
                frame->viewSets.push_back(std::move(set));
            }
        } else if (s->config.mode == Mode::Replay && s->cursor) {
            if (const ViewSet* set = s->cursor->nextViewSet()) {
                if (viewState) {
                    viewState->viewStateFlags = set->viewStateFlags;
                }
                const uint32_t n = std::min(viewCapacityInput, static_cast<uint32_t>(set->views.size()));
                for (uint32_t i = 0; i < n; ++i) {
                    views[i].pose = s->cursor->offsetView(set->views[i].pose);
                    views[i].fov = set->views[i].fov;
                }
            }
        }
    }
    return r;
}

XRAPI_ATTR XrResult XRAPI_CALL hookLocateSpace(XrSpace space, XrSpace baseSpace, XrTime time, XrSpaceLocation* location) {
    InstanceState* s = Registry::get().active();
    if (!s || !s->down.locateSpace) {
        return XR_ERROR_HANDLE_INVALID;
    }
    const auto start = Clock::now();
    const XrResult r = s->down.locateSpace(space, baseSpace, time, location);
    logCall(*s, "xrLocateSpace", r, start, -1);

    if (XR_SUCCEEDED(r) && location) {
        std::lock_guard<std::mutex> lock(s->mutex);
        if (s->config.mode == Mode::Record) {
            if (FrameRecord* frame = s->currentFrame()) {
                frame->spaces.push_back({location->locationFlags, location->pose});
            }
        } else if (s->config.mode == Mode::Replay && s->cursor) {
            if (const SpaceLocation* rec = s->cursor->nextSpace()) {
                location->locationFlags = rec->locationFlags;
                location->pose = s->cursor->offsetView(rec->pose);
            }
        }
    }
    return r;
}

XRAPI_ATTR XrResult XRAPI_CALL hookLocateSpaces(XrSession session, const XrSpacesLocateInfo* locateInfo, XrSpaceLocations* spaceLocations) {
    InstanceState* s = Registry::get().active();
    if (!s || !s->down.locateSpaces) {
        return XR_ERROR_HANDLE_INVALID;
    }
    const auto start = Clock::now();
    const XrResult r = s->down.locateSpaces(session, locateInfo, spaceLocations);
    logCall(*s, "xrLocateSpaces", r, start, -1);

    if (XR_SUCCEEDED(r) && spaceLocations && spaceLocations->locations) {
        std::lock_guard<std::mutex> lock(s->mutex);
        const uint32_t count = spaceLocations->locationCount;
        if (s->config.mode == Mode::Record) {
            if (FrameRecord* frame = s->currentFrame()) {
                SpacesLocation group;
                group.locations.reserve(count);
                for (uint32_t i = 0; i < count; ++i) {
                    group.locations.push_back({spaceLocations->locations[i].locationFlags, spaceLocations->locations[i].pose});
                }
                frame->spacesGroups.push_back(std::move(group));
            }
        } else if (s->config.mode == Mode::Replay && s->cursor) {
            if (const SpacesLocation* rec = s->cursor->nextSpacesGroup()) {
                const uint32_t n = std::min(count, static_cast<uint32_t>(rec->locations.size()));
                for (uint32_t i = 0; i < n; ++i) {
                    spaceLocations->locations[i].locationFlags = rec->locations[i].locationFlags;
                    spaceLocations->locations[i].pose = s->cursor->offsetView(rec->locations[i].pose);
                }
            }
        }
    }
    return r;
}

XRAPI_ATTR XrResult XRAPI_CALL hookSyncActions(XrSession session, const XrActionsSyncInfo* syncInfo) {
    InstanceState* s = Registry::get().active();
    if (!s || !s->down.syncActions) {
        return XR_ERROR_HANDLE_INVALID;
    }
    const auto start = Clock::now();
    const XrResult r = s->down.syncActions(session, syncInfo);
    logCall(*s, "xrSyncActions", r, start, -1);
    return r;
}

XRAPI_ATTR XrResult XRAPI_CALL hookGetActionStatePose(XrSession session, const XrActionStateGetInfo* getInfo, XrActionStatePose* state) {
    InstanceState* s = Registry::get().active();
    if (!s || !s->down.getActionStatePose) {
        return XR_ERROR_HANDLE_INVALID;
    }
    const auto start = Clock::now();
    const XrResult r = s->down.getActionStatePose(session, getInfo, state);
    logCall(*s, "xrGetActionStatePose", r, start, -1);

    if (XR_SUCCEEDED(r) && state) {
        std::lock_guard<std::mutex> lock(s->mutex);
        if (s->config.mode == Mode::Record) {
            if (FrameRecord* frame = s->currentFrame()) {
                frame->poseInputs.push_back({state->isActive == XR_TRUE});
            }
        } else if (s->config.mode == Mode::Replay && s->cursor) {
            if (const PoseInput* rec = s->cursor->nextPoseInput()) {
                state->isActive = rec->isActive ? XR_TRUE : XR_FALSE;
            }
        }
    }
    return r;
}

XRAPI_ATTR XrResult XRAPI_CALL hookGetActionStateBoolean(XrSession session, const XrActionStateGetInfo* getInfo,
                                                         XrActionStateBoolean* state) {
    InstanceState* s = Registry::get().active();
    if (!s || !s->down.getActionStateBoolean) {
        return XR_ERROR_HANDLE_INVALID;
    }
    const auto start = Clock::now();
    const XrResult r = s->down.getActionStateBoolean(session, getInfo, state);
    logCall(*s, "xrGetActionStateBoolean", r, start, -1);

    if (XR_SUCCEEDED(r) && state) {
        std::lock_guard<std::mutex> lock(s->mutex);
        if (s->config.mode == Mode::Record) {
            if (FrameRecord* frame = s->currentFrame()) {
                frame->booleanInputs.push_back({state->isActive == XR_TRUE, state->currentState == XR_TRUE,
                                                state->changedSinceLastSync == XR_TRUE, state->lastChangeTime});
            }
        } else if (s->config.mode == Mode::Replay && s->cursor) {
            if (const BooleanInput* rec = s->cursor->nextBooleanInput()) {
                state->isActive = rec->isActive ? XR_TRUE : XR_FALSE;
                state->currentState = rec->currentState ? XR_TRUE : XR_FALSE;
                state->changedSinceLastSync = rec->changedSinceLastSync ? XR_TRUE : XR_FALSE;
                state->lastChangeTime = rec->lastChangeTime;
            }
        }
    }
    return r;
}

XRAPI_ATTR XrResult XRAPI_CALL hookGetActionStateFloat(XrSession session, const XrActionStateGetInfo* getInfo, XrActionStateFloat* state) {
    InstanceState* s = Registry::get().active();
    if (!s || !s->down.getActionStateFloat) {
        return XR_ERROR_HANDLE_INVALID;
    }
    const auto start = Clock::now();
    const XrResult r = s->down.getActionStateFloat(session, getInfo, state);
    logCall(*s, "xrGetActionStateFloat", r, start, -1);

    if (XR_SUCCEEDED(r) && state) {
        std::lock_guard<std::mutex> lock(s->mutex);
        if (s->config.mode == Mode::Record) {
            if (FrameRecord* frame = s->currentFrame()) {
                frame->floatInputs.push_back(
                    {state->isActive == XR_TRUE, state->changedSinceLastSync == XR_TRUE, state->currentState, state->lastChangeTime});
            }
        } else if (s->config.mode == Mode::Replay && s->cursor) {
            if (const FloatInput* rec = s->cursor->nextFloatInput()) {
                state->isActive = rec->isActive ? XR_TRUE : XR_FALSE;
                state->changedSinceLastSync = rec->changedSinceLastSync ? XR_TRUE : XR_FALSE;
                state->currentState = rec->currentState;
                state->lastChangeTime = rec->lastChangeTime;
            }
        }
    }
    return r;
}

XRAPI_ATTR XrResult XRAPI_CALL hookGetActionStateVector2f(XrSession session, const XrActionStateGetInfo* getInfo,
                                                          XrActionStateVector2f* state) {
    InstanceState* s = Registry::get().active();
    if (!s || !s->down.getActionStateVector2f) {
        return XR_ERROR_HANDLE_INVALID;
    }
    const auto start = Clock::now();
    const XrResult r = s->down.getActionStateVector2f(session, getInfo, state);
    logCall(*s, "xrGetActionStateVector2f", r, start, -1);

    if (XR_SUCCEEDED(r) && state) {
        std::lock_guard<std::mutex> lock(s->mutex);
        if (s->config.mode == Mode::Record) {
            if (FrameRecord* frame = s->currentFrame()) {
                frame->vector2fInputs.push_back({state->isActive == XR_TRUE, state->changedSinceLastSync == XR_TRUE, state->currentState,
                                                 state->lastChangeTime});
            }
        } else if (s->config.mode == Mode::Replay && s->cursor) {
            if (const Vector2fInput* rec = s->cursor->nextVector2fInput()) {
                state->isActive = rec->isActive ? XR_TRUE : XR_FALSE;
                state->changedSinceLastSync = rec->changedSinceLastSync ? XR_TRUE : XR_FALSE;
                state->currentState = rec->currentState;
                state->lastChangeTime = rec->lastChangeTime;
            }
        }
    }
    return r;
}

XRAPI_ATTR XrResult XRAPI_CALL hookEndFrame(XrSession session, const XrFrameEndInfo* frameEndInfo) {
    InstanceState* s = Registry::get().active();
    if (!s || !s->down.endFrame) {
        return XR_ERROR_HANDLE_INVALID;
    }

    // Replay rewrites the projection-layer view poses so the compositor renders
    // from the recorded head/eye poses even for an app that caches them.
    if (frameEndInfo && s->config.mode == Mode::Replay && s->cursor) {
        std::lock_guard<std::mutex> lock(s->mutex);
        const FrameRecord* frame = s->cursor->current();
        if (frame && !frame->viewSets.empty()) {
            const ViewSet& recordedViews = frame->viewSets.front();
            const uint32_t layerCount = frameEndInfo->layerCount;

            std::vector<const XrCompositionLayerBaseHeader*> layerPtrs(layerCount);
            std::vector<XrCompositionLayerProjection> projCopies;
            std::vector<std::vector<XrCompositionLayerProjectionView>> viewCopies;
            projCopies.reserve(layerCount);
            viewCopies.reserve(layerCount);

            bool rewroteAny = false;
            for (uint32_t i = 0; i < layerCount; ++i) {
                const XrCompositionLayerBaseHeader* header = frameEndInfo->layers ? frameEndInfo->layers[i] : nullptr;
                if (header && header->type == XR_TYPE_COMPOSITION_LAYER_PROJECTION) {
                    const auto* proj = reinterpret_cast<const XrCompositionLayerProjection*>(header);
                    viewCopies.emplace_back(proj->views, proj->views + proj->viewCount);
                    std::vector<XrCompositionLayerProjectionView>& vc = viewCopies.back();
                    const uint32_t n = std::min(proj->viewCount, static_cast<uint32_t>(recordedViews.views.size()));
                    for (uint32_t j = 0; j < n; ++j) {
                        vc[j].pose = s->cursor->offsetView(recordedViews.views[j].pose);
                    }
                    XrCompositionLayerProjection copy = *proj;
                    copy.views = vc.data();
                    projCopies.push_back(copy);
                    layerPtrs[i] = reinterpret_cast<const XrCompositionLayerBaseHeader*>(&projCopies.back());
                    rewroteAny = true;
                } else {
                    layerPtrs[i] = header;
                }
            }

            if (rewroteAny) {
                XrFrameEndInfo patched = *frameEndInfo;
                patched.layers = layerPtrs.data();
                const auto start = Clock::now();
                const XrResult r = s->down.endFrame(session, &patched);
                logCall(*s, "xrEndFrame", r, start, -1);
                return r;
            }
        }
    }

    const auto start = Clock::now();
    const XrResult r = s->down.endFrame(session, frameEndInfo);
    logCall(*s, "xrEndFrame", r, start, -1);
    return r;
}

XRAPI_ATTR XrResult XRAPI_CALL hookDestroyInstance(XrInstance instance) {
    std::unique_ptr<InstanceState> state = Registry::get().remove(instance);
    XrResult r = XR_SUCCESS;
    if (state) {
        if (state->config.mode == Mode::Record && !state->config.file.empty()) {
            LoadError err;
            if (!state->recording.save(state->config.file, &err) && state->log) {
                *state->log << "[pose-layer] failed to write recording: " << err.message << "\n";
            }
        }
        if (state->down.destroyInstance) {
            r = state->down.destroyInstance(instance);
        }
    }
    return r;
}

// name -> hooked function, for getInstanceProcAddr.
PFN_xrVoidFunction hookedFunction(const char* name) {
    const std::string n = name ? name : "";
    auto as = [](auto fn) { return reinterpret_cast<PFN_xrVoidFunction>(fn); };
    if (n == "xrGetInstanceProcAddr") {
        return as(&getInstanceProcAddr);
    }
    if (n == "xrDestroyInstance") {
        return as(&hookDestroyInstance);
    }
    if (n == "xrWaitFrame") {
        return as(&hookWaitFrame);
    }
    if (n == "xrLocateViews") {
        return as(&hookLocateViews);
    }
    if (n == "xrLocateSpace") {
        return as(&hookLocateSpace);
    }
    if (n == "xrLocateSpaces") {
        return as(&hookLocateSpaces);
    }
    if (n == "xrSyncActions") {
        return as(&hookSyncActions);
    }
    if (n == "xrGetActionStatePose") {
        return as(&hookGetActionStatePose);
    }
    if (n == "xrGetActionStateBoolean") {
        return as(&hookGetActionStateBoolean);
    }
    if (n == "xrGetActionStateFloat") {
        return as(&hookGetActionStateFloat);
    }
    if (n == "xrGetActionStateVector2f") {
        return as(&hookGetActionStateVector2f);
    }
    if (n == "xrEndFrame") {
        return as(&hookEndFrame);
    }
    return nullptr;
}

template <typename Fn> bool resolve(PFN_xrGetInstanceProcAddr getProc, XrInstance instance, const char* name, Fn& out) {
    PFN_xrVoidFunction fn = nullptr;
    if (getProc(instance, name, &fn) == XR_SUCCESS && fn) {
        out = reinterpret_cast<Fn>(fn);
        return true;
    }
    return false;
}

} // namespace

XRAPI_ATTR XrResult XRAPI_CALL getInstanceProcAddr(XrInstance instance, const char* name, PFN_xrVoidFunction* function) {
    if (!function) {
        return XR_ERROR_VALIDATION_FAILURE;
    }
    *function = nullptr;

    if (PFN_xrVoidFunction hooked = hookedFunction(name)) {
        *function = hooked;
        return XR_SUCCESS;
    }

    InstanceState* s = instance != XR_NULL_HANDLE ? Registry::get().forInstance(instance) : nullptr;
    PFN_xrGetInstanceProcAddr down = s ? s->down.getInstanceProcAddr : Registry::get().fallbackGetProcAddr();
    if (!down) {
        return XR_ERROR_FUNCTION_UNSUPPORTED;
    }
    return down(instance, name, function);
}

XRAPI_ATTR XrResult XRAPI_CALL createApiLayerInstance(const XrInstanceCreateInfo* info, const XrApiLayerCreateInfo* apiLayerInfo,
                                                      XrInstance* instance) {
    if (!apiLayerInfo || !apiLayerInfo->nextInfo || !instance) {
        return XR_ERROR_VALIDATION_FAILURE;
    }
    XrApiLayerNextInfo* nextInfo = apiLayerInfo->nextInfo;
    if (nextInfo->structType != XR_LOADER_INTERFACE_STRUCT_API_LAYER_NEXT_INFO || !nextInfo->nextGetInstanceProcAddr ||
        !nextInfo->nextCreateApiLayerInstance) {
        return XR_ERROR_VALIDATION_FAILURE;
    }

    // Pass the rest of the chain down: copy the create info and advance nextInfo
    // to the next layer's, so the layer below us sees itself at the head.
    XrApiLayerCreateInfo nextCreateInfo = *apiLayerInfo;
    nextCreateInfo.nextInfo = nextInfo->next;

    Registry::get().setFallbackGetProcAddr(nextInfo->nextGetInstanceProcAddr);

    const XrResult r = nextInfo->nextCreateApiLayerInstance(info, &nextCreateInfo, instance);
    if (XR_FAILED(r)) {
        return r;
    }

    InstanceState* s = Registry::get().create(*instance);
    s->config = g_configOverride ? *g_configOverride : Config::fromEnvironment();

    PFN_xrGetInstanceProcAddr getProc = nextInfo->nextGetInstanceProcAddr;
    s->down.getInstanceProcAddr = getProc;
    resolve(getProc, *instance, "xrDestroyInstance", s->down.destroyInstance);
    resolve(getProc, *instance, "xrWaitFrame", s->down.waitFrame);
    resolve(getProc, *instance, "xrLocateViews", s->down.locateViews);
    resolve(getProc, *instance, "xrLocateSpace", s->down.locateSpace);
    resolve(getProc, *instance, "xrLocateSpaces", s->down.locateSpaces);
    resolve(getProc, *instance, "xrSyncActions", s->down.syncActions);
    resolve(getProc, *instance, "xrGetActionStatePose", s->down.getActionStatePose);
    resolve(getProc, *instance, "xrGetActionStateBoolean", s->down.getActionStateBoolean);
    resolve(getProc, *instance, "xrGetActionStateFloat", s->down.getActionStateFloat);
    resolve(getProc, *instance, "xrGetActionStateVector2f", s->down.getActionStateVector2f);
    resolve(getProc, *instance, "xrEndFrame", s->down.endFrame);

    // Bring the mode online: open the log, or load and arm the recording.
    if (!s->config.valid) {
        s->config.mode = Mode::Off; // an unparsable config disables the layer
    }
    if (s->config.mode == Mode::Log) {
        if (!s->config.logPath.empty()) {
            s->logFile.open(s->config.logPath, std::ios::trunc);
            if (s->logFile) {
                s->log = &s->logFile;
            }
        }
        if (!s->log) {
            s->log = &std::clog;
        }
        *s->log << "[pose-layer] " POSELAYER_VERSION_STRING " log mode\n";
        s->log->flush();
    } else if (s->config.mode == Mode::Replay) {
        LoadError err;
        s->loaded = Recording::load(s->config.file, &err);
        if (s->loaded) {
            ReplaySettings settings;
            settings.timeScale = s->config.timeScale;
            settings.loop = s->config.loop;
            settings.offset = s->config.offset;
            s->cursor.emplace(*s->loaded, settings);
        } else {
            s->config.mode = Mode::Off; // nothing to replay
        }
    }

    return r;
}

XrResult negotiate(const XrNegotiateLoaderInfo* loaderInfo, const char* /*layerName*/, XrNegotiateApiLayerRequest* apiLayerRequest) {
    if (!loaderInfo || !apiLayerRequest) {
        return XR_ERROR_INITIALIZATION_FAILED;
    }
    if (loaderInfo->structType != XR_LOADER_INTERFACE_STRUCT_LOADER_INFO || loaderInfo->structVersion != XR_LOADER_INFO_STRUCT_VERSION ||
        loaderInfo->structSize != sizeof(XrNegotiateLoaderInfo)) {
        return XR_ERROR_INITIALIZATION_FAILED;
    }
    if (apiLayerRequest->structType != XR_LOADER_INTERFACE_STRUCT_API_LAYER_REQUEST ||
        apiLayerRequest->structVersion != XR_API_LAYER_INFO_STRUCT_VERSION ||
        apiLayerRequest->structSize != sizeof(XrNegotiateApiLayerRequest)) {
        return XR_ERROR_INITIALIZATION_FAILED;
    }
    if (XR_CURRENT_LOADER_API_LAYER_VERSION < loaderInfo->minInterfaceVersion ||
        XR_CURRENT_LOADER_API_LAYER_VERSION > loaderInfo->maxInterfaceVersion) {
        return XR_ERROR_INITIALIZATION_FAILED;
    }

    apiLayerRequest->layerInterfaceVersion = XR_CURRENT_LOADER_API_LAYER_VERSION;
    apiLayerRequest->layerApiVersion = XR_CURRENT_API_VERSION;
    apiLayerRequest->getInstanceProcAddr = &getInstanceProcAddr;
    apiLayerRequest->createApiLayerInstance = &createApiLayerInstance;
    return XR_SUCCESS;
}

namespace testing {
void setConfigOverride(const Config* config) {
    g_configOverride = config;
}
} // namespace testing

} // namespace poselayer
