#include "generated/BridgeCodec.h"

#include <catch2/catch_test_macros.hpp>
#include <limits>

using namespace ap::bridge;
using ap::desktop::bridge::parseIntent;
using ap::desktop::bridge::toVar;

namespace
{
juce::var parseJson (const char* json)
{
    return juce::JSON::parse (juce::String (json));
}
} // namespace

TEST_CASE ("parseIntent accepts well-formed intents", "[bridge]")
{
    const auto ready = parseIntent (parseJson (R"({"type":"app.ready","payload":{}})"));
    REQUIRE (ready.has_value());
    CHECK (std::holds_alternative<AppReady> (*ready));

    const auto enable = parseIntent (parseJson (R"({"type":"tone.setEnabled","payload":{"enabled":true}})"));
    REQUIRE (enable.has_value());
    CHECK (std::get<ToneSetEnabled> (*enable).enabled);

    const auto level = parseIntent (parseJson (R"({"type":"tone.setLevel","payload":{"db":-24}})"));
    REQUIRE (level.has_value());
    CHECK (std::get<ToneSetLevel> (*level).db == -24.0);

    const auto boundary = parseIntent (parseJson (R"({"type":"tone.setLevel","payload":{"db":-6.0}})"));
    CHECK (boundary.has_value());
}

TEST_CASE ("parseIntent rejects malformed or hostile input", "[bridge][security]")
{
    const char* rejected[] = {
        R"(null)",
        R"([])",
        R"("app.ready")",
        R"({})",
        R"({"type":"app.ready"})",
        R"({"payload":{}})",
        R"({"type":"app.ready","payload":{},"extra":1})",
        R"({"type":"app.ready","payload":{"unexpected":1}})",
        R"({"type":"app.ready","payload":[]})",
        R"({"type":42,"payload":{}})",
        R"({"type":"engine.status","payload":{}})", // events are not intents
        R"({"type":"system.exec","payload":{"cmd":"rm -rf /"}})",
        R"({"type":"tone.setEnabled","payload":{}})",
        R"({"type":"tone.setEnabled","payload":{"enabled":1}})",
        R"({"type":"tone.setEnabled","payload":{"enabled":"true"}})",
        R"({"type":"tone.setLevel","payload":{"db":0}})",
        R"({"type":"tone.setLevel","payload":{"db":-61}})",
        R"({"type":"tone.setLevel","payload":{"db":"-12"}})",
        R"({"type":"tone.setLevel","payload":{"db":-12,"db2":0}})",
    };

    for (const auto* json : rejected)
    {
        INFO (json);
        CHECK_FALSE (parseIntent (parseJson (json)).has_value());
    }
}

TEST_CASE ("parseIntent rejects non-finite numbers", "[bridge][security]")
{
    for (const double value :
         {std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity()})
    {
        auto* payload = new juce::DynamicObject();
        payload->setProperty ("db", value);
        auto* message = new juce::DynamicObject();
        message->setProperty ("type", "tone.setLevel");
        message->setProperty ("payload", juce::var (payload));

        CHECK_FALSE (parseIntent (juce::var (message)).has_value());
    }
}

TEST_CASE ("toVar serialises events into the documented envelope", "[bridge]")
{
    EngineStatus status;
    status.deviceName = "Speakers \xC3\xA9"; // UTF-8 survives the round trip
    status.sampleRate = 48000.0;
    status.bufferSize = 256;
    status.toneEnabled = true;
    status.toneLevelDb = -18.0;

    const auto json = juce::JSON::toString (toVar (Event{status}), true);
    const auto parsed = juce::JSON::parse (json);

    CHECK (parsed["type"].toString() == "engine.status");
    CHECK (parsed["payload"]["deviceName"].toString() == juce::String::fromUTF8 ("Speakers \xC3\xA9"));
    CHECK (static_cast<int> (parsed["payload"]["bufferSize"]) == 256);
    CHECK (static_cast<bool> (parsed["payload"]["toneEnabled"]));
}
