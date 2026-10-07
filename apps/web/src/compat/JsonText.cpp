#include <cmath>
#include <juce_core/juce_core.h>
#include <nlohmann/json.hpp>

namespace juce
{
namespace
{
using Json = nlohmann::json;

var fromJson (const Json& json, int depth)
{
    if (depth > JSON::maxDepth)
        return {};
    if (json.is_boolean())
        return var (json.get<bool>());
    if (json.is_number_integer() || json.is_number_unsigned())
    {
        const auto value = json.is_number_unsigned() ? static_cast<std::int64_t> (json.get<std::uint64_t>())
                                                     : json.get<std::int64_t>();
        if (value >= INT32_MIN && value <= INT32_MAX)
            return var (static_cast<int> (value));
        return var (value);
    }
    if (json.is_number_float())
        return var (json.get<double>());
    if (json.is_string())
        return var (String (json.get<std::string>()));
    if (json.is_array())
    {
        Array<var> array;
        for (const auto& item : json)
            array.add (fromJson (item, depth + 1));
        return var (array);
    }
    if (json.is_object())
    {
        auto* object = new DynamicObject();
        for (const auto& [key, value] : json.items())
            object->setProperty (key.c_str(), fromJson (value, depth + 1));
        return var (object);
    }
    return {};
}

Json toJson (const var& value)
{
    if (value.isBool())
        return static_cast<bool> (value);
    if (value.isInt())
        return static_cast<int> (value);
    if (value.isInt64())
        return value.toInt64();
    if (value.isDouble())
    {
        const auto number = static_cast<double> (value);
        return std::isfinite (number) ? Json (number) : Json (0.0); // JSON has no NaN or infinity
    }
    if (value.isString())
        return value.toString().toStdString();
    if (const auto* array = value.getArray())
    {
        auto out = Json::array();
        for (const auto& item : *array)
            out.push_back (toJson (item));
        return out;
    }
    if (const auto* object = value.getDynamicObject())
    {
        auto out = Json::object();
        // Names in a fixed order (the object keeps them sorted) so output is reproducible.
        for (const auto& [key, item] : object->getProperties())
            out[key] = toJson (item);
        return out;
    }
    return nullptr;
}
} // namespace

var JSON::parse (std::string_view text)
{
    const auto json = Json::parse (text.begin(), text.end(), nullptr, false);
    if (json.is_discarded())
        return {};
    return fromJson (json, 0);
}

std::string JSON::toString (const var& value)
{
    return toJson (value).dump();
}

} // namespace juce
