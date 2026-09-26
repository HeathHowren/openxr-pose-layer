// poseplay: inspect, summarize and trim pose recordings written by the layer.
#include "core/Recording.h"

#include "Version.h"

#include <cstdio>
#include <cstring>
#include <limits>
#include <optional>
#include <string>
#include <vector>

using namespace poselayer;

namespace {

struct Bounds {
    float lo = std::numeric_limits<float>::max();
    float hi = std::numeric_limits<float>::lowest();
    void add(float v) {
        if (v < lo) {
            lo = v;
        }
        if (v > hi) {
            hi = v;
        }
    }
    bool valid() const { return lo <= hi; }
};

void printUsage() {
    std::printf("poseplay %s -- inspect and trim OpenXR pose recordings\n\n", POSELAYER_VERSION_STRING);
    std::printf("Usage:\n");
    std::printf("  poseplay info <file> [--json]\n");
    std::printf("  poseplay dump <file> [--frames A-B] [--json]\n");
    std::printf("  poseplay trim <file> --frames A-B -o <out>\n");
    std::printf("  poseplay --version\n");
    std::printf("  poseplay help\n\n");
    std::printf("info   summarize a recording: frames, duration, inputs, pose extents.\n");
    std::printf("dump   print per-frame times and head poses; --json for machine output.\n");
    std::printf("trim   write frames A..B (inclusive) to a new recording, reindexed from 0.\n");
}

// Parse "A-B" or "A" into an inclusive [begin, end] frame range.
bool parseRange(const std::string& text, uint32_t& begin, uint32_t& end) {
    const size_t dash = text.find('-');
    try {
        if (dash == std::string::npos) {
            begin = end = static_cast<uint32_t>(std::stoul(text));
            return true;
        }
        begin = static_cast<uint32_t>(std::stoul(text.substr(0, dash)));
        end = static_cast<uint32_t>(std::stoul(text.substr(dash + 1)));
        return begin <= end;
    } catch (...) {
        return false;
    }
}

std::optional<Recording> loadOrReport(const std::string& path) {
    LoadError err;
    std::optional<Recording> rec = Recording::load(path, &err);
    if (!rec) {
        std::fprintf(stderr, "poseplay: cannot read '%s': %s\n", path.c_str(), err.message.c_str());
    }
    return rec;
}

// The head position for a frame: the mean of the recorded eye positions, or the
// first space location when no views were recorded. Returns false if neither.
bool headPosition(const FrameRecord& frame, XrVector3f& out) {
    if (!frame.viewSets.empty() && !frame.viewSets.front().views.empty()) {
        const ViewSet& set = frame.viewSets.front();
        XrVector3f sum = {0.0f, 0.0f, 0.0f};
        for (const RecordedView& view : set.views) {
            sum.x += view.pose.position.x;
            sum.y += view.pose.position.y;
            sum.z += view.pose.position.z;
        }
        const float n = static_cast<float>(set.views.size());
        out = {sum.x / n, sum.y / n, sum.z / n};
        return true;
    }
    if (!frame.spaces.empty()) {
        out = frame.spaces.front().pose.position;
        return true;
    }
    return false;
}

int cmdInfo(const Recording& rec, const std::string& path, bool json) {
    Bounds bx, by, bz;
    size_t framesWithHead = 0;
    size_t totalViewSets = 0, totalSpaces = 0, totalSpacesGroups = 0;
    size_t totalPose = 0, totalBool = 0, totalFloat = 0, totalVec2 = 0;
    for (const FrameRecord& frame : rec.frames) {
        XrVector3f head;
        if (headPosition(frame, head)) {
            bx.add(head.x);
            by.add(head.y);
            bz.add(head.z);
            ++framesWithHead;
        }
        totalViewSets += frame.viewSets.size();
        totalSpaces += frame.spaces.size();
        totalSpacesGroups += frame.spacesGroups.size();
        totalPose += frame.poseInputs.size();
        totalBool += frame.booleanInputs.size();
        totalFloat += frame.floatInputs.size();
        totalVec2 += frame.vector2fInputs.size();
    }

    const double durationMs = static_cast<double>(rec.durationNanos()) / 1.0e6;
    const double fps = durationMs > 0.0 ? static_cast<double>(rec.frameCount() - 1) / (durationMs / 1000.0) : 0.0;

    if (json) {
        std::printf("{\n");
        std::printf("  \"file\": \"%s\",\n", path.c_str());
        std::printf("  \"formatVersion\": %u,\n", Recording::kFormatVersion);
        std::printf("  \"frames\": %zu,\n", rec.frameCount());
        std::printf("  \"durationMs\": %.3f,\n", durationMs);
        std::printf("  \"fps\": %.2f,\n", fps);
        std::printf("  \"viewSets\": %zu,\n", totalViewSets);
        std::printf("  \"spaces\": %zu,\n", totalSpaces);
        std::printf("  \"spacesGroups\": %zu,\n", totalSpacesGroups);
        std::printf("  \"poseInputs\": %zu,\n", totalPose);
        std::printf("  \"booleanInputs\": %zu,\n", totalBool);
        std::printf("  \"floatInputs\": %zu,\n", totalFloat);
        std::printf("  \"vector2fInputs\": %zu", totalVec2);
        if (bx.valid()) {
            std::printf(",\n  \"headBounds\": {\"x\": [%.4f, %.4f], \"y\": [%.4f, %.4f], \"z\": [%.4f, %.4f]}\n", bx.lo, bx.hi, by.lo,
                        by.hi, bz.lo, bz.hi);
        } else {
            std::printf("\n");
        }
        std::printf("}\n");
        return 0;
    }

    std::printf("poseplay %s\n", POSELAYER_VERSION_STRING);
    std::printf("file            %s\n", path.c_str());
    std::printf("format version  %u\n", Recording::kFormatVersion);
    std::printf("frames          %zu\n", rec.frameCount());
    std::printf("duration        %.1f ms  (%.1f fps)\n", durationMs, fps);
    std::printf("view sets       %zu\n", totalViewSets);
    std::printf("space locates   %zu single, %zu grouped\n", totalSpaces, totalSpacesGroups);
    std::printf("inputs          %zu pose, %zu boolean, %zu float, %zu vector2f\n", totalPose, totalBool, totalFloat, totalVec2);
    if (bx.valid()) {
        std::printf("head position   x [%.4f, %.4f]  y [%.4f, %.4f]  z [%.4f, %.4f]  (%zu frames)\n", bx.lo, bx.hi, by.lo, by.hi, bz.lo,
                    bz.hi, framesWithHead);
    } else {
        std::printf("head position   none recorded\n");
    }
    return 0;
}

int cmdDump(const Recording& rec, bool haveRange, uint32_t begin, uint32_t end, bool json) {
    bool first = true;
    if (json) {
        std::printf("[\n");
    }
    for (const FrameRecord& frame : rec.frames) {
        if (haveRange && (frame.frameIndex < begin || frame.frameIndex > end)) {
            continue;
        }
        XrVector3f head = {0.0f, 0.0f, 0.0f};
        const bool haveHead = headPosition(frame, head);
        if (json) {
            if (!first) {
                std::printf(",\n");
            }
            first = false;
            std::printf("  {\"frame\": %u, \"time\": %lld, \"entries\": %zu", frame.frameIndex,
                        static_cast<long long>(frame.predictedDisplayTime), frame.entryCount());
            if (haveHead) {
                std::printf(", \"head\": [%.4f, %.4f, %.4f]", head.x, head.y, head.z);
            }
            std::printf("}");
        } else {
            std::printf("f%-6u t=%-14lld entries=%-3zu", frame.frameIndex, static_cast<long long>(frame.predictedDisplayTime),
                        frame.entryCount());
            if (haveHead) {
                std::printf("  head=(%.4f, %.4f, %.4f)", head.x, head.y, head.z);
            }
            std::printf("\n");
        }
    }
    if (json) {
        std::printf("\n]\n");
    }
    return 0;
}

int cmdTrim(const Recording& rec, uint32_t begin, uint32_t end, const std::string& out) {
    Recording trimmed;
    uint32_t newIndex = 0;
    for (const FrameRecord& frame : rec.frames) {
        if (frame.frameIndex < begin || frame.frameIndex > end) {
            continue;
        }
        FrameRecord copy = frame;
        copy.frameIndex = newIndex++;
        trimmed.frames.push_back(std::move(copy));
    }
    if (trimmed.frames.empty()) {
        std::fprintf(stderr, "poseplay: no frames in range %u-%u\n", begin, end);
        return 1;
    }
    LoadError err;
    if (!trimmed.save(out, &err)) {
        std::fprintf(stderr, "poseplay: cannot write '%s': %s\n", out.c_str(), err.message.c_str());
        return 1;
    }
    std::printf("wrote %zu frames to %s\n", trimmed.frameCount(), out.c_str());
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    std::vector<std::string> args(argv + 1, argv + argc);
    if (args.empty() || args[0] == "help" || args[0] == "-h" || args[0] == "--help") {
        printUsage();
        return args.empty() ? 1 : 0;
    }
    if (args[0] == "--version" || args[0] == "-v") {
        std::printf("poseplay %s\n", POSELAYER_VERSION_STRING);
        return 0;
    }

    const std::string command = args[0];
    std::string file;
    std::string out;
    bool json = false;
    bool haveRange = false;
    uint32_t begin = 0, end = 0;

    for (size_t i = 1; i < args.size(); ++i) {
        const std::string& a = args[i];
        if (a == "--json") {
            json = true;
        } else if (a == "--frames" && i + 1 < args.size()) {
            if (!parseRange(args[++i], begin, end)) {
                std::fprintf(stderr, "poseplay: bad --frames range '%s'\n", args[i].c_str());
                return 1;
            }
            haveRange = true;
        } else if ((a == "-o" || a == "--out") && i + 1 < args.size()) {
            out = args[++i];
        } else if (!a.empty() && a[0] == '-') {
            std::fprintf(stderr, "poseplay: unknown option '%s'\n", a.c_str());
            return 1;
        } else if (file.empty()) {
            file = a;
        } else {
            std::fprintf(stderr, "poseplay: unexpected argument '%s'\n", a.c_str());
            return 1;
        }
    }

    if (file.empty()) {
        std::fprintf(stderr, "poseplay: %s needs a file\n", command.c_str());
        return 1;
    }

    const std::optional<Recording> rec = loadOrReport(file);
    if (!rec) {
        return 1;
    }

    if (command == "info") {
        return cmdInfo(*rec, file, json);
    }
    if (command == "dump") {
        return cmdDump(*rec, haveRange, begin, end, json);
    }
    if (command == "trim") {
        if (!haveRange) {
            std::fprintf(stderr, "poseplay: trim needs --frames A-B\n");
            return 1;
        }
        if (out.empty()) {
            std::fprintf(stderr, "poseplay: trim needs -o <out>\n");
            return 1;
        }
        return cmdTrim(*rec, begin, end, out);
    }

    std::fprintf(stderr, "poseplay: unknown command '%s'\n", command.c_str());
    printUsage();
    return 1;
}
