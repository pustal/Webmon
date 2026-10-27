/**
 * @file Json.cpp
 * @brief Implementation of Json.h.
 */
#include "Json.h"

#include <cstdio>
#include <cstdlib>

namespace webmon::json
{

std::string Escape(const std::string &text)
{
    std::string out;
    out.reserve(text.size() + 8);
    for (const char c : text) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char buffer[8];
                    std::snprintf(buffer, sizeof(buffer), "\\u%04x", static_cast<unsigned>(c));
                    out += buffer;
                } else {
                    out += c;
                }
        }
    }
    return out;
}

bool StringField(const std::string &document, const std::string &key, std::string *value)
{
    const std::string needle = "\"" + key + "\"";
    std::size_t at           = document.find(needle);
    if (at == std::string::npos)
        return false;
    at = document.find(':', at + needle.size());
    if (at == std::string::npos)
        return false;
    at = document.find_first_not_of(" \t\r\n", at + 1);
    if (at == std::string::npos || document[at] != '"')
        return false;

    std::string out;
    for (std::size_t i = at + 1; i < document.size(); ++i) {
        const char c = document[i];
        if (c == '"')
            break;
        if (c != '\\' || i + 1 >= document.size()) {
            out += c;
            continue;
        }
        const char escaped = document[++i];
        switch (escaped) {
            case 'n': out += '\n'; break;
            case 't': out += '\t'; break;
            case 'r': out += '\r'; break;
            case 'u':
                // Only the ASCII range is needed here (shell output, file names typed in the page).
                if (i + 4 < document.size()) {
                    out += static_cast<char>(std::strtol(document.substr(i + 1, 4).c_str(), nullptr, 16));
                    i += 4;
                }
                break;
            default: out += escaped; break;
        }
    }
    *value = std::move(out);
    return true;
}

} // namespace webmon::json
