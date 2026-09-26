#include "core/Recording.h"

#include "Support.h"

#include <catch2/catch_test_macros.hpp>

using namespace poselayer;

namespace {

Recording twoFrames() {
    Recording rec;

    FrameRecord f0;
    f0.frameIndex = 0;
    f0.predictedDisplayTime = 1000;
    ViewSet views;
    views.viewStateFlags = 0xF;
    views.views.push_back({{{0.0f, 0.0f, 0.0f, 1.0f}, {0.1f, 1.6f, -0.2f}}, {-0.7f, 0.7f, 0.7f, -0.7f}});
    views.views.push_back({{{0.0f, 0.0f, 0.0f, 1.0f}, {0.16f, 1.6f, -0.2f}}, {-0.7f, 0.7f, 0.7f, -0.7f}});
    f0.viewSets.push_back(views);
    f0.spaces.push_back({0x3, {{0.0f, 0.0f, 0.0f, 1.0f}, {0.3f, 1.3f, -0.4f}}});
    f0.booleanInputs.push_back({true, false, false, 900});
    f0.floatInputs.push_back({true, false, 0.25f, 900});
    rec.frames.push_back(f0);

    FrameRecord f1;
    f1.frameIndex = 1;
    f1.predictedDisplayTime = 1000 + 11111111;
    f1.spacesGroups.push_back({{{0x3, {{0.0f, 0.0f, 0.0f, 1.0f}, {1.0f, 2.0f, 3.0f}}}, {0x3, {{0.0f, 0.0f, 0.0f, 1.0f}, {4.0f, 5.0f, 6.0f}}}}});
    f1.poseInputs.push_back({true});
    f1.vector2fInputs.push_back({true, true, {0.5f, -0.5f}, 950});
    rec.frames.push_back(f1);

    return rec;
}

bool sameFrame(const FrameRecord& a, const FrameRecord& b) {
    if (a.frameIndex != b.frameIndex || a.predictedDisplayTime != b.predictedDisplayTime) {
        return false;
    }
    if (a.viewSets.size() != b.viewSets.size() || a.spaces.size() != b.spaces.size() || a.spacesGroups.size() != b.spacesGroups.size() ||
        a.poseInputs.size() != b.poseInputs.size() || a.booleanInputs.size() != b.booleanInputs.size() ||
        a.floatInputs.size() != b.floatInputs.size() || a.vector2fInputs.size() != b.vector2fInputs.size()) {
        return false;
    }
    for (size_t i = 0; i < a.viewSets.size(); ++i) {
        if (a.viewSets[i].viewStateFlags != b.viewSets[i].viewStateFlags) {
            return false;
        }
        if (a.viewSets[i].views.size() != b.viewSets[i].views.size()) {
            return false;
        }
        for (size_t v = 0; v < a.viewSets[i].views.size(); ++v) {
            if (!nearlyEqual(a.viewSets[i].views[v].pose, b.viewSets[i].views[v].pose)) {
                return false;
            }
        }
    }
    for (size_t i = 0; i < a.spaces.size(); ++i) {
        if (a.spaces[i].locationFlags != b.spaces[i].locationFlags || !nearlyEqual(a.spaces[i].pose, b.spaces[i].pose)) {
            return false;
        }
    }
    return true;
}

} // namespace

TEST_CASE("a recording round-trips through encode and decode", "[recording]") {
    const Recording original = twoFrames();
    const std::vector<uint8_t> bytes = original.encode();

    LoadError err;
    const std::optional<Recording> decoded = Recording::decode(bytes, &err);
    REQUIRE(decoded);
    REQUIRE(decoded->frameCount() == original.frameCount());
    for (size_t i = 0; i < original.frameCount(); ++i) {
        INFO("frame " << i);
        CHECK(sameFrame(decoded->frames[i], original.frames[i]));
    }
    CHECK(decoded->frames[1].spacesGroups.at(0).locations.size() == 2);
    CHECK(decoded->frames[1].vector2fInputs.at(0).currentState.x == 0.5f);
    CHECK(decoded->frames[1].poseInputs.at(0).isActive);
}

TEST_CASE("duration is the span between first and last predicted display time", "[recording]") {
    const Recording rec = twoFrames();
    CHECK(rec.durationNanos() == 11111111);

    Recording single;
    single.frames.push_back({});
    CHECK(single.durationNanos() == 0);
}

TEST_CASE("decode rejects bad magic", "[recording]") {
    std::vector<uint8_t> bytes = {'N', 'O', 'P', 'E', 0, 0, 0, 0, 1, 0, 0, 0};
    LoadError err;
    CHECK_FALSE(Recording::decode(bytes, &err));
    CHECK(err.message.find("magic") != std::string::npos);
}

TEST_CASE("decode rejects a truncated recording", "[recording]") {
    const std::vector<uint8_t> full = twoFrames().encode();
    const std::vector<uint8_t> cut(full.begin(), full.begin() + full.size() / 2);
    LoadError err;
    CHECK_FALSE(Recording::decode(cut, &err));
    CHECK_FALSE(err.message.empty());
}

TEST_CASE("save then load returns the same recording", "[recording]") {
    const Recording original = support::makeSampleRecording(30);
    const std::string path = std::string(POSELAYER_TEST_TMP) + "/roundtrip.oxrr";

    LoadError err;
    REQUIRE(original.save(path, &err));
    const std::optional<Recording> loaded = Recording::load(path, &err);
    REQUIRE(loaded);
    REQUIRE(loaded->frameCount() == 30);
    CHECK(sameFrame(loaded->frames[15], original.frames[15]));
}

TEST_CASE("loading a missing file fails cleanly", "[recording]") {
    LoadError err;
    CHECK_FALSE(Recording::load(std::string(POSELAYER_TEST_TMP) + "/does-not-exist.oxrr", &err));
    CHECK_FALSE(err.message.empty());
}
