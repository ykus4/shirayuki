#include "Json.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sstream>

namespace Shirayuki {
namespace Json {

namespace {

const Array kEmptyArray;
const Object kEmptyObject;
const Value kNullValue;

// --- Writing ---------------------------------------------------------------

void writeIndent(std::ostringstream &out, int depth) {
    for (int i = 0; i < depth; i++)
        out << "  ";
}

/// Serialise a number without the exponent notation or lost precision that
/// operator<< would introduce, and without a trailing ".0" for integers.
std::string formatNumber(double n) {
    if (!std::isfinite(n))
        return "0"; // JSON has no NaN or Infinity
    if (n == static_cast<double>(static_cast<long long>(n))) {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%lld", static_cast<long long>(n));
        return buf;
    }
    char buf[40];
    std::snprintf(buf, sizeof(buf), "%.17g", n);
    return buf;
}

// --- Parsing ---------------------------------------------------------------

class Parser {
  public:
    Parser(const std::string &text) : m_text(text) {
    }

    bool parse(Value &out, std::string &error) {
        skipWhitespace();
        if (!parseValue(out)) {
            error = m_error.empty() ? "malformed JSON" : m_error;
            return false;
        }
        skipWhitespace();
        if (m_pos != m_text.size()) {
            error = "trailing characters after JSON value";
            return false;
        }
        return true;
    }

  private:
    const std::string &m_text;
    size_t m_pos = 0;
    std::string m_error;
    // Bounds recursion so a pathological file cannot exhaust the stack.
    int m_depth = 0;
    static const int kMaxDepth = 64;

    bool fail(const char *why) {
        if (m_error.empty()) {
            std::ostringstream os;
            os << why << " at offset " << m_pos;
            m_error = os.str();
        }
        return false;
    }

    bool atEnd() const {
        return m_pos >= m_text.size();
    }
    char peek() const {
        return m_pos < m_text.size() ? m_text[m_pos] : '\0';
    }

    void skipWhitespace() {
        while (m_pos < m_text.size()) {
            const char c = m_text[m_pos];
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r')
                m_pos++;
            else
                break;
        }
    }

    bool literal(const char *word) {
        const size_t len = std::strlen(word);
        if (m_text.compare(m_pos, len, word) != 0)
            return false;
        m_pos += len;
        return true;
    }

    bool parseValue(Value &out) {
        if (m_depth >= kMaxDepth)
            return fail("nesting too deep");
        if (atEnd())
            return fail("unexpected end of input");

        switch (peek()) {
            case 'n':
                if (!literal("null"))
                    return fail("expected null");
                out = Value();
                return true;
            case 't':
                if (!literal("true"))
                    return fail("expected true");
                out = Value(true);
                return true;
            case 'f':
                if (!literal("false"))
                    return fail("expected false");
                out = Value(false);
                return true;
            case '"': {
                std::string s;
                if (!parseString(s))
                    return false;
                out = Value(std::move(s));
                return true;
            }
            case '[':
                return parseArray(out);
            case '{':
                return parseObject(out);
            default:
                return parseNumber(out);
        }
    }

    void appendUtf8(std::string &out, uint32_t cp) {
        if (cp <= 0x7F) {
            out += static_cast<char>(cp);
        } else if (cp <= 0x7FF) {
            out += static_cast<char>(0xC0 | (cp >> 6));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        } else if (cp <= 0xFFFF) {
            out += static_cast<char>(0xE0 | (cp >> 12));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        } else {
            out += static_cast<char>(0xF0 | (cp >> 18));
            out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        }
    }

