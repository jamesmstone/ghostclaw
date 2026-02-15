#include "ghostclaw/browser/actions.hpp"

#include "ghostclaw/common/fs.hpp"

#include <optional>

namespace ghostclaw::browser {

namespace {

common::Result<BrowserActionResult> ok_result(JsonMap data = {}) {
  BrowserActionResult result;
  result.success = true;
  result.data = std::move(data);
  return common::Result<BrowserActionResult>::success(std::move(result));
}

common::Result<BrowserActionResult> error_result(const std::string &message) {
  return common::Result<BrowserActionResult>::failure(message);
}

} // namespace

BrowserActions::BrowserActions(CDPClient &client) : client_(client) {}

common::Result<BrowserActionResult> BrowserActions::execute(const BrowserAction &action) {
  const std::string name = common::to_lower(common::trim(action.action));
  if (name.empty()) {
    return error_result("action is required");
  }

  if (name == "navigate") {
    return action_navigate(action);
  }
  if (name == "click") {
    return action_click(action);
  }
  if (name == "type") {
    return action_type(action);
  }
  if (name == "fill") {
    return action_fill(action);
  }
  if (name == "press") {
    return action_press(action);
  }
  if (name == "hover") {
    return action_hover(action);
  }
  if (name == "drag") {
    return action_drag(action);
  }
  if (name == "select") {
    return action_select(action);
  }
  if (name == "scroll") {
    return action_scroll(action);
  }
  if (name == "screenshot") {
    return action_screenshot(action);
  }
  if (name == "snapshot") {
    return action_snapshot(action);
  }
  if (name == "pdf") {
    return action_pdf(action);
  }
  if (name == "evaluate") {
    return action_evaluate(action);
  }
  return error_result("unsupported browser action: " + name);
}

common::Result<std::vector<BrowserActionResult>>
BrowserActions::execute_batch(const std::vector<BrowserAction> &actions) {
  if (actions.empty()) {
    return common::Result<std::vector<BrowserActionResult>>::failure(
        "actions list is empty");
  }
  std::vector<BrowserActionResult> out;
  out.reserve(actions.size());
  for (const auto &action : actions) {
    auto result = execute(action);
    if (result.ok()) {
      out.push_back(result.value());
    } else {
      BrowserActionResult failure;
      failure.success = false;
      failure.error = result.error();
      out.push_back(std::move(failure));
    }
  }
  return common::Result<std::vector<BrowserActionResult>>::success(std::move(out));
}

common::Result<BrowserActionResult>
BrowserActions::action_navigate(const BrowserAction &action) {
  const std::string url = param_or_empty(action, "url");
  if (url.empty()) {
    return error_result("navigate requires url");
  }
  auto response = client_.send_command("Page.navigate", {{"url", url}});
  if (!response.ok()) {
    return error_result(response.error());
  }
  JsonMap out = response.value();
  out["url"] = url;
  return ok_result(std::move(out));
}

common::Result<BrowserActionResult>
BrowserActions::action_click(const BrowserAction &action) {
  std::string selector = param_or_empty(action, "selector");
  if (selector.empty()) {
    selector = param_or_empty(action, "ref");
  }
  if (selector.empty()) {
    return error_result("click requires selector");
  }
  const std::string js = "(function(){const el=document.querySelector('" +
                         escape_js_string(selector) +
                         "');if(!el){throw new Error('selector_not_found');}"
                         "el.click();return 'ok';})()";
  auto response = client_.evaluate_js(js);
  if (!response.ok()) {
    return error_result(response.error());
  }
  JsonMap out = response.value();
  out["selector"] = selector;
  return ok_result(std::move(out));
}

common::Result<BrowserActionResult>
BrowserActions::action_type(const BrowserAction &action) {
  const std::string text = param_or_empty(action, "text");
  if (text.empty()) {
    return error_result("type requires text");
  }
  const std::string selector = param_or_empty(action, "selector");
  std::string js;
  if (!selector.empty()) {
    js = "(function(){const el=document.querySelector('" + escape_js_string(selector) +
         "');if(!el){throw new Error('selector_not_found');}el.focus();"
         "el.value=(el.value||'')+'" +
         escape_js_string(text) +
         "';el.dispatchEvent(new Event('input',{bubbles:true}));return 'ok';})()";
  } else {
    js = "(function(){const el=document.activeElement;if(!el){throw new Error('no_active_element');}"
         "el.value=(el.value||'')+'" +
         escape_js_string(text) +
         "';el.dispatchEvent(new Event('input',{bubbles:true}));return 'ok';})()";
  }
  auto response = client_.evaluate_js(js);
  if (!response.ok()) {
    return error_result(response.error());
  }
  JsonMap out = response.value();
  out["text"] = text;
  if (!selector.empty()) {
    out["selector"] = selector;
  }
  return ok_result(std::move(out));
}

common::Result<BrowserActionResult>
BrowserActions::action_fill(const BrowserAction &action) {
  const std::string selector = param_or_empty(action, "selector");
  if (selector.empty()) {
    return error_result("fill requires selector");
  }
  const std::string value = param_or_empty(action, "value").empty()
                                ? param_or_empty(action, "text")
                                : param_or_empty(action, "value");
  std::string js = "(function(){const el=document.querySelector('" +
                   escape_js_string(selector) +
                   "');if(!el){throw new Error('selector_not_found');}"
                   "el.focus();el.value='" +
                   escape_js_string(value) +
                   "';el.dispatchEvent(new Event('input',{bubbles:true}));"
                   "el.dispatchEvent(new Event('change',{bubbles:true}));return 'ok';})()";
  auto response = client_.evaluate_js(js);
  if (!response.ok()) {
    return error_result(response.error());
  }
  JsonMap out = response.value();
  out["selector"] = selector;
  return ok_result(std::move(out));
}

common::Result<BrowserActionResult>
BrowserActions::action_press(const BrowserAction &action) {
  const std::string key = param_or_empty(action, "key");
  if (key.empty()) {
    return error_result("press requires key");
  }
  auto down = client_.send_command("Input.dispatchKeyEvent",
                                   {{"type", "keyDown"}, {"key", key}, {"text", key}});
  if (!down.ok()) {
    return error_result(down.error());
  }
  auto up = client_.send_command("Input.dispatchKeyEvent",
                                 {{"type", "keyUp"}, {"key", key}});
  if (!up.ok()) {
    return error_result(up.error());
  }
  JsonMap out;
  out["key"] = key;
  out["status"] = "ok";
  return ok_result(std::move(out));
}

common::Result<BrowserActionResult>
BrowserActions::action_hover(const BrowserAction &action) {
  const std::string selector = param_or_empty(action, "selector");
  if (selector.empty()) {
    return error_result("hover requires selector");
  }
  const std::string js = "(function(){const el=document.querySelector('" +
                         escape_js_string(selector) +
                         "');if(!el){throw new Error('selector_not_found');}"
                         "el.dispatchEvent(new MouseEvent('mouseover',{bubbles:true}));"
                         "return 'ok';})()";
  auto response = client_.evaluate_js(js);
  if (!response.ok()) {
    return error_result(response.error());
  }
  JsonMap out = response.value();
  out["selector"] = selector;
  return ok_result(std::move(out));
}

common::Result<BrowserActionResult>
BrowserActions::action_drag(const BrowserAction &action) {
  const std::string from = param_or_empty(action, "from");
  const std::string to = param_or_empty(action, "to");
  if (from.empty() || to.empty()) {
    return error_result("drag requires from and to selectors");
  }
  const std::string js = "(function(){const src=document.querySelector('" +
                         escape_js_string(from) + "');const dst=document.querySelector('" +
                         escape_js_string(to) +
                         "');if(!src||!dst){throw new Error('selector_not_found');}"
                         "const dt=new DataTransfer();"
                         "src.dispatchEvent(new DragEvent('dragstart',{dataTransfer:dt,bubbles:true}));"
                         "dst.dispatchEvent(new DragEvent('dragover',{dataTransfer:dt,bubbles:true}));"
                         "dst.dispatchEvent(new DragEvent('drop',{dataTransfer:dt,bubbles:true}));"
                         "src.dispatchEvent(new DragEvent('dragend',{dataTransfer:dt,bubbles:true}));"
                         "return 'ok';})()";
  auto response = client_.evaluate_js(js);
  if (!response.ok()) {
    return error_result(response.error());
  }
  JsonMap out = response.value();
  out["from"] = from;
  out["to"] = to;
  return ok_result(std::move(out));
}

common::Result<BrowserActionResult>
BrowserActions::action_select(const BrowserAction &action) {
  const std::string selector = param_or_empty(action, "selector");
  const std::string value = param_or_empty(action, "value");
  if (selector.empty() || value.empty()) {
    return error_result("select requires selector and value");
  }
  const std::string js = "(function(){const el=document.querySelector('" +
                         escape_js_string(selector) +
                         "');if(!el){throw new Error('selector_not_found');}"
                         "el.value='" +
                         escape_js_string(value) +
                         "';el.dispatchEvent(new Event('change',{bubbles:true}));return 'ok';})()";
  auto response = client_.evaluate_js(js);
  if (!response.ok()) {
    return error_result(response.error());
  }
  JsonMap out = response.value();
  out["selector"] = selector;
  out["value"] = value;
  return ok_result(std::move(out));
}

common::Result<BrowserActionResult>
BrowserActions::action_scroll(const BrowserAction &action) {
  const auto x = parse_double_param(action, "x").value_or(0.0);
  const auto y = parse_double_param(action, "y").value_or(500.0);
  const std::string js = "window.scrollBy(" + std::to_string(x) + "," + std::to_string(y) +
                         ");'ok'";
  auto response = client_.evaluate_js(js);
  if (!response.ok()) {
    return error_result(response.error());
  }
  JsonMap out = response.value();
  out["x"] = std::to_string(x);
  out["y"] = std::to_string(y);
  return ok_result(std::move(out));
}

common::Result<BrowserActionResult>
BrowserActions::action_screenshot(const BrowserAction &action) {
  const std::string format = param_or_empty(action, "format");
  common::Result<std::string> screenshot = format.empty()
                                               ? client_.capture_screenshot()
                                               : common::Result<std::string>::failure("");
  if (!format.empty()) {
    auto response = client_.send_command("Page.captureScreenshot", {{"format", format}});
    if (!response.ok()) {
      return error_result(response.error());
    }
    const auto data_it = response.value().find("data");
    if (data_it == response.value().end()) {
      return error_result("screenshot data missing");
    }
    screenshot = common::Result<std::string>::success(data_it->second);
  }
  if (!screenshot.ok()) {
    return error_result(screenshot.error());
  }
  JsonMap out;
  out["data"] = screenshot.value();
  out["format"] = format.empty() ? "png" : format;
  return ok_result(std::move(out));
}

common::Result<BrowserActionResult>
BrowserActions::action_snapshot(const BrowserAction &) {
  auto response = client_.get_accessibility_tree();
  if (!response.ok()) {
    return error_result(response.error());
  }
  return ok_result(response.value());
}

common::Result<BrowserActionResult>
BrowserActions::action_pdf(const BrowserAction &action) {
  JsonMap params;
  const std::string landscape = param_or_empty(action, "landscape");
  if (!landscape.empty()) {
    params["landscape"] = landscape;
  }
  params["printBackground"] = "true";
  auto response = client_.send_command("Page.printToPDF", params);
  if (!response.ok()) {
    return error_result(response.error());
  }
  const auto data_it = response.value().find("data");
  if (data_it == response.value().end()) {
    return error_result("pdf data missing");
  }
  JsonMap out;
  out["data"] = data_it->second;
  return ok_result(std::move(out));
}

common::Result<BrowserActionResult>
BrowserActions::action_evaluate(const BrowserAction &action) {
  const std::string expression = param_or_empty(action, "expression");
  if (expression.empty()) {
    return error_result("evaluate requires expression");
  }
  auto response = client_.evaluate_js(expression);
  if (!response.ok()) {
    return error_result(response.error());
  }
  return ok_result(response.value());
}

std::string BrowserActions::escape_js_string(const std::string &value) {
  std::string out;
  out.reserve(value.size() + 8);
  for (const char ch : value) {
    switch (ch) {
    case '\'':
      out += "\\'";
      break;
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
      out.push_back(ch);
      break;
    }
  }
  return out;
}

std::string BrowserActions::param_or_empty(const BrowserAction &action,
                                           const std::string &key) {
  const auto it = action.params.find(key);
  if (it == action.params.end()) {
    return "";
  }
  return it->second;
}

std::optional<double> BrowserActions::parse_double_param(const BrowserAction &action,
                                                         const std::string &key) {
  const auto it = action.params.find(key);
  if (it == action.params.end()) {
    return std::nullopt;
  }
  try {
    return std::stod(common::trim(it->second));
  } catch (...) {
    return std::nullopt;
  }
}

} // namespace ghostclaw::browser
