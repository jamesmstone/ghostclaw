#include "ghostclaw/tools/builtin/web_fetch.hpp"

#include <curl/curl.h>

#include <regex>

namespace ghostclaw::tools {

namespace {

size_t write_callback(char *ptr, size_t size, size_t nmemb, void *userdata) {
  auto *output = static_cast<std::string *>(userdata);
  const auto total = size * nmemb;
  output->append(ptr, total);
  return total;
}

std::string strip_html(const std::string &html) {
  std::string text =
      std::regex_replace(html, std::regex("<script[^>]*>[\\s\\S]*?</script>", std::regex::icase), " ");
  text = std::regex_replace(text, std::regex("<style[^>]*>[\\s\\S]*?</style>", std::regex::icase), " ");
  text = std::regex_replace(text, std::regex("</?(p|div|h1|h2|h3|h4|h5|h6|li|br)[^>]*>",
                                             std::regex::icase),
                            "\n");
  text = std::regex_replace(text, std::regex("<[^>]+>"), " ");
  text = std::regex_replace(text, std::regex("[ \t\r\f\v]+"), " ");
  text = std::regex_replace(text, std::regex("\n+"), "\n");
  return text;
}

} // namespace

std::string_view WebFetchTool::name() const { return "web_fetch"; }

std::string_view WebFetchTool::description() const { return "Fetch and extract readable text from URL"; }

std::string WebFetchTool::parameters_schema() const {
  return R"({"type":"object","required":["url"],"properties":{"url":{"type":"string"}}})";
}

common::Result<ToolResult> WebFetchTool::execute(const ToolArgs &args, const ToolContext &) {
  const auto it = args.find("url");
  if (it == args.end() || it->second.empty()) {
    return common::Result<ToolResult>::failure("Missing url");
  }

  CURL *curl = curl_easy_init();
  if (curl == nullptr) {
    return common::Result<ToolResult>::failure("curl init failed");
  }

  std::string body;
  curl_easy_setopt(curl, CURLOPT_URL, it->second.c_str());
  curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, 15000L);
  curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &body);

  const CURLcode code = curl_easy_perform(curl);
  long status = 0;
  curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
  curl_easy_cleanup(curl);

  if (code != CURLE_OK || status < 200 || status >= 300) {
    return common::Result<ToolResult>::failure("HTTP fetch failed");
  }

  ToolResult result;
  result.output = strip_html(body);
  if (result.output.size() > 50 * 1024) {
    result.output.resize(50 * 1024);
    result.truncated = true;
  }

  return common::Result<ToolResult>::success(std::move(result));
}

bool WebFetchTool::is_safe() const { return true; }

std::string_view WebFetchTool::group() const { return "web"; }

} // namespace ghostclaw::tools
