#include "ap/params/Parameters.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cmath>
#include <limits>
#include <set>
#include <string>

using namespace ap::params;
using Catch::Matchers::WithinRel;

TEST_CASE ("Every parameter descriptor is well-formed", "[params]")
{
    std::set<std::string_view> ids;
    for (const auto& d : descriptors)
    {
        INFO (std::string (d.id));
        CHECK (ids.insert (d.id).second);
        CHECK (d.min < d.max);
        CHECK (isInRange (d, d.defaultValue));
        CHECK (d.step > 0.0f);
        if (d.curve == Curve::logarithmic)
            CHECK (d.min > 0.0f);
        REQUIRE (findParamId (d.id).has_value());
        CHECK (&descriptor (*findParamId (d.id)) == &d);
    }
}

TEST_CASE ("findParamId rejects unknown IDs", "[params]")
{
    CHECK_FALSE (findParamId ("").has_value());
    CHECK_FALSE (findParamId ("tone").has_value());
    CHECK_FALSE (findParamId ("tone.level ").has_value());
    CHECK_FALSE (findParamId ("TONE.LEVEL").has_value());
}

TEST_CASE ("Linear normalisation maps the range onto [0, 1] and back", "[params]")
{
    const ParameterDescriptor d {"t.linear", "Linear", "dB", -60.0f, 0.0f, -12.0f, 0.5f, Curve::linear};

    CHECK (toNormalized (d, -60.0f) == 0.0f);
    CHECK (toNormalized (d, 0.0f) == 1.0f);
    CHECK_THAT (toNormalized (d, -30.0f), WithinRel (0.5f, 1.0e-6f));
    CHECK (toNormalized (d, 100.0f) == 1.0f);
    CHECK (fromNormalized (d, 2.0f) == 0.0f);

    for (float n = 0.0f; n <= 1.0f; n += 0.01f)
        CHECK_THAT (toNormalized (d, fromNormalized (d, n)),
                    WithinRel (n, 1.0e-4f) || Catch::Matchers::WithinAbs (static_cast<double> (n), 1.0e-6));
}

TEST_CASE ("Logarithmic normalisation gives equal travel per octave", "[params]")
{
    const ParameterDescriptor d {"t.freq", "Frequency", "Hz", 20.0f,
                                 20480.0f, 1000.0f,     1.0f, Curve::logarithmic};

    CHECK_THAT (fromNormalized (d, 0.0f), WithinRel (20.0f, 1.0e-6f));
    CHECK_THAT (fromNormalized (d, 1.0f), WithinRel (20480.0f, 1.0e-6f));
    CHECK_THAT (fromNormalized (d, 0.5f), WithinRel (640.0f, 1.0e-4f)); // geometric mean: 5 of 10 octaves
    CHECK_THAT (toNormalized (d, 40.0f), WithinRel (0.1f, 1.0e-4f));    // one octave = 1/10
}

TEST_CASE ("ParameterStore starts at defaults, clamps and rejects non-finite values", "[params]")
{
    ParameterStore store;
    const auto id = ParamId::toneLevel;
    const auto& d = descriptor (id);

    CHECK (store.get (id) == d.defaultValue);

    CHECK (store.set (id, d.max + 100.0f));
    CHECK (store.get (id) == d.max);

    CHECK_FALSE (store.set (id, std::numeric_limits<float>::quiet_NaN()));
    CHECK_FALSE (store.set (id, std::numeric_limits<float>::infinity()));
    CHECK (store.get (id) == d.max);

    store.resetToDefault (id);
    CHECK (store.get (id) == d.defaultValue);
}
