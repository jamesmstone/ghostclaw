#pragma once

#include <cstddef>
#include <string>
#include <unordered_map>
#include <vector>

namespace ghostclaw::common {

/// Escape a string for embedding inside a JSON string literal.
[[nodiscard]] std::string json_escape(const std::string &value);

/// Unescape a JSON-encoded string (handles \n, \r, \t and pass-through).
[[nodiscard]] std::string json_unescape(const std::string &raw);

/// Find the position of a JSON key in a JSON string.
[[nodiscard]] std::size_t json_find_key(const std::string &json, const std::string &key,
                                        std::size_t from = 0);

/// Skip whitespace starting at pos, returning the first non-whitespace position.
[[nodiscard]] std::size_t json_skip_ws(const std::string &text, std::size_t pos);

/// Find the closing quote of a JSON string starting at quote_pos.
[[nodiscard]] std::size_t json_find_string_end(const std::string &json, std::size_t quote_pos);

/// Find matching bracket/brace for nested JSON structures.
[[nodiscard]] std::size_t json_find_matching_token(const std::string &json, std::size_t open_pos,
                                                    char open_ch, char close_ch);

/// Extract a string field value from a JSON document.
[[nodiscard]] std::string json_get_string(const std::string &json, const std::string &field);

/// Extract a numeric field value (as string) from a JSON document.
[[nodiscard]] std::string json_get_number(const std::string &json, const std::string &field);

/// Extract a nested JSON object field (including braces) from a JSON document.
[[nodiscard]] std::string json_get_object(const std::string &json, const std::string &field);

/// Extract a nested JSON array field (including brackets) from a JSON document.
[[nodiscard]] std::string json_get_array(const std::string &json, const std::string &field);

/// Extract a string array from a JSON array string like ["a","b"].
[[nodiscard]] std::vector<std::string> json_get_string_array(const std::string &json,
                                                              const std::string &field);

/// Parse a flat JSON object into a key→value map (string values only, top-level).
using JsonFlatMap = std::unordered_map<std::string, std::string>;
[[nodiscard]] JsonFlatMap json_parse_flat(const std::string &json);

/// Split a JSON array of objects into individual object strings.
[[nodiscard]] std::vector<std::string> json_split_top_level_objects(const std::string &array_json);

} // namespace ghostclaw::common
