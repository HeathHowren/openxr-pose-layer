#include "core/Transform.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>

using namespace poselayer;

TEST_CASE("identity offset leaves a pose unchanged", "[transform]") {
    const XrPosef pose = {{0.1f, 0.2f, 0.3f, 0.927f}, {1.0f, 2.0f, 3.0f}};
    CHECK(nearlyEqual(applyOffset(kPoseIdentity, pose), pose));
}

TEST_CASE("a translation offset adds in the offset's frame", "[transform]") {
    const XrPosef offset = {{0.0f, 0.0f, 0.0f, 1.0f}, {10.0f, 0.0f, -5.0f}};
    const XrPosef pose = {{0.0f, 0.0f, 0.0f, 1.0f}, {1.0f, 2.0f, 3.0f}};
    const XrPosef out = applyOffset(offset, pose);
    CHECK(nearlyEqual(out.position.x, 11.0f));
    CHECK(nearlyEqual(out.position.y, 2.0f));
    CHECK(nearlyEqual(out.position.z, -2.0f));
}

TEST_CASE("a 90 degree yaw offset rotates position and orientation", "[transform]") {
    const XrQuaternionf yaw = quatFromYawDegrees(90.0f);
    const XrPosef offset = {yaw, {0.0f, 0.0f, 0.0f}};
    // A point one metre along +Z, yawed +90 degrees about +Y, lands on +X.
    const XrPosef pose = {{0.0f, 0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 1.0f}};
    const XrPosef out = applyOffset(offset, pose);
    CHECK(nearlyEqual(out.position.x, 1.0f, 1e-4f));
    CHECK(nearlyEqual(out.position.y, 0.0f, 1e-4f));
    CHECK(nearlyEqual(out.position.z, 0.0f, 1e-4f));
}

TEST_CASE("quatRotate turns +X into +Z under a -90 degree yaw", "[transform]") {
    const XrQuaternionf yaw = quatFromYawDegrees(-90.0f);
    const XrVector3f v = quatRotate(yaw, {1.0f, 0.0f, 0.0f});
    CHECK(nearlyEqual(v.x, 0.0f, 1e-4f));
    CHECK(nearlyEqual(v.z, 1.0f, 1e-4f));
}

TEST_CASE("quaternion multiply composes rotations", "[transform]") {
    const XrQuaternionf a = quatFromYawDegrees(30.0f);
    const XrQuaternionf b = quatFromYawDegrees(60.0f);
    const XrQuaternionf full = quatFromYawDegrees(90.0f);
    const XrQuaternionf composed = quatNormalize(quatMultiply(a, b));
    CHECK(nearlyEqual(composed.x, full.x, 1e-4f));
    CHECK(nearlyEqual(composed.y, full.y, 1e-4f));
    CHECK(nearlyEqual(composed.z, full.z, 1e-4f));
    CHECK(nearlyEqual(composed.w, full.w, 1e-4f));
}

TEST_CASE("normalizing a zero quaternion returns identity rather than NaN", "[transform]") {
    const XrQuaternionf q = quatNormalize({0.0f, 0.0f, 0.0f, 0.0f});
    CHECK(nearlyEqual(q.w, 1.0f));
    CHECK_FALSE(std::isnan(q.x));
}