    bool parseHex4(uint32_t &out) {
        if (m_pos + 4 > m_text.size())
            return fail("truncated \\u escape");
        out = 0;
        for (int i = 0; i < 4; i++) {
            const char c = m_text[m_pos++];
            out <<= 4;
            if (c >= '0' && c <= '9')
                out |= static_cast<uint32_t>(c - '0');
            else if (c >= 'a' && c <= 'f')
                out |= static_cast<uint32_t>(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F')
                out |= static_cast<uint32_t>(c - 'A' + 10);
            else
                return fail("invalid \\u escape");
        }
        return true;
    }

    bool parseString(std::string &out) {
        if (peek() != '"')
            return fail("expected string");
        m_pos++;

        while (true) {
            if (atEnd())
                return fail("unterminated string");
            const char c = m_text[m_pos++];
            if (c == '"')
                return true;

            if (c != '\\') {
                // Raw control characters are not legal inside a JSON string.
                if (static_cast<unsigned char>(c) < 0x20)
                    return fail("unescaped control character in string");
                out += c;
                continue;
            }

            if (atEnd())
                return fail("unterminated escape");
            const char esc = m_text[m_pos++];
            switch (esc) {
                case '"':
                    out += '"';
                    break;
                case '\\':
                    out += '\\';
                    break;
                case '/':
                    out += '/';
                    break;
                case 'b':
                    out += '\b';
                    break;
                case 'f':
                    out += '\f';
                    break;
                case 'n':
                    out += '\n';
                    break;
                case 'r':
                    out += '\r';
                    break;
                case 't':
                    out += '\t';
                    break;
                case 'u': {
                    uint32_t cp = 0;
                    if (!parseHex4(cp))
                        return false;
                    // Combine a surrogate pair into one code point.
                    if (cp >= 0xD800 && cp <= 0xDBFF && m_pos + 1 < m_text.size() &&
                        m_text[m_pos] == '\\' && m_text[m_pos + 1] == 'u') {
                        const size_t save = m_pos;
                        m_pos += 2;
                        uint32_t low = 0;
                        if (parseHex4(low) && low >= 0xDC00 && low <= 0xDFFF) {
                            cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
                        } else {
                            m_pos = save;
                        }
                    }
                    appendUtf8(out, cp);
                    break;
                }
                default:
                    return fail("invalid escape character");
            }
        }
    }

    bool parseNumber(Value &out) {
        const size_t start = m_pos;
        if (peek() == '-' || peek() == '+')
            m_pos++;
        bool digits = false;
        while (!atEnd() && peek() >= '0' && peek() <= '9') {
            m_pos++;
            digits = true;
        }
        if (!atEnd() && peek() == '.') {
            m_pos++;
            while (!atEnd() && peek() >= '0' && peek() <= '9') {
                m_pos++;
                digits = true;
            }
        }
        if (!digits)
            return fail("expected number");
        if (!atEnd() && (peek() == 'e' || peek() == 'E')) {
            m_pos++;
            if (!atEnd() && (peek() == '+' || peek() == '-'))
                m_pos++;
            while (!atEnd() && peek() >= '0' && peek() <= '9')
                m_pos++;
        }

        const std::string text = m_text.substr(start, m_pos - start);
        out = Value(std::strtod(text.c_str(), nullptr));
        return true;
    }

    bool parseArray(Value &out) {
        m_pos++; // '['
        m_depth++;
        Array items;

        skipWhitespace();
        if (peek() == ']') {
            m_pos++;
            m_depth--;
            out = Value(std::move(items));
            return true;
        }

        while (true) {
            skipWhitespace();
            Value item;
            if (!parseValue(item))
                return false;
            items.push_back(std::move(item));

            skipWhitespace();
            if (peek() == ',') {
                m_pos++;
                continue;
            }
            if (peek() == ']') {
                m_pos++;
                m_depth--;
                out = Value(std::move(items));
                return true;
            }
            return fail("expected ',' or ']'");
        }
    }

    bool parseObject(Value &out) {
        m_pos++; // '{'
        m_depth++;
        Object members;

        skipWhitespace();
        if (peek() == '}') {
            m_pos++;
            m_depth--;
            out = Value(std::move(members));
            return true;
        }

        while (true) {
            skipWhitespace();
            std::string key;
            if (!parseString(key))
                return false;

            skipWhitespace();
            if (peek() != ':')
                return fail("expected ':'");
            m_pos++;

            skipWhitespace();
            Value value;
            if (!parseValue(value))
                return false;
            members[key] = std::move(value);

            skipWhitespace();
            if (peek() == ',') {
                m_pos++;
                continue;
            }
            if (peek() == '}') {
                m_pos++;
                m_depth--;
                out = Value(std::move(members));
                return true;
            }
            return fail("expected ',' or '}'");
        }
    }
};

} // namespace

// --- Value accessors -------------------------------------------------------

bool Value::asBool(bool fallback) const {
    return m_type == Type::Bool ? m_bool : fallback;
}

double Value::asNumber(double fallback) const {
    return m_type == Type::Number ? m_number : fallback;
}

uint64_t Value::asUInt64(uint64_t fallback) const {
    if (m_type == Type::Number)
        return m_number < 0 ? fallback : static_cast<uint64_t>(m_number);
    // Values that cannot survive a double are stored as strings, hex or decimal.
    if (m_type == Type::String && !m_string.empty()) {
        const int base =
            (m_string.compare(0, 2, "0x") == 0 || m_string.compare(0, 2, "0X") == 0) ? 16 : 10;
        const char *begin = m_string.c_str() + (base == 16 ? 2 : 0);
        char *end = nullptr;
        const unsigned long long parsed = std::strtoull(begin, &end, base);
        if (end && *end == '\0' && end != begin)
            return parsed;
    }
    return fallback;
}

std::string Value::asString(const std::string &fallback) const {
    return m_type == Type::String ? m_string : fallback;
}

const Array &Value::asArray() const {
    return m_type == Type::Array ? m_array : kEmptyArray;
}

const Object &Value::asObject() const {
    return m_type == Type::Object ? m_object : kEmptyObject;
}

const Value &Value::operator[](const std::string &key) const {
    if (m_type != Type::Object)
        return kNullValue;
    auto it = m_object.find(key);
    return it == m_object.end() ? kNullValue : it->second;
}

// --- Serialisation ---------------------------------------------------------

std::string escape(const std::string &s) {
    std::string out;
    out.reserve(s.size() + 8);
    for (unsigned char c : s) {
        switch (c) {
            case '"':
                out += "\\\"";
                break;
            case '\\':
                out += "\\\\";
                break;
            case '\b':
                out += "\\b";
                break;
            case '\f':
                out += "\\f";
                break;
            case '\n':
                out += "\\n";
                break;
            case '\r':
                out += "\\r";
                break;
            case '\t':
                out += "\\t";
                break;
            default:
                // Every remaining control character needs a \u escape; emitting
                // it raw produces JSON no parser will accept.
                if (c < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04X", c);
                    out += buf;
                } else {
                    out += static_cast<char>(c);
                }
        }
    }
    return out;
}

std::string Value::serialize(bool pretty, int indent) const {
    std::ostringstream out;

    switch (m_type) {
        case Type::Null:
            out << "null";
            break;
        case Type::Bool:
            out << (m_bool ? "true" : "false");
            break;
        case Type::Number:
            out << formatNumber(m_number);
            break;
        case Type::String:
            out << '"' << escape(m_string) << '"';
            break;
        case Type::Array: {
            if (m_array.empty()) {
                out << "[]";
                break;
            }
            out << '[';
            for (size_t i = 0; i < m_array.size(); i++) {
                if (i)
                    out << ',';
                if (pretty) {
                    out << '\n';
                    writeIndent(out, indent + 1);
                }
                out << m_array[i].serialize(pretty, indent + 1);
            }
            if (pretty) {
                out << '\n';
                writeIndent(out, indent);
            }
            out << ']';
            break;
        }
        case Type::Object: {
            if (m_object.empty()) {
                out << "{}";
                break;
            }
            out << '{';
            bool first = true;
            for (const auto &[key, value] : m_object) {
                if (!first)
                    out << ',';
                first = false;
                if (pretty) {
                    out << '\n';
                    writeIndent(out, indent + 1);
                }
                out << '"' << escape(key) << "\":";
                if (pretty)
                    out << ' ';
                out << value.serialize(pretty, indent + 1);
            }
            if (pretty) {
                out << '\n';
                writeIndent(out, indent);
            }
            out << '}';
            break;
        }
    }

    return out.str();
}

bool parse(const std::string &text, Value &outValue, std::string *outError) {
    Parser parser(text);
    std::string error;
    if (parser.parse(outValue, error)) {
        return true;
    }
    if (outError)
        *outError = error;
    return false;
}

} // namespace Json
} // namespace Shirayuki
