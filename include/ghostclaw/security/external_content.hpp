#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ghostclaw::security {

enum class ExternalSource {
  Email,
  Webhook,
  Api,
  Browser,
  ChannelMetadata,
  WebSearch,
  WebFetch,
  Unknown,
};

extern const std::string EXTERNAL_START;
extern const std::string EXTERNAL_END;

[[nodiscard]] std::string external_source_label(ExternalSource source);
[[nodiscard]] std::vector<std::string> detect_suspicious_patterns(const std::string &content);
[[nodiscard]] std::string normalize_homoglyphs(const std::string &content);
[[nodiscard]] std::string sanitize_external_markers(const std::string &content);

[[nodiscard]] std::string wrap_external_content(
    const std::string &content, ExternalSource source,
    const std::optional<std::string> &sender = std::nullopt,
    const std::optional<std::string> &subject = std::nullopt,
    bool include_warning = true);

[[nodiscard]] bool is_external_hook_session(std::string_view session_key);
[[nodiscard]] ExternalSource source_from_session_key(std::string_view session_key);

} // namespace ghostclaw::security
