#include "Support.h"

#include "core/Recording.h"

#include <catch2/catch_test_macros.hpp>

#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

using namespace poselayer;
using namespace poselayer::support;

namespace {

std::string tmp(const std::string& name) {
    return std::string(POSELAYER_TEST_TMP) + "/" + name;
}

Config recordConfig(const std::string& file) {
    Config c;
    c.mode = Mode::Record;
    c.file = file;
    return c;
}

Config replayConfig(const std::string& file) {
    Config c;
    c.mode = Mode::Replay;
    c.file = file;
    return c;
}

FrameOptions sampleOptions() {
    FrameOptions o;
    o.locateViews = true;
    o.locateSpace = true;
    o.syncActions = true;
    o.getBoolean = true;
    o.getFloat = true;
    o.endFrame = true;
    return o;
}

// A runtime whose live data differs from any recording, so a replay test can
// tell recorded poses from what the runtime would have returned.
Recording flatRuntime(uint32_t frames) {
    Recording rec;
    for (uint32_t f = 0; f < frames; ++f) {
        FrameRecord frame;
        frame.frameIndex = f;
        frame.predictedDisplayTime = 500 + static_cast<int64_t>(f) * 11111111;
        ViewSet views;
        views.views.push_back({kPoseIdentity, {}});
        views.views.push_back({kPoseIdentity, {}});
        frame.viewSets.push_back(views);
        frame.spaces.push_back({0, kPoseIdentity});
        frame.booleanInputs.push_back({true, false, false, 0});
        frame.floatInputs.push_back({true, false, 0.0f, 0});
        rec.frames.push_back(frame);
    }
    return rec;
}

// Sends std::clog to a string for the life of the object.
class CaptureClog {
public:
    CaptureClog() : old_(std::clog.rdbuf(text_.rdbuf())) {}
    ~CaptureClog() { std::clog.rdbuf(old_); }
    CaptureClog(const CaptureClog&) = delete;
    CaptureClog& operator=(const CaptureClog&) = delete;
    std::string str() const { return text_.str(); }

private:
    std::ostringstream text_;
    std::streambuf* old_;
};

} // namespace

TEST_CASE("record mode reports a recording it cannot write", "[layer]") {
    fake().reset();
    fake().script = makeSampleRecording(2);

    // The folder does not exist, so the file cannot be opened.
    const std::string path = tmp("no-such-folder/record.oxrr");
    CaptureClog clog;
    {
        LayerHarness harness(fake(), recordConfig(path));
        REQUIRE(harness.create());
        harness.driveFrame(sampleOptions());
        harness.driveFrame(sampleOptions());
        harness.destroy();
    }

    const std::string text = clog.str();
    CHECK(text.find("[pose-layer] failed to write recording to '" + path + "'") != std::string::npos);
    CHECK(text.find("could not open output file") != std::string::npos);
}

TEST_CASE("record mode captures the runtime's poses frame by frame", "[layer]") {
    const uint32_t frames = 12;
    fake().reset();
    fake().script = makeSampleRecording(frames);

    const std::string path = tmp("record.oxrr");
    {
        LayerHarness harness(fake(), recordConfig(path));
        REQUIRE(harness.create());
        for (uint32_t f = 0; f < frames; ++f) {
            harness.driveFrame(sampleOptions());
        }
        harness.destroy(); // flushes the recording
    }

    LoadError err;
    const std::optional<Recording> rec = Recording::load(path, &err);
    REQUIRE(rec);
    REQUIRE(rec->frameCount() == frames);

    for (uint32_t f = 0; f < frames; ++f) {
        INFO("frame " << f);
        const FrameRecord& got = rec->frames[f];
        const FrameRecord& want = fake().script.frames[f];
        REQUIRE(got.viewSets.size() == 1);
        REQUIRE(got.viewSets[0].views.size() == 2);
        CHECK(nearlyEqual(got.viewSets[0].views[0].pose, want.viewSets[0].views[0].pose));
        CHECK(nearlyEqual(got.viewSets[0].views[1].pose, want.viewSets[0].views[1].pose));
        REQUIRE(got.spaces.size() == 1);
        CHECK(nearlyEqual(got.spaces[0].pose, want.spaces[0].pose));
        REQUIRE(got.booleanInputs.size() == 1);
        CHECK(got.booleanInputs[0].currentState == want.booleanInputs[0].currentState);
        REQUIRE(got.floatInputs.size() == 1);
        CHECK(got.floatInputs[0].currentState == want.floatInputs[0].currentState);
        CHECK(got.predictedDisplayTime == want.predictedDisplayTime);
    }
}

TEST_CASE("replay returns recorded poses over the runtime's live data", "[layer]") {
    const uint32_t frames = 10;
    const Recording recording = makeSampleRecording(frames);
    const std::string path = tmp("replay.oxrr");
    REQUIRE(recording.save(path));

    fake().reset();
    fake().script = flatRuntime(frames); // runtime would return identity poses

    LayerHarness harness(fake(), replayConfig(path));
    REQUIRE(harness.create());

    for (uint32_t f = 0; f < frames; ++f) {
        harness.driveFrame(sampleOptions());
        INFO("frame " << f);

        REQUIRE(harness.lastViews.size() == 2);
        CHECK(nearlyEqual(harness.lastViews[0].pose, recording.frames[f].viewSets[0].views[0].pose));
        CHECK(nearlyEqual(harness.lastViews[1].pose, recording.frames[f].viewSets[0].views[1].pose));
        CHECK(nearlyEqual(harness.lastSpace.pose, recording.frames[f].spaces[0].pose));
        CHECK((harness.lastBoolean.currentState == XR_TRUE) == recording.frames[f].booleanInputs[0].currentState);
        CHECK(harness.lastFloat.currentState == recording.frames[f].floatInputs[0].currentState);
    }
}

