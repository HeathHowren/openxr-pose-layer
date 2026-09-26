// Produce the artifacts the README shows: a recording written by the layer in
// record mode through the fake next layer, and a log-mode call trace from the
// same path. This is the fake-next-layer harness the unit tests use, run once
// to capture real output. It is not a shipped tool.
#include "Support.h"

#include "core/Config.h"

#include <cstdio>
#include <cstdlib>
#include <string>

using namespace poselayer;
using namespace poselayer::support;

namespace {

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

} // namespace

int main(int argc, char** argv) {
    const std::string dir = argc > 1 ? argv[1] : ".";
    const uint32_t frames = argc > 2 ? static_cast<uint32_t>(std::atoi(argv[2])) : 120;

    // Record mode: drive the layer through the fake and let it write the file.
    {
        fake().reset();
        fake().script = makeSampleRecording(frames);
        Config config;
        config.mode = Mode::Record;
        config.file = dir + "/sample.oxrr";
        LayerHarness harness(fake(), config);
        if (!harness.create()) {
            std::fprintf(stderr, "sample_gen: record create failed\n");
            return 1;
        }
        for (uint32_t f = 0; f < frames; ++f) {
            harness.driveFrame(sampleOptions());
        }
        harness.destroy();
        std::printf("wrote %s (%u frames)\n", config.file.c_str(), frames);
    }

    // Log mode: a short call trace to a text file.
    {
        fake().reset();
        fake().script = makeSampleRecording(3);
        Config config;
        config.mode = Mode::Log;
        config.logPath = dir + "/sample.log";
        LayerHarness harness(fake(), config);
        if (!harness.create()) {
            std::fprintf(stderr, "sample_gen: log create failed\n");
            return 1;
        }
        for (uint32_t f = 0; f < 3; ++f) {
            harness.driveFrame(sampleOptions());
        }
        harness.destroy();
        std::printf("wrote %s\n", config.logPath.c_str());
    }

    return 0;
}
