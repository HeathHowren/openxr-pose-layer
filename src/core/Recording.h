// The on-disk record format and its in-memory form.
//
// A recording is a list of frames. A frame is one xrWaitFrame: it carries the
// frame index the layer assigned (0, 1, 2, ...) and the predicted display time
// that xrWaitFrame returned, then the poses and input states the app read for
// that frame. Entries within a frame are stored in the order the app asked for
// them; replay hands them back in that same order (see Replay.h), so nothing is
// keyed on an XrSpace or XrAction handle, which would not survive a new run.
#pragma once

#include "core/Transform.h"

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace poselayer {

// One eye/view from xrLocateViews.
struct RecordedView {
    XrPosef pose = kPoseIdentity;
    XrFovf fov = {0.0f, 0.0f, 0.0f, 0.0f};
};

// The result of one data-carrying xrLocateViews call.
struct ViewSet {
    uint64_t viewStateFlags = 0;
    std::vector<RecordedView> views;
};

// The result of one xrLocateSpace call.
struct SpaceLocation {
    uint64_t locationFlags = 0;
    XrPosef pose = kPoseIdentity;
};

// The result of one xrLocateSpaces call (OpenXR 1.1), which locates many spaces
// against one base at once.
struct SpacesLocation {
    std::vector<SpaceLocation> locations;
};

struct PoseInput {
    bool isActive = false;
};

struct BooleanInput {
    bool isActive = false;
    bool currentState = false;
    bool changedSinceLastSync = false;
    int64_t lastChangeTime = 0;
};

struct FloatInput {
    bool isActive = false;
    bool changedSinceLastSync = false;
    float currentState = 0.0f;
    int64_t lastChangeTime = 0;
};

struct Vector2fInput {
    bool isActive = false;
    bool changedSinceLastSync = false;
    XrVector2f currentState = {0.0f, 0.0f};
    int64_t lastChangeTime = 0;
};

struct FrameRecord {
    uint32_t frameIndex = 0;
    int64_t predictedDisplayTime = 0;
    std::vector<ViewSet> viewSets;
    std::vector<SpaceLocation> spaces;
    std::vector<SpacesLocation> spacesGroups;
    std::vector<PoseInput> poseInputs;
    std::vector<BooleanInput> booleanInputs;
    std::vector<FloatInput> floatInputs;
    std::vector<Vector2fInput> vector2fInputs;

    size_t entryCount() const;
};

struct LoadError {
    std::string message;
};

class Recording {
public:
    static constexpr uint32_t kFormatVersion = 1;

    std::vector<FrameRecord> frames;

    size_t frameCount() const { return frames.size(); }
    bool empty() const { return frames.empty(); }

    // Nanosecond span from the first to the last frame's predicted display time,
    // or 0 for fewer than two frames.
    int64_t durationNanos() const;

    std::vector<uint8_t> encode() const;
    static std::optional<Recording> decode(std::span<const uint8_t> bytes, LoadError* error = nullptr);

    bool save(const std::string& path, LoadError* error = nullptr) const;
    static std::optional<Recording> load(const std::string& path, LoadError* error = nullptr);
};

} // namespace poselayer
