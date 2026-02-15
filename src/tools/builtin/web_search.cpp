#include "ghostclaw/tools/builtin/web_search.hpp"

#include "ghostclaw/common/fs.hpp"
#include "ghostclaw/common/json_util.hpp"

#include <curl/curl.h>

#include <sstream>

namespace ghostclaw::tools {

namespace {

size_t write_callback(char *ptr, size_t size, size_t nmemb, void *userdata) {
  auto *output = static_cast<std::string *>(userdata);
  const auto total = size * nmemb;
  output->append(ptr, total);
  return total;
}

std::string url_encode(const std::string &value) {
  CURL *curl = curl_easy_init();
  if (curl == nullptr) {
    return value;
  }
  char *encoded = curl_easy_escape(curl, value.c_str(), static_cast<int>(value.size()));
  std::string result = encoded != nullptr ? encoded : value;
  curl_free(encoded);
  curl_easy_cleanup(curl);
  return result;
}

} // namespace

WebSearchTool::WebSearchTool(WebSearchConfig config) : config_(std::move(config)) {}

std::string_view WebSearchTool::name() const { return "web_search"; }

std::string_view WebSearchTool::description() const { return "Search the web for relevant results"; }

std::string WebSearchTool::parameters_schema() const {
  return R"json({"type":"object","required":["query"],"properties":{"query":{"type":"string","description":"The search query"},"count":{"type":"integer","description":"Number of results (1-10, default 5)"}}})json";
}

common::Result<ToolResult> WebSearchTool::execute(const ToolArgs &args, const ToolContext &) {
  const auto query_it = args.find("query");
  if (query_it == args.end() || query_it->second.empty()) {
    return common::Result<ToolResult>::failure("Missing query");
  }

  // Choose provider: brave if API key available, else duckduckgo
  bool use_brave = !config_.brave_api_key.empty();
  if (config_.provider == "brave") {
    use_brave = true;
  } else if (config_.provider == "duckduckgo") {
    use_brave = false;
  }

  if (use_brave && !config_.brave_api_key.empty()) {
    return search_brave(query_it->second);
  }
  return search_duckduckgo(query_it->second);
}

common::Result<ToolResult> WebSearchTool::search_brave(const std::string &query) {
  CURL *curl = curl_easy_init();
  if (curl == nullptr) {
    return common::Result<ToolResult>::failure("curl init failed");
  }

  const std::string url =
      "https://api.search.brave.com/res/v1/web/search?q=" + url_encode(query) + "&count=5";

  struct curl_slist *headers = nullptr;
  const std::string auth_header = "X-Subscription-Token: " + config_.brave_api_key;
  headers = curl_slist_append(headers, auth_header.c_str());
  headers = curl_slist_append(headers, "Accept: application/json");

  std::string body;
  curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
  curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
  curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, 15000L);
  curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &body);

  const CURLcode code = curl_easy_perform(curl);
  long status = 0;
  curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
  curl_slist_free_all(headers);
  curl_easy_cleanup(curl);

  if (code != CURLE_OK || status < 200 || status >= 300) {
    return common::Result<ToolResult>::failure("Brave Search API request failed (HTTP " +
                                               std::to_string(status) + ")");
  }

  // Parse the Brave Search JSON response
  // Structure: {"web":{"results":[{"title":"...","url":"...","description":"..."},...]}}
  std::ostringstream out;
  const std::string web_obj = common::json_get_object(body, "web");
  if (web_obj.empty()) {
    out << "No results found for: " << query << "\n";
  } else {
    const std::string results_arr = common::json_get_array(web_obj, "results");
    if (results_arr.empty()) {
      out << "No results found for: " << query << "\n";
    } else {
      // Parse individual result objects from the array
      std::size_t pos = 1; // skip opening [
      int count = 0;
      while (pos < results_arr.size() && count < 5) {
        pos = common::json_skip_ws(results_arr, pos);
        if (pos >= results_arr.size() || results_arr[pos] == ']') {
          break;
        }
        if (results_arr[pos] == ',') {
          ++pos;
          continue;
        }
        if (results_arr[pos] == '{') {
          const auto end =
              common::json_find_matching_token(results_arr, pos, '{', '}');
          if (end == std::string::npos) {
            break;
          }
          const std::string result_obj = results_arr.substr(pos, end - pos + 1);
          const std::string title = common::json_get_string(result_obj, "title");
          const std::string result_url = common::json_get_string(result_obj, "url");
          const std::string desc = common::json_get_string(result_obj, "description");

          out << "- [" << title << "](" << result_url << ")\n";
          if (!desc.empty()) {
            out << "  " << desc << "\n";
          }
          pos = end + 1;
          ++count;
        } else {
          ++pos;
        }
      }
    }
  }

  ToolResult result;
  result.output = out.str();
  result.metadata["provider"] = "brave";
  return common::Result<ToolResult>::success(std::move(result));
}

