#include "ghostclaw/common/json_util.hpp"

#include <cctype>
#include <sstream>

namespace ghostclaw::common {

std::string json_escape(const std::string &value) {
  std::string escaped;
  escaped.reserve(value.size() + 8);
  for (const char ch : value) {
    switch (ch) {
    case '"':
      escaped += "\\\"";
      break;
    case '\\':
      escaped += "\\\\";
      break;
    case '\n':
      escaped += "\\n";
      break;
    case '\r':
      escaped += "\\r";
      break;
    case '\t':
      escaped += "\\t";
      break;
    default:
      escaped.push_back(ch);
      break;
    }
  }
  return escaped;
}

std::string json_unescape(const std::string &raw) {
  std::string out;
  out.reserve(raw.size());
  bool escaped = false;
  for (const char ch : raw) {
    if (!escaped) {
      if (ch == '\\') {
        escaped = true;
      } else {
        out.push_back(ch);
      }
      continue;
    }
    switch (ch) {
    case 'n':
      out.push_back('\n');
      break;
    case 'r':
      out.push_back('\r');
      break;
    case 't':
      out.push_back('\t');
      break;
    default:
      out.push_back(ch);
      break;
    }
    escaped = false;
  }
  return out;
}

std::size_t json_find_key(const std::string &json, const std::string &key, std::size_t from) {
  const std::string quoted = "\"" + key + "\"";
  return json.find(quoted, from);
}

std::size_t json_skip_ws(const std::string &text, std::size_t pos) {
  while (pos < text.size() && std::isspace(static_cast<unsigned char>(text[pos])) != 0) {
    ++pos;
  }
  return pos;
}

std::size_t json_find_string_end(const std::string &json, std::size_t quote_pos) {
  bool escaped = false;
  for (std::size_t i = quote_pos + 1; i < json.size(); ++i) {
    const char ch = json[i];
    if (!escaped && ch == '"') {
      return i;
    }
    if (!escaped && ch == '\\') {
      escaped = true;
      continue;
    }
    escaped = false;
  }
  return std::string::npos;
}

std::size_t json_find_matching_token(const std::string &json, std::size_t open_pos,
                                      const char open_ch, const char close_ch) {
  if (open_pos >= json.size() || json[open_pos] != open_ch) {
    return std::string::npos;
  }
  std::size_t depth = 0;
  bool in_string = false;
  bool escaped = false;
  for (std::size_t i = open_pos; i < json.size(); ++i) {
    const char ch = json[i];
    if (in_string) {
      if (!escaped && ch == '"') {
        in_string = false;
      } else if (!escaped && ch == '\\') {
        escaped = true;
        continue;
      }
      escaped = false;
      continue;
    }
    if (ch == '"') {
      in_string = true;
      escaped = false;
      continue;
    }
    if (ch == open_ch) {
      ++depth;
    } else if (ch == close_ch) {
      if (depth == 0) {
        return std::string::npos;
      }
      --depth;
      if (depth == 0) {
        return i;
      }
    }
  }
  return std::string::npos;
}

std::string json_get_string(const std::string &json, const std::string &field) {
  const auto key_pos = json_find_key(json, field);
  if (key_pos == std::string::npos) {
    return "";
  }
  const auto colon = json.find(':', key_pos + field.size() + 2);
  if (colon == std::string::npos) {
    return "";
  }
  std::size_t pos = json_skip_ws(json, colon + 1);
  if (pos >= json.size() || json[pos] != '"') {
    return "";
  }
  const auto end = json_find_string_end(json, pos);
  if (end == std::string::npos || end <= pos) {
    return "";
  }
  return json_unescape(json.substr(pos + 1, end - pos - 1));
}

std::string json_get_number(const std::string &json, const std::string &field) {
  const auto key_pos = json_find_key(json, field);
  if (key_pos == std::string::npos) {
    return "";
  }
  const auto colon = json.find(':', key_pos + field.size() + 2);
  if (colon == std::string::npos) {
    return "";
  }
  std::size_t pos = json_skip_ws(json, colon + 1);
  if (pos >= json.size() || json[pos] == '"') {
    return "";
  }
  const std::size_t start = pos;
  while (pos < json.size()) {
    const char ch = json[pos];
    if (ch == ',' || ch == '}' || ch == ']' || std::isspace(static_cast<unsigned char>(ch)) != 0) {
      break;
    }
    ++pos;
  }
  if (pos <= start) {
    return "";
  }
  return json.substr(start, pos - start);
}

std::string json_get_object(const std::string &json, const std::string &field) {
  const auto key_pos = json_find_key(json, field);
  if (key_pos == std::string::npos) {
    return "";
  }
  const auto colon = json.find(':', key_pos + field.size() + 2);
  if (colon == std::string::npos) {
    return "";
  }
  std::size_t pos = json_skip_ws(json, colon + 1);
  if (pos >= json.size() || json[pos] != '{') {
    return "";
  }
  const auto end = json_find_matching_token(json, pos, '{', '}');
  if (end == std::string::npos || end < pos) {
    return "";
  }
  return json.substr(pos, end - pos + 1);
}

std::string json_get_array(const std::string &json, const std::string &field) {
  const auto key_pos = json_find_key(json, field);
  if (key_pos == std::string::npos) {
    return "";
  }
  const auto colon = json.find(':', key_pos + field.size() + 2);
  if (colon == std::string::npos) {
    return "";
  }
  std::size_t pos = json_skip_ws(json, colon + 1);
  if (pos >= json.size() || json[pos] != '[') {
    return "";
  }
  const auto end = json_find_matching_token(json, pos, '[', ']');
  if (end == std::string::npos || end < pos) {
    return "";
  }
  return json.substr(pos, end - pos + 1);
}

std::vector<std::string> json_get_string_array(const std::string &json, const std::string &field) {
  const std::string array_str = json_get_array(json, field);
  if (array_str.empty()) {
    return {};
  }

  std::vector<std::string> out;
  std::size_t pos = 1; // skip opening [
  while (pos < array_str.size()) {
    pos = json_skip_ws(array_str, pos);
    if (pos >= array_str.size() || array_str[pos] == ']') {
      break;
    }
    if (array_str[pos] == ',') {
      ++pos;
      continue;
    }
    if (array_str[pos] == '"') {
      const auto end = json_find_string_end(array_str, pos);
      if (end == std::string::npos || end <= pos) {
        break;
      }
      out.push_back(json_unescape(array_str.substr(pos + 1, end - pos - 1)));
      pos = end + 1;
    } else {
      ++pos;
    }
  }
  return out;
}

JsonFlatMap json_parse_flat(const std::string &json) {
  JsonFlatMap result;
  if (json.size() < 2 || json.front() != '{') {
    return result;
  }

  std::size_t pos = 1; // skip opening {
  while (pos < json.size()) {
    pos = json_skip_ws(json, pos);
    if (pos >= json.size() || json[pos] == '}') {
      break;
    }
    if (json[pos] == ',') {
      ++pos;
      continue;
    }

    // expect key
    if (json[pos] != '"') {
      ++pos;
      continue;
    }
    const auto key_end = json_find_string_end(json, pos);
    if (key_end == std::string::npos) {
      break;
    }
    const std::string key = json.substr(pos + 1, key_end - pos - 1);
    pos = key_end + 1;

    // expect colon
    pos = json_skip_ws(json, pos);
    if (pos >= json.size() || json[pos] != ':') {
      break;
    }
    ++pos;
    pos = json_skip_ws(json, pos);
    if (pos >= json.size()) {
      break;
    }

    // read value
    if (json[pos] == '"') {
      const auto val_end = json_find_string_end(json, pos);
      if (val_end == std::string::npos) {
        break;
      }
      result[key] = json_unescape(json.substr(pos + 1, val_end - pos - 1));
      pos = val_end + 1;
    } else if (json[pos] == '{' || json[pos] == '[') {
      const char open = json[pos];
      const char close = (open == '{') ? '}' : ']';
      const auto end = json_find_matching_token(json, pos, open, close);
      if (end == std::string::npos) {
        break;
      }
      result[key] = json.substr(pos, end - pos + 1);
      pos = end + 1;
    } else if (json[pos] == 't' || json[pos] == 'f' || json[pos] == 'n') {
      // true/false/null
      const std::size_t start = pos;
      while (pos < json.size() && json[pos] != ',' && json[pos] != '}' && json[pos] != ']' &&
             std::isspace(static_cast<unsigned char>(json[pos])) == 0) {
        ++pos;
      }
      result[key] = json.substr(start, pos - start);
    } else {
      // number
      const std::size_t start = pos;
      while (pos < json.size() && json[pos] != ',' && json[pos] != '}' && json[pos] != ']' &&
             std::isspace(static_cast<unsigned char>(json[pos])) == 0) {
        ++pos;
      }
      result[key] = json.substr(start, pos - start);
    }
  }

  return result;
}

std::vector<std::string> json_split_top_level_objects(const std::string &array_json) {
  std::vector<std::string> out;
  if (array_json.size() < 2 || array_json.front() != '[' || array_json.back() != ']') {
    return out;
  }

  bool in_string = false;
  bool escaped = false;
  std::size_t depth = 0;
  std::size_t current_start = std::string::npos;
  for (std::size_t i = 1; i + 1 < array_json.size(); ++i) {
    const char ch = array_json[i];
    if (in_string) {
      if (!escaped && ch == '"') {
        in_string = false;
      } else if (!escaped && ch == '\\') {
        escaped = true;
        continue;
      }
      escaped = false;
      continue;
    }
    if (ch == '"') {
      in_string = true;
      escaped = false;
      continue;
    }
    if (ch == '{') {
      if (depth == 0) {
        current_start = i;
      }
      ++depth;
      continue;
    }
    if (ch == '}') {
      if (depth == 0) {
        continue;
      }
      --depth;
      if (depth == 0 && current_start != std::string::npos) {
        out.push_back(array_json.substr(current_start, i - current_start + 1));
        current_start = std::string::npos;
      }
    }
  }
  return out;
}

} // namespace ghostclaw::common
