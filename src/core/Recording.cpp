#include "core/Recording.h"

#include <cstring>
#include <fstream>

namespace poselayer {

namespace {

constexpr char kMagic[8] = {'O', 'X', 'R', 'P', 'O', 'S', 'E', '\x1A'};

enum class EntryKind : uint16_t {
    View = 1,
    Space = 2,
    Spaces = 3,
    PoseInput = 4,
    BooleanInput = 5,
    FloatInput = 6,
    Vector2fInput = 7,
};

// Little-endian writer. The format is fixed little-endian so a recording made
// on one machine reads back the same on another, rather than depending on host
// byte order.
class ByteWriter {
public:
    void u8(uint8_t v) { bytes_.push_back(v); }
    void u16(uint16_t v) {
        u8(static_cast<uint8_t>(v));
        u8(static_cast<uint8_t>(v >> 8));
    }
    void u32(uint32_t v) {
        for (int i = 0; i < 4; ++i) {
            u8(static_cast<uint8_t>(v >> (8 * i)));
        }
    }
    void u64(uint64_t v) {
        for (int i = 0; i < 8; ++i) {
            u8(static_cast<uint8_t>(v >> (8 * i)));
        }
    }
    void i64(int64_t v) { u64(static_cast<uint64_t>(v)); }
    void f32(float v) {
        uint32_t bits = 0;
        std::memcpy(&bits, &v, sizeof(bits));
        u32(bits);
    }
    void raw(const char* p, size_t n) { bytes_.insert(bytes_.end(), p, p + n); }

    void pose(const XrPosef& p) {
        f32(p.orientation.x);
        f32(p.orientation.y);
        f32(p.orientation.z);
        f32(p.orientation.w);
        f32(p.position.x);
        f32(p.position.y);
        f32(p.position.z);
    }
    void fov(const XrFovf& f) {
        f32(f.angleLeft);
        f32(f.angleRight);
        f32(f.angleUp);
        f32(f.angleDown);
    }

    std::vector<uint8_t> take() { return std::move(bytes_); }

private:
    std::vector<uint8_t> bytes_;
};

// Bounds-checked little-endian reader. Every read past the end sets a sticky
// failure flag, so a truncated file is rejected rather than read as garbage.
class ByteReader {
public:
    explicit ByteReader(std::span<const uint8_t> bytes) : bytes_(bytes) {}

    bool ok() const { return ok_; }
    bool atEnd() const { return pos_ >= bytes_.size(); }

    uint8_t u8() {
        if (pos_ + 1 > bytes_.size()) {
            ok_ = false;
            return 0;
        }
        return bytes_[pos_++];
    }
    uint16_t u16() {
        const uint16_t lo = u8();
        const uint16_t hi = u8();
        return static_cast<uint16_t>(lo | (hi << 8));
    }
    uint32_t u32() {
        uint32_t v = 0;
        for (int i = 0; i < 4; ++i) {
            v |= static_cast<uint32_t>(u8()) << (8 * i);
        }
        return v;
    }
    uint64_t u64() {
        uint64_t v = 0;
        for (int i = 0; i < 8; ++i) {
            v |= static_cast<uint64_t>(u8()) << (8 * i);
        }
        return v;
    }
    int64_t i64() { return static_cast<int64_t>(u64()); }
    float f32() {
        const uint32_t bits = u32();
        float v = 0.0f;
        std::memcpy(&v, &bits, sizeof(v));
        return v;
    }
    bool magic(const char* expected, size_t n) {
        if (pos_ + n > bytes_.size()) {
            ok_ = false;
            return false;
        }
        const bool match = std::memcmp(bytes_.data() + pos_, expected, n) == 0;
        pos_ += n;
        return match;
    }

