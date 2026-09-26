// Pose math for the record/replay path. Everything here works on the OpenXR
// value types (XrPosef, XrQuaternionf, XrVector3f) and never touches the loader
// or an instance, so the tests link it on its own.
#pragma once

#include <openxr/openxr.h>

namespace poselayer {

// The identity pose: no rotation, no translation. XrPosef{} would zero the
// quaternion, which is not a rotation, so callers should start from this.
inline constexpr XrPosef kPoseIdentity = {{0.0f, 0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 0.0f}};

XrQuaternionf quatMultiply(const XrQuaternionf& a, const XrQuaternionf& b);
XrQuaternionf quatNormalize(const XrQuaternionf& q);
XrVector3f quatRotate(const XrQuaternionf& q, const XrVector3f& v);

// A rotation of yawDegrees about +Y, the axis you turn a scene around to face
// the replayed path a different way.
XrQuaternionf quatFromYawDegrees(float yawDegrees);

// b expressed in the frame that a defines: orientation = a.o * b.o, position =
// a.position + rotate(a.o, b.position). Composing kPoseIdentity returns b.
XrPosef poseCompose(const XrPosef& a, const XrPosef& b);

// The offset a replay applies to every recorded pose. With kPoseIdentity the
// recorded pose is returned unchanged.
XrPosef applyOffset(const XrPosef& offset, const XrPosef& recorded);

bool nearlyEqual(float a, float b, float epsilon = 1e-5f);
bool nearlyEqual(const XrPosef& a, const XrPosef& b, float epsilon = 1e-5f);

} // namespace poselayer
