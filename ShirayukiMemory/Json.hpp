#ifndef SHIRAYUKI_JSON_HPP
#define SHIRAYUKI_JSON_HPP

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace Shirayuki {

/// Minimal JSON reader/writer.
///
/// Exists so `ShirayukiMemory/` stays pure C++, as its layering contract says.
/// Session persistence previously wrote JSON with a hand-rolled serialiser and
/// read it back with NSJSONSerialization — two different technologies for one
/// format, in a directory documented as containing no ObjC. The two halves were
/// not even compatible: the writer left `\r`, `\b`, `\f` and control characters
/// unescaped, producing files the reader rejected.
namespace Json {

class Value;
using Object = std::map<std::string, Value>;
using Array = std::vector<Value>;

enum class Type { Null, Bool, Number, String, Array, Object };

class Value {
  public:
    Value() = default;
    Value(bool b) : m_type(Type::Bool), m_bool(b) {
    }
    Value(double n) : m_type(Type::Number), m_number(n) {
    }
    Value(int n) : m_type(Type::Number), m_number(n) {
    }
    Value(long long n) : m_type(Type::Number), m_number(static_cast<double>(n)) {
    }
    Value(unsigned long long n) : m_type(Type::Number), m_number(static_cast<double>(n)) {
    }
    Value(const char *s) : m_type(Type::String), m_string(s ? s : "") {
    }
    Value(std::string s) : m_type(Type::String), m_string(std::move(s)) {
    }
    Value(Array a) : m_type(Type::Array), m_array(std::move(a)) {
    }
    Value(Object o) : m_type(Type::Object), m_object(std::move(o)) {
    }

    Type type() const {
        return m_type;
    }
    bool isNull() const {
        return m_type == Type::Null;
    }

    /// Accessors return the supplied fallback on a type mismatch, so a truncated
    /// or hand-edited session file degrades instead of throwing.
    bool asBool(bool fallback = false) const;
    double asNumber(double fallback = 0) const;
    /// Numbers above 2^53 cannot round-trip through a double; addresses are
    /// therefore written as hex strings and read back with this.
    uint64_t asUInt64(uint64_t fallback = 0) const;
    std::string asString(const std::string &fallback = "") const;
    const Array &asArray() const;
    const Object &asObject() const;

    /// Object member lookup. Returns a null Value when absent.
    const Value &operator[](const std::string &key) const;

    std::string serialize(bool pretty = false, int indent = 0) const;

  private:
    Type m_type = Type::Null;
    bool m_bool = false;
    double m_number = 0;
    std::string m_string;
    Array m_array;
    Object m_object;
};

/// Parse `text`. Returns false and sets `outError` on malformed input; never
/// throws, since the input is a file the user may have edited by hand.
bool parse(const std::string &text, Value &outValue, std::string *outError = nullptr);

/// Escape a string as a JSON string body (without the surrounding quotes).
std::string escape(const std::string &s);

} // namespace Json
} // namespace Shirayuki

#endif // SHIRAYUKI_JSON_HPP
