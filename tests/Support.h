// A fake "next layer" and a harness that drives the real layer through it.
//
// The fake implements just the handful of xr* entry points the layer forwards
// to, serving scripted per-frame data. The harness builds a real
// XrApiLayerCreateInfo whose nextInfo points at the fake, calls the layer's
// xrCreateApiLayerInstance exactly as the loader would, then fetches the hooked
// functions through the layer's xrGetInstanceProcAddr and calls them frame by
// frame. No OpenXR runtime and no headset are involved.
#pragma once

#include "core/Config.h"
#include "core/Recording.h"

#include <openxr/openxr.h>
#include <openxr/openxr_loader_negotiation.h>

#include <vector>

namespace poselayer::support {

// The fake runtime below the layer. Its script is a Recording: frame i serves
// the poses and inputs in script.frames[i], in record order per category.
class FakeRuntime {
public:
    Recording script;

    // What xrEndFrame received, per call: the projection-layer view poses the
    // layer forwarded down. Replay tests assert these were rewritten.
    std::vector<std::vector<XrPosef>> submittedViewPoses;

    // Bookkeeping the fake exposes for assertions.
    uint32_t waitFrameCalls = 0;
    uint32_t endFrameCalls = 0;

    void reset();

    // Per-frame serving state, advanced by the fake's xrWaitFrame.
    size_t served = 0;
    size_t viewOrdinal = 0;
    size_t spaceOrdinal = 0;
    size_t spacesGroupOrdinal = 0;
    size_t poseOrdinal = 0;
    size_t booleanOrdinal = 0;
    size_t floatOrdinal = 0;
    size_t vector2fOrdinal = 0;

    const FrameRecord* servedFrame() const;
};

// The one active fake, so the C-linkage fake entry points can reach it. Tests
// run one harness at a time.
FakeRuntime& fake();

// What one driven frame should exercise.
struct FrameOptions {
    bool locateViews = true;
    bool locateSpace = true;
    bool locateSpaces = false;
    bool syncActions = true;
    bool getPose = false;
    bool getBoolean = false;
    bool getFloat = false;
    bool getVector2f = false;
    bool endFrame = true;
    uint32_t projectionViewCount = 2; // views in the submitted projection layer
};

// Drives the real layer through the fake. create() must succeed before
// driveFrame(); destroy() flushes a record-mode recording to its file.
class LayerHarness {
public:
    LayerHarness(FakeRuntime& runtime, const Config& config);
    ~LayerHarness();

    LayerHarness(const LayerHarness&) = delete;
    LayerHarness& operator=(const LayerHarness&) = delete;

    bool create();
    void driveFrame(const FrameOptions& options);
    void destroy();

    XrInstance instance() const { return instance_; }

    // The values the layer returned for the most recent driveFrame(), for
    // assertions. Populated when the matching option was set.
    XrViewState lastViewState{};
    std::vector<XrView> lastViews;
    XrSpaceLocation lastSpace{};
    std::vector<XrSpaceLocation> lastSpacesGroup;
    XrActionStatePose lastPose{};
    XrActionStateBoolean lastBoolean{};
    XrActionStateFloat lastFloat{};
    XrActionStateVector2f lastVector2f{};
    XrFrameState lastFrameState{};

private:
    FakeRuntime& runtime_;
    Config config_;
    XrInstance instance_ = XR_NULL_HANDLE;
    bool created_ = false;

    XrApiLayerNextInfo nextInfo_{};

    PFN_xrGetInstanceProcAddr getProc_ = nullptr;
    PFN_xrDestroyInstance destroyInstance_ = nullptr;
    PFN_xrWaitFrame waitFrame_ = nullptr;
    PFN_xrLocateViews locateViews_ = nullptr;
    PFN_xrLocateSpace locateSpace_ = nullptr;
    PFN_xrLocateSpaces locateSpaces_ = nullptr;
    PFN_xrSyncActions syncActions_ = nullptr;
    PFN_xrGetActionStatePose getPose_ = nullptr;
    PFN_xrGetActionStateBoolean getBoolean_ = nullptr;
    PFN_xrGetActionStateFloat getFloat_ = nullptr;
    PFN_xrGetActionStateVector2f getVector2f_ = nullptr;
    PFN_xrEndFrame endFrame_ = nullptr;
};

// Build a plausible recording: a head that bobs and sways, two eyes offset from
// it, one controller space, and a trigger that presses partway through.
Recording makeSampleRecording(uint32_t frameCount);

} // namespace poselayer::support
