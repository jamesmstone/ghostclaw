#include "ghostclaw/agent/stream_parser.hpp"

#include "ghostclaw/common/fs.hpp"

#include <regex>

namespace ghostclaw::agent {

StreamParser::StreamParser(ToolCallCallback on_tool_call) : on_tool_call_(std::move(on_tool_call)) {}

void StreamParser::feed(const std::string_view chunk) {
  content_.append(chunk);
  buffer_.append(chunk);
  parse_buffer();
}

void StreamParser::finish() { parse_buffer(); }

std::string StreamParser::accumulated_content() const { return content_; }

std::vector<ParsedToolCall> StreamParser::tool_calls() const { return tool_calls_; }

namespace {

std::string unescape_json_string(const std::string &value) {
  std::string out;
  out.reserve(value.size());
  bool escaped = false;
  for (char ch : value) {
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
    case '"':
      out.push_back('"');
      break;
    case '\\':
      out.push_back('\\');
      break;
    default:
      out.push_back(ch);
      break;
    }
    escaped = false;
  }
  return out;
}

} // namespace

tools::ToolArgs StreamParser::parse_args_json(const std::string &json) {
  tools::ToolArgs args;
  std::regex pair_re(R"re("([^"]+)"\s*:\s*"([^"]*)")re");
  auto begin = std::sregex_iterator(json.begin(), json.end(), pair_re);
  auto end = std::sregex_iterator();
  for (auto it = begin; it != end; ++it) {
    args[(*it)[1].str()] = (*it)[2].str();
  }

  std::regex scalar_re(R"re("([^"]+)"\s*:\s*(-?[0-9]+(?:\.[0-9]+)?|true|false))re");
  begin = std::sregex_iterator(json.begin(), json.end(), scalar_re);
  for (auto it = begin; it != end; ++it) {
    args[(*it)[1].str()] = (*it)[2].str();
  }

  return args;
}

void StreamParser::parse_buffer() {
  // OpenAI-like tool_calls format
  std::regex openai_re(
      R"re("tool_calls"\s*:\s*\[\s*\{[^\}]*"id"\s*:\s*"([^"]+)"[^\}]*"name"\s*:\s*"([^"]+)"[^\}]*"arguments"\s*:\s*"((?:\\.|[^"])*)")re");
  std::smatch match;
  std::string temp = buffer_;
  while (std::regex_search(temp, match, openai_re)) {
    ParsedToolCall call;
    call.id = match[1].str();
    call.name = match[2].str();
    const std::string args_json = unescape_json_string(match[3].str());
    call.arguments = parse_args_json(args_json);
    const std::string signature = call.id + "|" + call.name + "|" + args_json;
    if (!seen_call_signatures_.contains(signature)) {
      seen_call_signatures_.insert(signature);
      tool_calls_.push_back(call);
      if (on_tool_call_) {
        on_tool_call_(call);
      }
    }
    temp = match.suffix().str();
  }

  // Anthropic-like tool_use format
  std::regex anthropic_re(
      R"re("type"\s*:\s*"tool_use"[^\}]*"name"\s*:\s*"([^"]+)"[^\}]*"input"\s*:\s*(\{[^\}]*\}))re");
  temp = buffer_;
  while (std::regex_search(temp, match, anthropic_re)) {
    ParsedToolCall call;
    call.id = "tool-" + std::to_string(tool_calls_.size() + 1);
    call.name = match[1].str();
    call.arguments = parse_args_json(match[2].str());
    const std::string signature = call.name + "|" + match[2].str();
    if (!seen_call_signatures_.contains(signature)) {
      seen_call_signatures_.insert(signature);
      tool_calls_.push_back(call);
      if (on_tool_call_) {
        on_tool_call_(call);
      }
    }
    temp = match.suffix().str();
  }

  // Text fallback format
  std::regex xml_re(R"(<tool>([^<]+)</tool>\s*<args>(\{[^\}]*\})</args>)");
  temp = buffer_;
  while (std::regex_search(temp, match, xml_re)) {
    ParsedToolCall call;
    call.id = "xml-" + std::to_string(tool_calls_.size() + 1);
    call.name = common::trim(match[1].str());
    call.arguments = parse_args_json(match[2].str());
    const std::string signature = call.name + "|" + match[2].str();
    if (!seen_call_signatures_.contains(signature)) {
      seen_call_signatures_.insert(signature);
      tool_calls_.push_back(call);
      if (on_tool_call_) {
        on_tool_call_(call);
      }
    }
    temp = match.suffix().str();
  }

  constexpr std::size_t kMaxBuffer = 256 * 1024;
  constexpr std::size_t kKeepTail = 128 * 1024;
  if (buffer_.size() > kMaxBuffer) {
    buffer_.erase(0, buffer_.size() - kKeepTail);
  }
}

} // namespace ghostclaw::agent