TEST_CASE("replay rewrites the projection view poses submitted in xrEndFrame", "[layer]") {
    const uint32_t frames = 6;
    const Recording recording = makeSampleRecording(frames);
    const std::string path = tmp("replay_endframe.oxrr");
    REQUIRE(recording.save(path));

    fake().reset();
    fake().script = flatRuntime(frames);

    LayerHarness harness(fake(), replayConfig(path));
    REQUIRE(harness.create());
    for (uint32_t f = 0; f < frames; ++f) {
        harness.driveFrame(sampleOptions()); // submits sentinel poses (9,9,9)
    }
    harness.destroy();

    REQUIRE(fake().submittedViewPoses.size() == frames);
    for (uint32_t f = 0; f < frames; ++f) {
        INFO("frame " << f);
        REQUIRE(fake().submittedViewPoses[f].size() == 2);
        // The layer replaced the sentinel with the recorded eye poses.
        CHECK(nearlyEqual(fake().submittedViewPoses[f][0], recording.frames[f].viewSets[0].views[0].pose));
        CHECK(nearlyEqual(fake().submittedViewPoses[f][1], recording.frames[f].viewSets[0].views[1].pose));
    }
}

TEST_CASE("replay applies the offset transform to returned and submitted poses", "[layer]") {
    const uint32_t frames = 4;
    const Recording recording = makeSampleRecording(frames);
    const std::string path = tmp("replay_offset.oxrr");
    REQUIRE(recording.save(path));

    fake().reset();
    fake().script = flatRuntime(frames);

    Config config = replayConfig(path);
    config.offset = {{0.0f, 0.0f, 0.0f, 1.0f}, {100.0f, 0.0f, 0.0f}};

    LayerHarness harness(fake(), config);
    REQUIRE(harness.create());
    for (uint32_t f = 0; f < frames; ++f) {
        harness.driveFrame(sampleOptions());
        INFO("frame " << f);
        const float recordedX = recording.frames[f].viewSets[0].views[0].pose.position.x;
        CHECK(nearlyEqual(harness.lastViews[0].pose.position.x, recordedX + 100.0f, 1e-3f));
    }
    harness.destroy();
    for (uint32_t f = 0; f < frames; ++f) {
        const float recordedX = recording.frames[f].viewSets[0].views[0].pose.position.x;
        CHECK(nearlyEqual(fake().submittedViewPoses[f][0].position.x, recordedX + 100.0f, 1e-3f));
    }
}

TEST_CASE("record and replay carry xrLocateSpaces groups", "[layer]") {
    // A runtime that returns a two-space group each frame.
    Recording script;
    for (uint32_t f = 0; f < 5; ++f) {
        FrameRecord frame;
        frame.frameIndex = f;
        frame.predictedDisplayTime = static_cast<int64_t>(f) * 1000;
        SpacesLocation group;
        group.locations.push_back({0x3, {{0.0f, 0.0f, 0.0f, 1.0f}, {static_cast<float>(f), 1.0f, 0.0f}}});
        group.locations.push_back({0x3, {{0.0f, 0.0f, 0.0f, 1.0f}, {static_cast<float>(f), 2.0f, 0.0f}}});
        frame.spacesGroups.push_back(group);
        script.frames.push_back(frame);
    }

    const std::string path = tmp("spaces.oxrr");
    FrameOptions options;
    options.locateViews = false;
    options.locateSpace = false;
    options.syncActions = false;
    options.locateSpaces = true;
    options.endFrame = false;

    fake().reset();
    fake().script = script;
    {
        LayerHarness harness(fake(), recordConfig(path));
        REQUIRE(harness.create());
        for (uint32_t f = 0; f < 5; ++f) {
            harness.driveFrame(options);
        }
        harness.destroy();
    }

    LoadError err;
    const std::optional<Recording> rec = Recording::load(path, &err);
    REQUIRE(rec);
    REQUIRE(rec->frameCount() == 5);
    for (uint32_t f = 0; f < 5; ++f) {
        REQUIRE(rec->frames[f].spacesGroups.size() == 1);
        REQUIRE(rec->frames[f].spacesGroups[0].locations.size() == 2);
        CHECK(nearlyEqual(rec->frames[f].spacesGroups[0].locations[0].pose, script.frames[f].spacesGroups[0].locations[0].pose));
    }
}

TEST_CASE("log mode writes a call trace naming the hooked functions", "[layer]") {
    fake().reset();
    fake().script = makeSampleRecording(3);

    const std::string logPath = tmp("trace.log");
    Config config;
    config.mode = Mode::Log;
    config.logPath = logPath;

    {
        LayerHarness harness(fake(), config);
        REQUIRE(harness.create());
        harness.driveFrame(sampleOptions());
        harness.driveFrame(sampleOptions());
        harness.destroy();
    }

    std::ifstream in(logPath);
    REQUIRE(in);
    std::stringstream buffer;
    buffer << in.rdbuf();
    const std::string text = buffer.str();
    CHECK(text.find("xrWaitFrame") != std::string::npos);
    CHECK(text.find("xrLocateViews") != std::string::npos);
    CHECK(text.find("xrEndFrame") != std::string::npos);
    CHECK(text.find("XR_SUCCESS") != std::string::npos);
}
