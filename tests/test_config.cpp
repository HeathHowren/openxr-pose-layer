#include "core/Config.h"

#include <catch2/catch_test_macros.hpp>

using namespace poselayer;

TEST_CASE("defaults to off with no keys", "[config]") {
    const Config c = Config::fromKeyValues({});
    CHECK(c.valid);
    CHECK(c.mode == Mode::Off);
}

TEST_CASE("record and replay need a file", "[config]") {
    CHECK_FALSE(Config::fromKeyValues({{"mode", "record"}}).valid);
    CHECK_FALSE(Config::fromKeyValues({{"mode", "replay"}}).valid);
    CHECK(Config::fromKeyValues({{"mode", "record"}, {"file", "run.oxrr"}}).valid);
}

TEST_CASE("log mode does not need a file", "[config]") {
    const Config c = Config::fromKeyValues({{"mode", "log"}});
    CHECK(c.valid);
    CHECK(c.mode == Mode::Log);
}

TEST_CASE("an unknown mode is rejected", "[config]") {
    const Config c = Config::fromKeyValues({{"mode", "wat"}});
    CHECK_FALSE(c.valid);
    CHECK(c.error.find("mode") != std::string::npos);
}

TEST_CASE("time scale must be positive", "[config]") {
    CHECK_FALSE(Config::fromKeyValues({{"mode", "replay"}, {"file", "r"}, {"time_scale", "0"}}).valid);
    CHECK_FALSE(Config::fromKeyValues({{"mode", "replay"}, {"file", "r"}, {"time_scale", "-1"}}).valid);
    const Config c = Config::fromKeyValues({{"mode", "replay"}, {"file", "r"}, {"time_scale", "1.5"}});
    CHECK(c.valid);
    CHECK(c.timeScale == 1.5);
}

TEST_CASE("loop parses common truthy and falsy spellings", "[config]") {
    CHECK(Config::fromKeyValues({{"mode", "replay"}, {"file", "r"}, {"loop", "1"}}).loop);
    CHECK(Config::fromKeyValues({{"mode", "replay"}, {"file", "r"}, {"loop", "true"}}).loop);
    CHECK_FALSE(Config::fromKeyValues({{"mode", "replay"}, {"file", "r"}, {"loop", "off"}}).loop);
    CHECK_FALSE(Config::fromKeyValues({{"mode", "replay"}, {"file", "r"}, {"loop", "maybe"}}).valid);
}

TEST_CASE("offset parses translation and optional yaw", "[config]") {
    const Config c = Config::fromKeyValues({{"mode", "replay"}, {"file", "r"}, {"offset", "1 2 3 90"}});
    REQUIRE(c.valid);
    CHECK(c.offset.position.x == 1.0f);
    CHECK(c.offset.position.y == 2.0f);
    CHECK(c.offset.position.z == 3.0f);
    // 90 degree yaw about +Y: quaternion y = sin(45) ~= 0.7071.
    CHECK(nearlyEqual(c.offset.orientation.y, 0.70710677f, 1e-4f));

    const Config noYaw = Config::fromKeyValues({{"mode", "replay"}, {"file", "r"}, {"offset", "1 2 3"}});
    CHECK(noYaw.valid);
    CHECK(nearlyEqual(noYaw.offset.orientation.w, 1.0f));

    CHECK_FALSE(Config::fromKeyValues({{"mode", "replay"}, {"file", "r"}, {"offset", "junk"}}).valid);
}

TEST_CASE("a config file parses into key values, comments ignored", "[config]") {
    const std::string text = "# a comment\nmode = record\nfile = C:/tmp/run.oxrr\n\ntime_scale = 2\n";
    const auto values = Config::parseConfigFile(text);
    const Config c = Config::fromKeyValues(values);
    CHECK(c.mode == Mode::Record);
    CHECK(c.file == "C:/tmp/run.oxrr");
    CHECK(c.timeScale == 2.0);
}

TEST_CASE("modeName is stable", "[config]") {
    CHECK(std::string(modeName(Mode::Off)) == "off");
    CHECK(std::string(modeName(Mode::Log)) == "log");
    CHECK(std::string(modeName(Mode::Record)) == "record");
    CHECK(std::string(modeName(Mode::Replay)) == "replay");
}
