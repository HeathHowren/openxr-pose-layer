// Replay side: turn an app's frame loop back into the recorded poses.
//
// The app calls xrWaitFrame once per frame. The cursor maps that call to a
// recorded frame with time scaling and looping, then hands out that frame's
// entries in the order they were recorded as the app repeats its locate and
// action-state calls. Handles are never matched; call order is.
#pragma once

#include "core/Recording.h"

namespace poselayer {

struct ReplaySettings {
    // Recorded frames advanced per app frame. 1.0 is real speed, 2.0 skips every
    // other recorded frame, 0.5 shows each recorded frame twice.
    double timeScale = 1.0;
    // When true, playback wraps to frame 0 past the end; when false it holds on
    // the last recorded frame.
    bool loop = false;
    // Applied to every replayed pose (see applyOffset). kPoseIdentity is a no-op.
    XrPosef offset = kPoseIdentity;
};

class ReplayCursor {
public:
    ReplayCursor(const Recording& recording, const ReplaySettings& settings);

    bool hasFrames() const { return !recording_.frames.empty(); }

    // Advance to the recorded frame for the next app frame. Call once per
    // xrWaitFrame, before the frame's locate and action-state calls. Returns the
    // recorded frame, or nullptr when the recording is empty.
    const FrameRecord* advance();

    // The current recorded frame after the most recent advance().
    const FrameRecord* current() const { return current_; }

    // The recorded frame index the current app frame maps to.
    uint32_t mappedFrameIndex() const { return mappedIndex_; }

    // Next recorded entry of each kind for the current frame, in record order,
    // or nullptr when the app asks for more than were recorded. Poses come back
    // with the offset already applied.
    const ViewSet* nextViewSet();
    XrPosef offsetView(const XrPosef& recorded) const { return applyOffset(settings_.offset, recorded); }
    const SpaceLocation* nextSpace();
    const SpacesLocation* nextSpacesGroup();
    const PoseInput* nextPoseInput();
    const BooleanInput* nextBooleanInput();
    const FloatInput* nextFloatInput();
    const Vector2fInput* nextVector2fInput();

    const ReplaySettings& settings() const { return settings_; }

private:
    const Recording& recording_;
    ReplaySettings settings_;
    uint64_t appFrame_ = 0;
    uint32_t mappedIndex_ = 0;
    const FrameRecord* current_ = nullptr;

    size_t viewCursor_ = 0;
    size_t spaceCursor_ = 0;
    size_t spacesGroupCursor_ = 0;
    size_t poseCursor_ = 0;
    size_t booleanCursor_ = 0;
    size_t floatCursor_ = 0;
    size_t vector2fCursor_ = 0;
};

} // namespace poselayer