    XrPosef pose() {
        XrPosef p;
        p.orientation.x = f32();
        p.orientation.y = f32();
        p.orientation.z = f32();
        p.orientation.w = f32();
        p.position.x = f32();
        p.position.y = f32();
        p.position.z = f32();
        return p;
    }
    XrFovf fov() {
        XrFovf f;
        f.angleLeft = f32();
        f.angleRight = f32();
        f.angleUp = f32();
        f.angleDown = f32();
        return f;
    }

private:
    std::span<const uint8_t> bytes_;
    size_t pos_ = 0;
    bool ok_ = true;
};

void fail(LoadError* error, const char* message) {
    if (error) {
        error->message = message;
    }
}

} // namespace

size_t FrameRecord::entryCount() const {
    return viewSets.size() + spaces.size() + spacesGroups.size() + poseInputs.size() + booleanInputs.size() + floatInputs.size() +
           vector2fInputs.size();
}

int64_t Recording::durationNanos() const {
    if (frames.size() < 2) {
        return 0;
    }
    return frames.back().predictedDisplayTime - frames.front().predictedDisplayTime;
}

std::vector<uint8_t> Recording::encode() const {
    ByteWriter w;
    w.raw(kMagic, sizeof(kMagic));
    w.u32(kFormatVersion);
    w.u32(0); // reserved flags
    w.u64(frames.size());

    for (const FrameRecord& frame : frames) {
        w.u32(frame.frameIndex);
        w.i64(frame.predictedDisplayTime);
        w.u32(static_cast<uint32_t>(frame.entryCount()));

        for (const ViewSet& set : frame.viewSets) {
            w.u16(static_cast<uint16_t>(EntryKind::View));
            w.u64(set.viewStateFlags);
            w.u32(static_cast<uint32_t>(set.views.size()));
            for (const RecordedView& view : set.views) {
                w.pose(view.pose);
                w.fov(view.fov);
            }
        }
        for (const SpaceLocation& space : frame.spaces) {
            w.u16(static_cast<uint16_t>(EntryKind::Space));
            w.u64(space.locationFlags);
            w.pose(space.pose);
        }
        for (const SpacesLocation& group : frame.spacesGroups) {
            w.u16(static_cast<uint16_t>(EntryKind::Spaces));
            w.u32(static_cast<uint32_t>(group.locations.size()));
            for (const SpaceLocation& space : group.locations) {
                w.u64(space.locationFlags);
                w.pose(space.pose);
            }
        }
        for (const PoseInput& input : frame.poseInputs) {
            w.u16(static_cast<uint16_t>(EntryKind::PoseInput));
            w.u8(input.isActive ? 1 : 0);
        }
        for (const BooleanInput& input : frame.booleanInputs) {
            w.u16(static_cast<uint16_t>(EntryKind::BooleanInput));
            w.u8(input.isActive ? 1 : 0);
            w.u8(input.currentState ? 1 : 0);
            w.u8(input.changedSinceLastSync ? 1 : 0);
            w.i64(input.lastChangeTime);
        }
        for (const FloatInput& input : frame.floatInputs) {
            w.u16(static_cast<uint16_t>(EntryKind::FloatInput));
            w.u8(input.isActive ? 1 : 0);
            w.u8(input.changedSinceLastSync ? 1 : 0);
            w.f32(input.currentState);
            w.i64(input.lastChangeTime);
        }
        for (const Vector2fInput& input : frame.vector2fInputs) {
            w.u16(static_cast<uint16_t>(EntryKind::Vector2fInput));
            w.u8(input.isActive ? 1 : 0);
            w.u8(input.changedSinceLastSync ? 1 : 0);
            w.f32(input.currentState.x);
            w.f32(input.currentState.y);
            w.i64(input.lastChangeTime);
        }
    }
    return w.take();
}

std::optional<Recording> Recording::decode(std::span<const uint8_t> bytes, LoadError* error) {
    ByteReader r(bytes);
    if (!r.magic(kMagic, sizeof(kMagic))) {
        fail(error, "not a pose recording (bad magic)");
        return std::nullopt;
    }
    const uint32_t version = r.u32();
    if (version != kFormatVersion) {
        fail(error, "unsupported recording version");
        return std::nullopt;
    }
    (void)r.u32(); // reserved flags
    const uint64_t frameCount = r.u64();

    Recording rec;
    rec.frames.reserve(static_cast<size_t>(frameCount));

    for (uint64_t f = 0; f < frameCount; ++f) {
        FrameRecord frame;
        frame.frameIndex = r.u32();
        frame.predictedDisplayTime = r.i64();
        const uint32_t entries = r.u32();
        if (!r.ok()) {
            fail(error, "truncated recording (frame header)");
            return std::nullopt;
        }
        for (uint32_t e = 0; e < entries; ++e) {
            const auto kind = static_cast<EntryKind>(r.u16());
            switch (kind) {
            case EntryKind::View: {
                ViewSet set;
                set.viewStateFlags = r.u64();
                const uint32_t viewCount = r.u32();
                set.views.reserve(viewCount);
                for (uint32_t v = 0; v < viewCount; ++v) {
                    RecordedView view;
                    view.pose = r.pose();
                    view.fov = r.fov();
                    set.views.push_back(view);
                }
                frame.viewSets.push_back(std::move(set));
                break;
            }
            case EntryKind::Space: {
                SpaceLocation space;
                space.locationFlags = r.u64();
                space.pose = r.pose();
                frame.spaces.push_back(space);
                break;
            }
            case EntryKind::Spaces: {
                SpacesLocation group;
                const uint32_t count = r.u32();
                group.locations.reserve(count);
                for (uint32_t s = 0; s < count; ++s) {
                    SpaceLocation space;
                    space.locationFlags = r.u64();
                    space.pose = r.pose();
                    group.locations.push_back(space);
                }
                frame.spacesGroups.push_back(std::move(group));
                break;
            }
            case EntryKind::PoseInput: {
                PoseInput input;
                input.isActive = r.u8() != 0;
                frame.poseInputs.push_back(input);
                break;
            }
            case EntryKind::BooleanInput: {
                BooleanInput input;
                input.isActive = r.u8() != 0;
                input.currentState = r.u8() != 0;
                input.changedSinceLastSync = r.u8() != 0;
                input.lastChangeTime = r.i64();
                frame.booleanInputs.push_back(input);
                break;
            }
            case EntryKind::FloatInput: {
                FloatInput input;
                input.isActive = r.u8() != 0;
                input.changedSinceLastSync = r.u8() != 0;
                input.currentState = r.f32();
                input.lastChangeTime = r.i64();
                frame.floatInputs.push_back(input);
                break;
            }
            case EntryKind::Vector2fInput: {
                Vector2fInput input;
                input.isActive = r.u8() != 0;
                input.changedSinceLastSync = r.u8() != 0;
                input.currentState.x = r.f32();
                input.currentState.y = r.f32();
                input.lastChangeTime = r.i64();
                frame.vector2fInputs.push_back(input);
                break;
            }
            default:
                fail(error, "unknown entry kind in recording");
                return std::nullopt;
            }
            if (!r.ok()) {
                fail(error, "truncated recording (frame body)");
                return std::nullopt;
            }
        }
        rec.frames.push_back(std::move(frame));
    }

    if (!r.ok()) {
        fail(error, "truncated recording");
        return std::nullopt;
    }
    return rec;
}

bool Recording::save(const std::string& path, LoadError* error) const {
    const std::vector<uint8_t> bytes = encode();
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        fail(error, "could not open output file");
        return false;
    }
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!out) {
        fail(error, "could not write recording");
        return false;
    }
    return true;
}

std::optional<Recording> Recording::load(const std::string& path, LoadError* error) {
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in) {
        fail(error, "could not open recording file");
        return std::nullopt;
    }
    const std::streamoff size = in.tellg();
    if (size < 0) {
        fail(error, "could not size recording file");
        return std::nullopt;
    }
    in.seekg(0);
    std::vector<uint8_t> bytes(static_cast<size_t>(size));
    if (size > 0) {
        in.read(reinterpret_cast<char*>(bytes.data()), size);
        if (!in) {
            fail(error, "could not read recording file");
            return std::nullopt;
        }
    }
    return decode(bytes, error);
}

} // namespace poselayer
