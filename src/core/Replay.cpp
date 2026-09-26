#include "core/Replay.h"

#include <cmath>

namespace poselayer {

ReplayCursor::ReplayCursor(const Recording& recording, const ReplaySettings& settings)
    : recording_(recording), settings_(settings) {}

const FrameRecord* ReplayCursor::advance() {
    if (recording_.frames.empty()) {
        current_ = nullptr;
        return nullptr;
    }

    const double scaled = static_cast<double>(appFrame_) * settings_.timeScale;
    long long selected = static_cast<long long>(std::floor(scaled));
    if (selected < 0) {
        selected = 0;
    }

    const long long count = static_cast<long long>(recording_.frames.size());
    if (settings_.loop) {
        selected %= count;
    } else if (selected >= count) {
        selected = count - 1;
    }

    mappedIndex_ = static_cast<uint32_t>(selected);
    current_ = &recording_.frames[static_cast<size_t>(selected)];

    viewCursor_ = 0;
    spaceCursor_ = 0;
    spacesGroupCursor_ = 0;
    poseCursor_ = 0;
    booleanCursor_ = 0;
    floatCursor_ = 0;
    vector2fCursor_ = 0;

    ++appFrame_;
    return current_;
}

const ViewSet* ReplayCursor::nextViewSet() {
    if (!current_ || viewCursor_ >= current_->viewSets.size()) {
        return nullptr;
    }
    return &current_->viewSets[viewCursor_++];
}

const SpaceLocation* ReplayCursor::nextSpace() {
    if (!current_ || spaceCursor_ >= current_->spaces.size()) {
        return nullptr;
    }
    return &current_->spaces[spaceCursor_++];
}

const SpacesLocation* ReplayCursor::nextSpacesGroup() {
    if (!current_ || spacesGroupCursor_ >= current_->spacesGroups.size()) {
        return nullptr;
    }
    return &current_->spacesGroups[spacesGroupCursor_++];
}

const PoseInput* ReplayCursor::nextPoseInput() {
    if (!current_ || poseCursor_ >= current_->poseInputs.size()) {
        return nullptr;
    }
    return &current_->poseInputs[poseCursor_++];
}

const BooleanInput* ReplayCursor::nextBooleanInput() {
    if (!current_ || booleanCursor_ >= current_->booleanInputs.size()) {
        return nullptr;
    }
    return &current_->booleanInputs[booleanCursor_++];
}

const FloatInput* ReplayCursor::nextFloatInput() {
    if (!current_ || floatCursor_ >= current_->floatInputs.size()) {
        return nullptr;
    }
    return &current_->floatInputs[floatCursor_++];
}

const Vector2fInput* ReplayCursor::nextVector2fInput() {
    if (!current_ || vector2fCursor_ >= current_->vector2fInputs.size()) {
        return nullptr;
    }
    return &current_->vector2fInputs[vector2fCursor_++];
}

} // namespace poselayer
