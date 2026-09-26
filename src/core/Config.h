// What the layer should do, and where. Read from environment variables, or a
// config file named by one of them, so it can be set per app without a rebuild.
//
// Environment variables (a config file uses the same names without the prefix):
//   XR_POSE_LAYER_MODE        off | log | record | replay   (default off)
//   XR_POSE_LAYER_FILE        recording path (record and replay)
//   XR_POSE_LAYER_LOG         call-trace text path (log mode; default stderr)
//   XR_POSE_LAYER_TIME_SCALE  replay speed, e.g. 0.5 or 2.0   (default 1.0)
//   XR_POSE_LAYER_LOOP        0 | 1                            (default 0)
//   XR_POSE_LAYER_OFFSET      "x y z yawDegrees" replay offset (default none)
//   XR_POSE_LAYER_CONFIG      path to a key = value config file
#pragma once

#include "core/Transform.h"

#include <map>
#include <string>

namespace poselayer {

enum class Mode {
    Off,
    Log,
    Record,
    Replay,
};

const char* modeName(Mode mode);

struct Config {
    Mode mode = Mode::Off;
    std::string file;
    std::string logPath;
    double timeScale = 1.0;
    bool loop = false;
    XrPosef offset = kPoseIdentity;

    // False when a value could not be parsed; error says which. An unparsable
    // config disables the layer rather than guessing.
    bool valid = true;
    std::string error;

    // Build from a flat key -> value map (keys without the XR_POSE_LAYER_
    // prefix, lower case). This is the whole parser; the sources below just feed
    // it. Pure, so the tests drive it directly.
    static Config fromKeyValues(const std::map<std::string, std::string>& values);

    // Read a "key = value" file (lines, # comments) into a key -> value map.
    static std::map<std::string, std::string> parseConfigFile(const std::string& text);

    // Collect the environment variables and any file named by XR_POSE_LAYER_CONFIG,
    // then run fromKeyValues. Environment variables win over the file.
    static Config fromEnvironment();
};

} // namespace poselayer
