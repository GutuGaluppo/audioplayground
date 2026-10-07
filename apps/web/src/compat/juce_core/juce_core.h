#pragma once

// A tiny stand-in for the part of JUCE's juce_core that the generated bridge codec
// (apps/desktop/src/generated/BridgeCodec.cpp) uses, so the browser build compiles that same file
// instead of a second codec that could drift from it. Only what the codec needs: a JSON value
// (juce::var), objects with named properties, arrays, strings, identifiers, and JSON text I/O.
// It is not JUCE and is never used by the desktop app.

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace juce
{

class String
{
public:
    String() = default;
    String (const char* text)
        : text (text != nullptr ? text : "")
    {
    }
    String (const std::string& text)
        : text (text)
    {
    }
    [[nodiscard]] std::string toStdString() const { return text; }
    [[nodiscard]] static String fromUTF8 (const char* utf8) { return String (utf8); }

private:
    std::string text;
};

class Identifier
{
public:
    Identifier (const char* name)
        : name (name)
    {
    }
    [[nodiscard]] const std::string& toStdString() const noexcept { return name; }

private:
    std::string name;
};

class DynamicObject;
template <typename Element> class Array;

class var
{
public:
    var() = default;
    var (bool value)
        : kind (Kind::boolean)
        , integer (value ? 1 : 0)
    {
    }
    var (int value)
        : kind (Kind::integer)
        , integer (value)
    {
    }
    var (std::int64_t value)
        : kind (Kind::integer64)
        , integer (value)
    {
    }
    var (double value)
        : kind (Kind::floating)
        , number (value)
    {
    }
    var (const String& value)
        : kind (Kind::text)
        , text (std::make_shared<std::string> (value.toStdString()))
    {
    }
    var (const char* value)
        : var (String (value))
    {
    }
    var (DynamicObject* object); // takes ownership
    var (const Array<var>& array);

    [[nodiscard]] bool isVoid() const noexcept { return kind == Kind::none; }
    [[nodiscard]] bool isBool() const noexcept { return kind == Kind::boolean; }
    [[nodiscard]] bool isInt() const noexcept { return kind == Kind::integer; }
    [[nodiscard]] bool isInt64() const noexcept { return kind == Kind::integer64; }
    [[nodiscard]] bool isDouble() const noexcept { return kind == Kind::floating; }
    [[nodiscard]] bool isString() const noexcept { return kind == Kind::text; }

    explicit operator bool() const noexcept { return kind == Kind::floating ? number != 0.0 : integer != 0; }
    explicit operator int() const noexcept
    {
        return kind == Kind::floating ? static_cast<int> (number) : static_cast<int> (integer);
    }
    explicit operator double() const noexcept
    {
        return kind == Kind::floating ? number : static_cast<double> (integer);
    }

    [[nodiscard]] std::int64_t toInt64() const noexcept { return integer; }
    [[nodiscard]] String toString() const { return text ? String (*text) : String(); }
    [[nodiscard]] const Array<var>* getArray() const noexcept
    {
        return kind == Kind::list ? list.get() : nullptr;
    }
    [[nodiscard]] DynamicObject* getDynamicObject() const noexcept
    {
        return kind == Kind::object ? object.get() : nullptr;
    }

private:
    friend class JSON;
    enum class Kind
    {
        none,
        boolean,
        integer,
        integer64,
        floating,
        text,
        object,
        list
    };

    Kind kind = Kind::none;
    std::int64_t integer = 0;
    double number = 0.0;
    std::shared_ptr<std::string> text;
    std::shared_ptr<DynamicObject> object;
    std::shared_ptr<Array<var>> list;
};

template <typename Element> class Array
{
public:
    void ensureStorageAllocated (int count) { items.reserve (static_cast<std::size_t> (count)); }
    void add (const Element& item) { items.push_back (item); }
    [[nodiscard]] int size() const noexcept { return static_cast<int> (items.size()); }
    [[nodiscard]] auto begin() const noexcept { return items.begin(); }
    [[nodiscard]] auto end() const noexcept { return items.end(); }
    [[nodiscard]] const Element& operator[] (int index) const
    {
        return items[static_cast<std::size_t> (index)];
    }

private:
    std::vector<Element> items;
};

class NamedValueSet
{
public:
    [[nodiscard]] int size() const noexcept { return static_cast<int> (values.size()); }
    [[nodiscard]] auto begin() const noexcept { return values.begin(); }
    [[nodiscard]] auto end() const noexcept { return values.end(); }
    [[nodiscard]] bool contains (const Identifier& name) const noexcept
    {
        return values.count (name.toStdString()) > 0;
    }

private:
    friend class DynamicObject;
    friend class JSON;
    std::map<std::string, var> values;
};

class DynamicObject
{
public:
    [[nodiscard]] const NamedValueSet& getProperties() const noexcept { return properties; }
    // A reference into the object (or to a void value), so callers may keep pointers into it.
    [[nodiscard]] const var& getProperty (const char* name) const noexcept
    {
        static const var none;
        const auto it = properties.values.find (name);
        return it == properties.values.end() ? none : it->second;
    }
    void setProperty (const char* name, const var& value) { properties.values[name] = value; }

private:
    friend class JSON;
    NamedValueSet properties;
};

inline var::var (DynamicObject* value)
    : kind (Kind::object)
    , object (value)
{
}

inline var::var (const Array<var>& value)
    : kind (Kind::list)
    , list (std::make_shared<Array<var>> (value))
{
}

// JSON text in and out. Parsing is strict (no comments, no trailing data) and bounded: nesting
// deeper than `maxDepth` is refused, since the text comes from a page.
class JSON
{
public:
    static constexpr int maxDepth = 32;
    // A void var when the text is not valid JSON.
    [[nodiscard]] static var parse (std::string_view text);
    [[nodiscard]] static std::string toString (const var& value);
};

} // namespace juce
