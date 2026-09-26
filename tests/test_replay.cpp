#include "core/Replay.h"

#include <catch2/catch_test_macros.hpp>

using namespace poselayer;

namespace {

Recording countedFrames(uint32_t n) {
    Recording rec;
    for (uint32_t i = 0; i < n; ++i) {
        FrameRecord f;
        f.frameIndex = i;
        f.predictedDisplayTime = 1000 + static_cast<int64_t>(i) * 10;
        ViewSet set;
        set.views.push_back({{{0.0f, 0.0f, 0.0f, 1.0f}, {static_cast<float>(i), 0.0f, 0.0f}}, {}});
        f.viewSets.push_back(set);
        rec.frames.push_back(f);
    }
    return rec;
}

} // namespace

TEST_CASE("replay maps one app frame to one recorded frame at time scale 1", "[replay]") {
    const Recording rec = countedFrames(5);
    ReplayCursor cursor(rec, {});
    for (uint32_t i = 0; i < 5; ++i) {
        const FrameRecord* f = cursor.advance();
        REQUIRE(f);
        CHECK(cursor.mappedFrameIndex() == i);
    }
}

TEST_CASE("without loop, replay holds on the last frame past the end", "[replay]") {
    const Recording rec = countedFrames(3);
    ReplaySettings settings;
    settings.loop = false;
    ReplayCursor cursor(rec, settings);
    cursor.advance(); // 0
    cursor.advance(); // 1
    cursor.advance(); // 2
    cursor.advance();
    CHECK(cursor.mappedFrameIndex() == 2);
    cursor.advance();
    CHECK(cursor.mappedFrameIndex() == 2);
}

TEST_CASE("with loop, replay wraps to the first frame", "[replay]") {
    const Recording rec = countedFrames(3);
    ReplaySettings settings;
    settings.loop = true;
    ReplayCursor cursor(rec, settings);
    cursor.advance(); // 0
    cursor.advance(); // 1
    cursor.advance(); // 2
    cursor.advance();
    CHECK(cursor.mappedFrameIndex() == 0);
    cursor.advance();
    CHECK(cursor.mappedFrameIndex() == 1);
}

TEST_CASE("a time scale above 1 skips recorded frames", "[replay]") {
    const Recording rec = countedFrames(10);
    ReplaySettings settings;
    settings.timeScale = 2.0;
    ReplayCursor cursor(rec, settings);
    cursor.advance();
    CHECK(cursor.mappedFrameIndex() == 0);
    cursor.advance();
    CHECK(cursor.mappedFrameIndex() == 2);
    cursor.advance();
    CHECK(cursor.mappedFrameIndex() == 4);
}

TEST_CASE("a time scale below 1 repeats recorded frames", "[replay]") {
    const Recording rec = countedFrames(10);
    ReplaySettings settings;
    settings.timeScale = 0.5;
    ReplayCursor cursor(rec, settings);
    cursor.advance();
    CHECK(cursor.mappedFrameIndex() == 0);
    cursor.advance();
    CHECK(cursor.mappedFrameIndex() == 0);
    cursor.advance();
    CHECK(cursor.mappedFrameIndex() == 1);
    cursor.advance();
    CHECK(cursor.mappedFrameIndex() == 1);
}

TEST_CASE("the frame cursor hands out entries in record order then stops", "[replay]") {
    const Recording rec = countedFrames(2);
    ReplayCursor cursor(rec, {});
    cursor.advance();
    const ViewSet* first = cursor.nextViewSet();
    REQUIRE(first);
    CHECK(first->views.size() == 1);
    CHECK(cursor.nextViewSet() == nullptr); // only one recorded this frame
    CHECK(cursor.nextSpace() == nullptr);   // none recorded
}

TEST_CASE("the offset is applied to poses the cursor returns", "[replay]") {
    const Recording rec = countedFrames(2);
    ReplaySettings settings;
    settings.offset = {{0.0f, 0.0f, 0.0f, 1.0f}, {100.0f, 0.0f, 0.0f}};
    ReplayCursor cursor(rec, settings);
    cursor.advance(); // frame 0, view position x == 0
    const ViewSet* set = cursor.nextViewSet();
    REQUIRE(set);
    const XrPosef out = cursor.offsetView(set->views[0].pose);
    CHECK(nearlyEqual(out.position.x, 100.0f));
}

TEST_CASE("an empty recording yields no frames", "[replay]") {
    const Recording rec;
    ReplayCursor cursor(rec, {});
    CHECK_FALSE(cursor.hasFrames());
    CHECK(cursor.advance() == nullptr);
}
