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

    const auto param = parseIntent (
        parseJson (R"({"type":"param.set","payload":{"id":"tone.level","value":-24,"gesture":3}})"));
    REQUIRE (param.has_value());
    CHECK (std::get<ParamSet> (*param).id == "tone.level");
    CHECK (std::get<ParamSet> (*param).value == -24.0);
    CHECK (std::get<ParamSet> (*param).gesture == 3);
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
        R"({"type":"param.set","payload":{"id":"tone.level"}})",
        R"({"type":"param.set","payload":{"id":"tone.level","value":-12}})",
        R"({"type":"param.set","payload":{"id":"tone.level","value":-12,"gesture":-1}})",
        R"({"type":"param.set","payload":{"id":"tone.level","value":-12,"gesture":1.5}})",
        R"({"type":"param.set","payload":{"id":"tone.level","value":"-12"}})",
        R"({"type":"param.set","payload":{"id":7,"value":-12}})",
        R"({"type":"param.set","payload":{"id":"tone.level","value":1e9}})",
        R"({"type":"param.set","payload":{"id":"tone.level","value":-12,"extra":0}})",
    };

    for (const auto* json : rejected)
    {
        INFO (json);
        CHECK_FALSE (parseIntent (parseJson (json)).has_value());
    }

    const auto longId = juce::String::repeatedString ("a", 65);
    CHECK_FALSE (parseIntent (parseJson (("{\"type\":\"param.set\",\"payload\":{\"id\":\"" + longId
                                          + "\",\"value\":0,\"gesture\":0}}")
                                             .toRawUTF8()))
                     .has_value());
}

TEST_CASE ("parseIntent rejects non-finite numbers", "[bridge][security]")
{
    for (const double value :
         {std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity()})
    {
        auto* payload = new juce::DynamicObject();
        payload->setProperty ("id", "tone.level");
        payload->setProperty ("value", value);
        auto* message = new juce::DynamicObject();
        message->setProperty ("type", "param.set");
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

    const auto json = juce::JSON::toString (toVar (Event {status}), true);
    const auto parsed = juce::JSON::parse (json);

    CHECK (parsed["type"].toString() == "engine.status");
    CHECK (parsed["payload"]["deviceName"].toString() == juce::String::fromUTF8 ("Speakers \xC3\xA9"));
    CHECK (static_cast<int> (parsed["payload"]["bufferSize"]) == 256);
    CHECK (static_cast<bool> (parsed["payload"]["toneEnabled"]));
}