common::Result<ToolResult> WebSearchTool::search_duckduckgo(const std::string &query) {
  CURL *curl = curl_easy_init();
  if (curl == nullptr) {
    return common::Result<ToolResult>::failure("curl init failed");
  }

  const std::string url =
      "https://api.duckduckgo.com/?q=" + url_encode(query) + "&format=json&no_html=1";

  std::string body;
  curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
  curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, 15000L);
  curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &body);

  const CURLcode code = curl_easy_perform(curl);
  long status = 0;
  curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
  curl_easy_cleanup(curl);

  if (code != CURLE_OK || status < 200 || status >= 300) {
    return common::Result<ToolResult>::failure("DuckDuckGo API request failed (HTTP " +
                                               std::to_string(status) + ")");
  }

  std::ostringstream out;

  // AbstractText is the main answer
  const std::string abstract_text = common::json_get_string(body, "AbstractText");
  const std::string abstract_url = common::json_get_string(body, "AbstractURL");
  const std::string heading = common::json_get_string(body, "Heading");

  if (!abstract_text.empty()) {
    if (!heading.empty()) {
      out << "## " << heading << "\n\n";
    }
    out << abstract_text << "\n";
    if (!abstract_url.empty()) {
      out << "Source: " << abstract_url << "\n";
    }
    out << "\n";
  }

  // RelatedTopics array contains additional results
  const std::string related = common::json_get_array(body, "RelatedTopics");
  if (!related.empty()) {
    std::size_t pos = 1;
    int count = 0;
    while (pos < related.size() && count < 5) {
      pos = common::json_skip_ws(related, pos);
      if (pos >= related.size() || related[pos] == ']') {
        break;
      }
      if (related[pos] == ',') {
        ++pos;
        continue;
      }
      if (related[pos] == '{') {
        const auto end = common::json_find_matching_token(related, pos, '{', '}');
        if (end == std::string::npos) {
          break;
        }
        const std::string topic = related.substr(pos, end - pos + 1);
        const std::string text = common::json_get_string(topic, "Text");
        const std::string first_url = common::json_get_string(topic, "FirstURL");

        if (!text.empty()) {
          if (!first_url.empty()) {
            out << "- [" << text << "](" << first_url << ")\n";
          } else {
            out << "- " << text << "\n";
          }
          ++count;
        }
        pos = end + 1;
      } else {
        ++pos;
      }
    }
  }

  if (out.str().empty()) {
    out << "No instant answer results found for: " << query << "\n";
    out << "Try searching at: https://duckduckgo.com/?q=" << url_encode(query) << "\n";
  }

  ToolResult result;
  result.output = out.str();
  result.metadata["provider"] = "duckduckgo";
  return common::Result<ToolResult>::success(std::move(result));
}

bool WebSearchTool::is_safe() const { return true; }

std::string_view WebSearchTool::group() const { return "web"; }

} // namespace ghostclaw::tools
