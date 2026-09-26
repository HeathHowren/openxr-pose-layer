#include "core/Config.h"

#include <cstdlib>
#include <fstream>
#include <sstream>

namespace poselayer {

const char* modeName(Mode mode) {
    switch (mode) {
    case Mode::Off:
        return "off";
    case Mode::Log:
        return "log";
    case Mode::Record:
        return "record";
    case Mode::Replay:
        return "replay";
    }
    return "off";
}

namespace {

std::string trim(const std::string& s) {
    const char* ws = " \t\r\n";
    const size_t begin = s.find_first_not_of(ws);
    if (begin == std::string::npos) {
        return {};
    }
    const size_t end = s.find_last_not_of(ws);
    return s.substr(begin, end - begin + 1);
}

std::string toLower(std::string s) {
    for (char& c : s) {
        if (c >= 'A' && c <= 'Z') {
            c = static_cast<char>(c - 'A' + 'a');
        }
    }
    return s;
}

// Read an environment variable without tripping MSVC's deprecation of getenv.
bool readEnv(const char* name, std::string& out) {
    size_t len = 0;
    if (getenv_s(&len, nullptr, 0, name) != 0 || len == 0) {
        return false;
    }
    std::string value(len, '\0');
    if (getenv_s(&len, value.data(), value.size(), name) != 0) {
        return false;
    }
    if (!value.empty() && value.back() == '\0') {
        value.pop_back();
    }
    out = value;
    return true;
}

bool parseBool(const std::string& raw, bool& out) {
    const std::string v = toLower(trim(raw));
    if (v == "1" || v == "true" || v == "on" || v == "yes") {
        out = true;
        return true;
    }
    if (v == "0" || v == "false" || v == "off" || v == "no") {
        out = false;
        return true;
    }
    return false;
}

bool parseDouble(const std::string& raw, double& out) {
    try {
        size_t consumed = 0;
        const double v = std::stod(trim(raw), &consumed);
        if (consumed == 0) {
            return false;
        }
        out = v;
        return true;
    } catch (...) {
        return false;
    }
}

// "x y z yawDegrees": a translation and a yaw about +Y, the transform a replay
// applies to the recorded path.
bool parseOffset(const std::string& raw, XrPosef& out) {
    std::istringstream in(raw);
    float x = 0.0f, y = 0.0f, z = 0.0f, yaw = 0.0f;
    if (!(in >> x >> y >> z)) {
        return false;
    }
    in >> yaw; // optional; defaults to 0
    out.position = {x, y, z};
    out.orientation = quatFromYawDegrees(yaw);
    return true;
}

} // namespace

std::map<std::string, std::string> Config::parseConfigFile(const std::string& text) {
    std::map<std::string, std::string> values;
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        const std::string trimmed = trim(line);
        if (trimmed.empty() || trimmed[0] == '#') {
            continue;
        }
        const size_t eq = trimmed.find('=');
        if (eq == std::string::npos) {
            continue;
        }
        const std::string key = toLower(trim(trimmed.substr(0, eq)));
        const std::string value = trim(trimmed.substr(eq + 1));
        if (!key.empty()) {
            values[key] = value;
        }
    }
    return values;
}

Config Config::fromKeyValues(const std::map<std::string, std::string>& values) {
    Config config;

    auto find = [&](const char* key) -> const std::string* {
        const auto it = values.find(key);
        return it == values.end() ? nullptr : &it->second;
    };

    if (const std::string* mode = find("mode")) {
        const std::string m = toLower(trim(*mode));
        if (m == "off" || m.empty()) {
            config.mode = Mode::Off;
        } else if (m == "log") {
            config.mode = Mode::Log;
        } else if (m == "record") {
            config.mode = Mode::Record;
        } else if (m == "replay") {
            config.mode = Mode::Replay;
        } else {
            config.valid = false;
            config.error = "unknown mode '" + m + "'";
            return config;
        }
    }

    if (const std::string* file = find("file")) {
        config.file = trim(*file);
    }
    if (const std::string* log = find("log")) {
        config.logPath = trim(*log);
    }
    if (const std::string* scale = find("time_scale")) {
        if (!parseDouble(*scale, config.timeScale) || config.timeScale <= 0.0) {
            config.valid = false;
            config.error = "time_scale must be a positive number";
            return config;
        }
    }
    if (const std::string* loop = find("loop")) {
        if (!parseBool(*loop, config.loop)) {
            config.valid = false;
            config.error = "loop must be 0 or 1";
            return config;
        }
    }
    if (const std::string* offset = find("offset")) {
        if (!parseOffset(*offset, config.offset)) {
            config.valid = false;
            config.error = "offset must be 'x y z' or 'x y z yawDegrees'";
            return config;
        }
    }

    if ((config.mode == Mode::Record || config.mode == Mode::Replay) && config.file.empty()) {
        config.valid = false;
        config.error = std::string(modeName(config.mode)) + " mode needs a file (XR_POSE_LAYER_FILE)";
    }
    return config;
}

Config Config::fromEnvironment() {
    std::map<std::string, std::string> values;

    std::string configPath;
    if (readEnv("XR_POSE_LAYER_CONFIG", configPath) && !configPath.empty()) {
        std::ifstream in(configPath, std::ios::binary);
        if (in) {
            std::ostringstream buffer;
            buffer << in.rdbuf();
            values = parseConfigFile(buffer.str());
        }
    }

    struct Var {
        const char* env;
        const char* key;
    };
    const Var vars[] = {
        {"XR_POSE_LAYER_MODE", "mode"},         {"XR_POSE_LAYER_FILE", "file"},   {"XR_POSE_LAYER_LOG", "log"},
        {"XR_POSE_LAYER_TIME_SCALE", "time_scale"}, {"XR_POSE_LAYER_LOOP", "loop"}, {"XR_POSE_LAYER_OFFSET", "offset"},
    };
    for (const Var& var : vars) {
        std::string value;
        if (readEnv(var.env, value)) {
            values[var.key] = value; // environment overrides the file
        }
    }

    return fromKeyValues(values);
}

} // namespace poselayer
