#include "Json.hpp"

#include <cstdint>
#include <cstdio>

namespace charon::json {
namespace {

class Reader {
public:
    explicit Reader(std::string_view text)
        : m_text(text)
    {
    }

    void skipSpace()
    {
        while (m_at < m_text.size()
            && (m_text[m_at] == ' ' || m_text[m_at] == '\t' || m_text[m_at] == '\n' || m_text[m_at] == '\r'))
            ++m_at;
    }

    bool take(char c)
    {
        skipSpace();
        if (m_at < m_text.size() && m_text[m_at] == c) {
            ++m_at;
            return true;
        }
        return false;
    }

    bool atEnd()
    {
        skipSpace();
        return m_at == m_text.size();
    }

    char peek()
    {
        skipSpace();
        return m_at < m_text.size() ? m_text[m_at] : '\0';
    }

    std::optional<std::string> string()
    {
        if (!take('"'))
            return std::nullopt;
        std::string out;
        while (m_at < m_text.size()) {
            const char c = m_text[m_at++];
            if (c == '"')
                return out;
            if (c != '\\') {
                out += c;
                continue;
            }
            if (m_at >= m_text.size())
                return std::nullopt;
            switch (const char escaped = m_text[m_at++]) {
            case '"':
            case '\\':
            case '/':
                out += escaped;
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
                auto unit = hex4();
                if (!unit)
                    return std::nullopt;
                uint32_t code = *unit;
                // A pair of surrogates makes one character beyond the first plane.
                if (code >= 0xd800 && code < 0xdc00 && m_text.substr(m_at, 2) == "\\u") {
                    m_at += 2;
                    const auto low = hex4();
                    if (!low || *low < 0xdc00 || *low >= 0xe000)
                        return std::nullopt;
                    code = 0x10000 + ((code - 0xd800) << 10) + (*low - 0xdc00);
                }
                appendUtf8(out, code);
                break;
            }
            default:
                return std::nullopt;
            }
        }
        return std::nullopt;
    }

    // Whatever value comes, passed by; false when it is not one.
    bool skipValue()
    {
        const char c = peek();
        if (c == '"')
            return string().has_value();
        if (c == '{' || c == '[') {
            const char close = c == '{' ? '}' : ']';
            take(c);
            if (take(close))
                return true;
            do {
                if (close == '}' && (!string() || !take(':')))
                    return false;
                if (!skipValue())
                    return false;
            } while (take(','));
            return take(close);
        }
        // Numbers, true, false and null.
        const std::size_t start = m_at;
        while (m_at < m_text.size() && m_text[m_at] != ',' && m_text[m_at] != '}' && m_text[m_at] != ']'
            && m_text[m_at] != ' ' && m_text[m_at] != '\n')
            ++m_at;
        return m_at > start;
    }

private:
    std::optional<uint32_t> hex4()
    {
        if (m_at + 4 > m_text.size())
            return std::nullopt;
        uint32_t value = 0;
        for (int i = 0; i < 4; ++i) {
            const char c = m_text[m_at++];
            value <<= 4;
            if (c >= '0' && c <= '9')
                value |= uint32_t(c - '0');
            else if (c >= 'a' && c <= 'f')
                value |= uint32_t(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F')
                value |= uint32_t(c - 'A' + 10);
            else
                return std::nullopt;
        }
        return value;
    }

    static void appendUtf8(std::string& out, uint32_t code)
    {
        if (code < 0x80) {
            out += char(code);
        } else if (code < 0x800) {
            out += char(0xc0 | (code >> 6));
            out += char(0x80 | (code & 0x3f));
        } else if (code < 0x10000) {
            out += char(0xe0 | (code >> 12));
            out += char(0x80 | ((code >> 6) & 0x3f));
            out += char(0x80 | (code & 0x3f));
        } else {
            out += char(0xf0 | (code >> 18));
            out += char(0x80 | ((code >> 12) & 0x3f));
            out += char(0x80 | ((code >> 6) & 0x3f));
            out += char(0x80 | (code & 0x3f));
        }
    }

    std::string_view m_text;
    std::size_t m_at = 0;
};

} // namespace

std::string quoted(std::string_view text)
{
    std::string out = "\"";
    for (const char c : text) {
        switch (c) {
        case '"':
            out += "\\\"";
            break;
        case '\\':
            out += "\\\\";
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
            if (static_cast<unsigned char>(c) < 0x20) {
                char escaped[8];
                std::snprintf(escaped, sizeof(escaped), "\\u%04x", unsigned(c));
                out += escaped;
            } else {
                out += c;
            }
        }
    }
    return out + '"';
}

std::string list(const std::vector<std::string>& items)
{
    std::string out = "[";
    for (std::size_t i = 0; i < items.size(); ++i)
        out += (i ? "," : "") + quoted(items[i]);
    return out + ']';
}

std::optional<std::map<std::string, std::string>> parseObject(std::string_view text)
{
    Reader reader(text);
    std::map<std::string, std::string> values;
    if (!reader.take('{'))
        return std::nullopt;
    if (!reader.take('}')) {
        do {
            auto key = reader.string();
            if (!key || !reader.take(':'))
                return std::nullopt;
            if (reader.peek() == '"') {
                auto value = reader.string();
                if (!value)
                    return std::nullopt;
                values[*key] = std::move(*value);
            } else if (!reader.skipValue()) {
                return std::nullopt;
            }
        } while (reader.take(','));
        if (!reader.take('}'))
            return std::nullopt;
    }
    if (!reader.atEnd())
        return std::nullopt;
    return values;
}

} // namespace charon::json
