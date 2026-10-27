/**
 * @file Json.h
 * @brief Minimal JSON helpers for building replies and reading the few flat
 *        JSON documents Webmon receives (luna-send replies, page requests).
 */
#ifndef WEBMON_JSON_H
#define WEBMON_JSON_H

#include <string>

namespace webmon::json
{

/** Escapes @p text for use inside a JSON string literal (without the quotes). */
std::string Escape(const std::string &text);

/** @p text as a quoted JSON string. */
inline std::string Quote(const std::string &text)
{
    return "\"" + Escape(text) + "\"";
}

/** "true" or "false". */
inline const char *Bool(bool value)
{
    return value ? "true" : "false";
}

/**
 * Reads the string value of "@p key" from a flat JSON object, decoding the
 * usual escapes.
 * @return False if the key is missing or not a string.
 */
bool StringField(const std::string &document, const std::string &key, std::string *value);

} // namespace webmon::json

#endif // WEBMON_JSON_H
