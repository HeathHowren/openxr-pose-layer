#include "core/Transform.h"

#include <cmath>

namespace poselayer {

XrQuaternionf quatMultiply(const XrQuaternionf& a, const XrQuaternionf& b) {
    return {
        a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
        a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
        a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,
        a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z,
    };
}

XrQuaternionf quatNormalize(const XrQuaternionf& q) {
    const float lenSq = q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w;
    if (lenSq <= 0.0f) {
        return {0.0f, 0.0f, 0.0f, 1.0f};
    }
    const float inv = 1.0f / std::sqrt(lenSq);
    return {q.x * inv, q.y * inv, q.z * inv, q.w * inv};
}

XrVector3f quatRotate(const XrQuaternionf& q, const XrVector3f& v) {
    // v + 2 * cross(q.xyz, cross(q.xyz, v) + q.w * v), the standard unit-quaternion
    // rotation with no matrix built.
    const XrVector3f u = {q.x, q.y, q.z};
    const XrVector3f t = {
        2.0f * (u.y * v.z - u.z * v.y),
        2.0f * (u.z * v.x - u.x * v.z),
        2.0f * (u.x * v.y - u.y * v.x),
    };
    return {
        v.x + q.w * t.x + (u.y * t.z - u.z * t.y),
        v.y + q.w * t.y + (u.z * t.x - u.x * t.z),
        v.z + q.w * t.z + (u.x * t.y - u.y * t.x),
    };
}

XrQuaternionf quatFromYawDegrees(float yawDegrees) {
    const float half = yawDegrees * (3.14159265358979323846f / 180.0f) * 0.5f;
    return {0.0f, std::sin(half), 0.0f, std::cos(half)};
}

XrPosef poseCompose(const XrPosef& a, const XrPosef& b) {
    XrPosef out;
    out.orientation = quatMultiply(a.orientation, b.orientation);
    const XrVector3f rotated = quatRotate(a.orientation, b.position);
    out.position = {a.position.x + rotated.x, a.position.y + rotated.y, a.position.z + rotated.z};
    return out;
}

XrPosef applyOffset(const XrPosef& offset, const XrPosef& recorded) {
    return poseCompose(offset, recorded);
}

bool nearlyEqual(float a, float b, float epsilon) {
    return std::fabs(a - b) <= epsilon;
}

bool nearlyEqual(const XrPosef& a, const XrPosef& b, float epsilon) {
    return nearlyEqual(a.orientation.x, b.orientation.x, epsilon) && nearlyEqual(a.orientation.y, b.orientation.y, epsilon) &&
           nearlyEqual(a.orientation.z, b.orientation.z, epsilon) && nearlyEqual(a.orientation.w, b.orientation.w, epsilon) &&
           nearlyEqual(a.position.x, b.position.x, epsilon) && nearlyEqual(a.position.y, b.position.y, epsilon) &&
           nearlyEqual(a.position.z, b.position.z, epsilon);
}

} // namespace poselayer
