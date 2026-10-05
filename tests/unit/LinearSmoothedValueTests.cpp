#include "ap/dsp/LinearSmoothedValue.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

using ap::dsp::LinearSmoothedValue;
using Catch::Matchers::WithinAbs;

TEST_CASE ("LinearSmoothedValue reaches the target exactly after the ramp length", "[dsp][smoothing]")
{
    LinearSmoothedValue value;
    value.reset (1000.0, 0.01); // 10-sample ramp
    value.setCurrentAndTarget (0.0f);
    value.setTarget (1.0f);

    float previous = 0.0f;
    for (int i = 0; i < 9; ++i)
    {
        const float current = value.next();
        CHECK (current > previous);
        CHECK (current < 1.0f);
        previous = current;
    }

    CHECK (value.next() == 1.0f);
    CHECK_FALSE (value.isSmoothing());
    CHECK (value.next() == 1.0f);
}

TEST_CASE ("LinearSmoothedValue retargeting mid-ramp continues from the current value", "[dsp][smoothing]")
{
    LinearSmoothedValue value;
    value.reset (1000.0, 0.01);
    value.setCurrentAndTarget (0.0f);
    value.setTarget (1.0f);

    for (int i = 0; i < 5; ++i)
        (void)value.next();

    const float midpoint = value.getCurrent();
    value.setTarget (0.0f);

    const float afterRetarget = value.next();
    CHECK (afterRetarget < midpoint);
    CHECK_THAT (static_cast<double> (midpoint - afterRetarget),
                WithinAbs (static_cast<double> (midpoint / 10.0f), 1.0e-6));
}

TEST_CASE ("LinearSmoothedValue with zero ramp jumps immediately", "[dsp][smoothing]")
{
    LinearSmoothedValue value;
    value.reset (48000.0, 0.0);
    value.setTarget (0.5f);
    CHECK_FALSE (value.isSmoothing());
    CHECK (value.next() == 0.5f);
}
