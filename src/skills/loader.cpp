#include "ghostclaw/skills/loader.hpp"

#include "ghostclaw/common/fs.hpp"
#include "ghostclaw/common/toml.hpp"

#include <fstream>
#include <sstream>

namespace ghostclaw::skills {

namespace {

struct FrontmatterDoc {
  std::unordered_map<std::string, std::string> values;
  std::unordered_map<std::string, std::vector<std::unordered_map<std::string, std::string>>>
      arrays;
};

struct FrontmatterParseResult {
  FrontmatterDoc doc;
  std::string body;
  bool has_frontmatter = false;
};

common::Result<std::string> read_file(const std::filesystem::path &path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    return common::Result<std::string>::failure("failed to read " + path.string());
  }
  std::stringstream buffer;
  buffer << in.rdbuf();
  return common::Result<std::string>::success(buffer.str());
}

std::string strip_quotes(std::string value) {
  value = common::trim(std::move(value));
  if (value.size() >= 2 &&
      ((value.front() == '"' && value.back() == '"') ||
       (value.front() == '\'' && value.back() == '\''))) {
    return value.substr(1, value.size() - 2);
  }
  return value;
}

std::string strip_hash_comment(const std::string &line) {
  bool in_quotes = false;
  std::string out;
  out.reserve(line.size());
  for (std::size_t i = 0; i < line.size(); ++i) {
    const char ch = line[i];
    if (ch == '"' && (i == 0 || line[i - 1] != '\\')) {
      in_quotes = !in_quotes;
    }
    if (!in_quotes && ch == '#') {
      break;
    }
    out.push_back(ch);
  }
  return out;
}

std::vector<std::string> split_array_elements(const std::string &array_value) {
  std::vector<std::string> out;
  std::string current;
  bool in_quotes = false;

  for (std::size_t i = 0; i < array_value.size(); ++i) {
    const char ch = array_value[i];
    if (ch == '"' && (i == 0 || array_value[i - 1] != '\\')) {
      in_quotes = !in_quotes;
      current.push_back(ch);
      continue;
    }

    if (!in_quotes && ch == ',') {
      const std::string trimmed = common::trim(current);
      if (!trimmed.empty()) {
        out.push_back(strip_quotes(trimmed));
      }
      current.clear();
      continue;
    }

    current.push_back(ch);
  }

  const std::string tail = common::trim(current);
  if (!tail.empty()) {
    out.push_back(strip_quotes(tail));
  }
  return out;
}

std::vector<std::string> parse_string_array(const std::string &value) {
  const std::string trimmed = common::trim(value);
  if (trimmed.size() < 2 || trimmed.front() != '[' || trimmed.back() != ']') {
    return {};
  }
  return split_array_elements(trimmed.substr(1, trimmed.size() - 2));
}

FrontmatterDoc parse_toml_frontmatter(const std::string &frontmatter) {
  FrontmatterDoc doc;

  std::istringstream stream(frontmatter);
  std::string line;
  std::string current_section;
  std::string current_array;

  while (std::getline(stream, line)) {
    const std::string clean = common::trim(strip_hash_comment(line));
    if (clean.empty()) {
      continue;
    }

    if (clean.size() >= 4 && clean.rfind("[[", 0) == 0 && clean.substr(clean.size() - 2) == "]]") {
      current_array = common::trim(clean.substr(2, clean.size() - 4));
      current_section.clear();
      doc.arrays[current_array].push_back({});
      continue;
    }

    if (clean.size() >= 2 && clean.front() == '[' && clean.back() == ']') {
      current_section = common::trim(clean.substr(1, clean.size() - 2));
      current_array.clear();
      continue;
    }

    const auto equals = clean.find('=');
    if (equals == std::string::npos) {
      continue;
    }

    const std::string key = common::trim(clean.substr(0, equals));
    const std::string raw_value = common::trim(clean.substr(equals + 1));
    if (key.empty()) {
      continue;
    }

    if (!current_array.empty()) {
      auto &rows = doc.arrays[current_array];
      if (rows.empty()) {
        rows.push_back({});
      }
      rows.back()[key] = strip_quotes(raw_value);
      continue;
    }

    const std::string full_key =
        current_section.empty() ? key : (current_section + "." + key);
    doc.values[full_key] = strip_quotes(raw_value);
  }

  return doc;
}

FrontmatterDoc parse_yaml_frontmatter(const std::string &frontmatter) {
  FrontmatterDoc doc;

  std::istringstream stream(frontmatter);
  std::string line;
  std::string pending_array_key;
  std::string pending_block_key;
  int brace_depth = 0;

  auto update_brace_depth = [](const std::string &value) {
    int depth = 0;
    bool in_quotes = false;
    for (std::size_t i = 0; i < value.size(); ++i) {
      const char ch = value[i];
      if (ch == '"' && (i == 0 || value[i - 1] != '\\')) {
        in_quotes = !in_quotes;
        continue;
      }
      if (in_quotes) {
        continue;
      }
      if (ch == '{') {
        ++depth;
      } else if (ch == '}') {
        --depth;
      }
    }
    return depth;
  };

  while (std::getline(stream, line)) {
    if (!pending_block_key.empty()) {
      const std::string trimmed_block = common::trim(line);
      if (!trimmed_block.empty()) {
        if (!doc.values[pending_block_key].empty()) {
          doc.values[pending_block_key] += " ";
        }
        doc.values[pending_block_key] += trimmed_block;
        brace_depth += update_brace_depth(trimmed_block);
      }
      if (brace_depth <= 0 && !trimmed_block.empty()) {
        pending_block_key.clear();
      }
      continue;
    }

    const std::string trimmed = common::trim(line);
    if (trimmed.empty()) {
      continue;
    }

    if (trimmed.rfind("- ", 0) == 0 && !pending_array_key.empty()) {
      const std::string current = doc.values[pending_array_key];
      std::vector<std::string> values;
      if (!current.empty()) {
        values = parse_string_array(current);
      }
      values.push_back(strip_quotes(common::trim(trimmed.substr(2))));

      std::ostringstream out;
      out << '[';
      for (std::size_t i = 0; i < values.size(); ++i) {
        if (i > 0) {
          out << ',';
        }
        out << '"' << values[i] << '"';
      }
      out << ']';
      doc.values[pending_array_key] = out.str();
      continue;
    }

    const auto colon = trimmed.find(':');
    if (colon == std::string::npos) {
      continue;
    }

    const std::string key = common::trim(trimmed.substr(0, colon));
    std::string value = common::trim(trimmed.substr(colon + 1));
    if (key.empty()) {
      continue;
    }

    if (value.empty()) {
      pending_array_key = key;
      if (key == "metadata") {
        pending_block_key = key;
        brace_depth = 0;
      }
      continue;
    }

    pending_array_key.clear();
    doc.values[key] = strip_quotes(value);
    if (!value.empty() && value.front() == '{') {
      brace_depth = update_brace_depth(value);
      if (brace_depth > 0) {
        pending_block_key = key;
      }
    }
  }

  return doc;
}

FrontmatterParseResult parse_frontmatter_block(const std::string &content) {
  FrontmatterParseResult result;
  result.body = content;

  auto parse_marker = [&](const std::string &marker, const bool toml_mode) {
    if (!common::starts_with(content, marker + "\n")) {
      return false;
    }

    const std::string end_marker = "\n" + marker + "\n";
    const std::size_t end = content.find(end_marker, marker.size() + 1);
    if (end == std::string::npos) {
      return false;
    }

    const std::string front = content.substr(marker.size() + 1, end - marker.size() - 1);
    result.doc = toml_mode ? parse_toml_frontmatter(front) : parse_yaml_frontmatter(front);
    result.body = content.substr(end + end_marker.size());
    result.has_frontmatter = true;
    return true;
  };

  if (parse_marker("+++", true)) {
    return result;
  }
  (void)parse_marker("---", false);
  return result;
}

std::string lookup_value(const FrontmatterDoc &doc, const std::string &key) {
  const auto it = doc.values.find(key);
  if (it == doc.values.end()) {
    return "";
  }
  return strip_quotes(it->second);
}

std::vector<SkillTool> parse_tool_specs(const FrontmatterDoc &doc) {
  std::vector<SkillTool> out;
  const auto consume_rows = [&](const std::string &key) {
    const auto it = doc.arrays.find(key);
    if (it == doc.arrays.end()) {
      return;
    }

    for (const auto &row : it->second) {
      SkillTool tool;
      const auto name_it = row.find("name");
      if (name_it != row.end()) {
        tool.name = strip_quotes(name_it->second);
      }
      const auto description_it = row.find("description");
      if (description_it != row.end()) {
        tool.description = strip_quotes(description_it->second);
      }
      const auto kind_it = row.find("kind");
      if (kind_it != row.end()) {
        tool.kind = strip_quotes(kind_it->second);
      }
      const auto command_it = row.find("command");
      if (command_it != row.end()) {
        tool.command = strip_quotes(command_it->second);
      }
      const auto args_it = row.find("args");
      if (args_it != row.end()) {
        tool.args = parse_string_array(args_it->second);
      }
      for (const auto &[field, value] : row) {
        if (field.rfind("env.", 0) == 0 && field.size() > 4) {
          tool.env[field.substr(4)] = strip_quotes(value);
        }
      }

      if (!common::trim(tool.name).empty()) {
        out.push_back(std::move(tool));
      }
    }
  };

  consume_rows("tools");
  consume_rows("tool");
  consume_rows("metadata.tools");
  return out;
}

std::vector<SkillInstallSpec> parse_install_specs(const FrontmatterDoc &doc) {
  std::vector<SkillInstallSpec> out;

  const auto consume_rows = [&](const std::string &key) {
    const auto it = doc.arrays.find(key);
    if (it == doc.arrays.end()) {
      return;
    }

    for (const auto &row : it->second) {
      SkillInstallSpec spec;
      const auto id_it = row.find("id");
      if (id_it != row.end()) {
        spec.id = strip_quotes(id_it->second);
      }
      const auto kind_it = row.find("kind");
      if (kind_it != row.end()) {
        spec.kind = strip_quotes(kind_it->second);
      }
      const auto label_it = row.find("label");
      if (label_it != row.end()) {
        spec.label = strip_quotes(label_it->second);
      }
      const auto formula_it = row.find("formula");
      if (formula_it != row.end()) {
        spec.formula = strip_quotes(formula_it->second);
      }
      const auto package_it = row.find("package");
      if (package_it != row.end()) {
        spec.package = strip_quotes(package_it->second);
      }
      const auto module_it = row.find("module");
      if (module_it != row.end()) {
        spec.module = strip_quotes(module_it->second);
      }
      const auto version_it = row.find("version");
      if (version_it != row.end()) {
        spec.version = strip_quotes(version_it->second);
      }
      const auto url_it = row.find("url");
      if (url_it != row.end()) {
        spec.url = strip_quotes(url_it->second);
      }
      const auto target_it = row.find("target_dir");
      if (target_it != row.end()) {
        spec.target_dir = strip_quotes(target_it->second);
      }
      const auto bins_it = row.find("bins");
      if (bins_it != row.end()) {
        spec.bins = parse_string_array(bins_it->second);
      }
      const auto os_it = row.find("os");
      if (os_it != row.end()) {
        spec.os = parse_string_array(os_it->second);
      }

      if (!common::trim(spec.kind).empty()) {
        if (spec.id.empty()) {
          spec.id = spec.kind;
        }
        out.push_back(std::move(spec));
      }
    }
  };

  consume_rows("install");
  consume_rows("installs");
  consume_rows("metadata.openclaw.install");
  return out;
}

std::string infer_description_from_body(const std::string &body) {
  std::istringstream stream(body);
  std::string line;
  while (std::getline(stream, line)) {
    std::string trimmed = common::trim(line);
    if (trimmed.empty()) {
      continue;
    }
    if (trimmed.rfind("#", 0) == 0) {
      continue;
    }
    if (trimmed.size() > 180) {
      trimmed.resize(180);
      trimmed += "...";
    }
    return trimmed;
  }
  return "";
}

void merge_skill_values(Skill &target, const Skill &incoming) {
  if (target.description.empty()) {
    target.description = incoming.description;
  }
  if (target.version.empty()) {
    target.version = incoming.version;
  }
  if (!target.author.has_value() && incoming.author.has_value()) {
    target.author = incoming.author;
  }
  if (target.tags.empty()) {
    target.tags = incoming.tags;
  }
  if (target.instructions_markdown.empty()) {
    target.instructions_markdown = incoming.instructions_markdown;
  }
  if (target.prompts.empty()) {
    target.prompts = incoming.prompts;
  }

  if (target.tools.empty() && !incoming.tools.empty()) {
    target.tools = incoming.tools;
  }
  if (target.install_specs.empty() && !incoming.install_specs.empty()) {
    target.install_specs = incoming.install_specs;
  }

  for (const auto &[k, v] : incoming.metadata) {
    if (!target.metadata.contains(k)) {
      target.metadata[k] = v;
    }
  }
}

void parse_toml_tool_sections(const common::TomlDocument &doc,
                              std::vector<SkillTool> *out_tools,
                              std::vector<SkillInstallSpec> *out_install) {
  if (out_tools == nullptr || out_install == nullptr) {
    return;
  }

  std::unordered_map<std::string, SkillTool> tools_by_name;
  std::unordered_map<std::string, SkillInstallSpec> install_by_id;

  for (const auto &[key, value] : doc.values) {
    if (key.rfind("tools.", 0) == 0) {
      const std::string rest = key.substr(6);
      const auto dot = rest.find('.');
      if (dot == std::string::npos) {
        continue;
      }
      const std::string tool_name = rest.substr(0, dot);
      const std::string field = rest.substr(dot + 1);
      auto &tool = tools_by_name[tool_name];
      if (tool.name.empty()) {
        tool.name = tool_name;
      }

      if (field == "description") {
        tool.description = strip_quotes(value);
      } else if (field == "kind") {
        tool.kind = strip_quotes(value);
      } else if (field == "command") {
        tool.command = strip_quotes(value);
      } else if (field == "args") {
        tool.args = parse_string_array(value);
      } else if (field.rfind("env.", 0) == 0 && field.size() > 4) {
        tool.env[field.substr(4)] = strip_quotes(value);
      }
      continue;
    }

    if (key.rfind("install.", 0) == 0) {
      const std::string rest = key.substr(8);
      const auto dot = rest.find('.');
      if (dot == std::string::npos) {
        continue;
      }
      const std::string install_id = rest.substr(0, dot);
      const std::string field = rest.substr(dot + 1);
      auto &spec = install_by_id[install_id];
      if (spec.id.empty()) {
        spec.id = install_id;
      }

      if (field == "kind") {
        spec.kind = strip_quotes(value);
      } else if (field == "label") {
        spec.label = strip_quotes(value);
      } else if (field == "formula") {
        spec.formula = strip_quotes(value);
      } else if (field == "package") {
        spec.package = strip_quotes(value);
      } else if (field == "module") {
        spec.module = strip_quotes(value);
      } else if (field == "version") {
        spec.version = strip_quotes(value);
      } else if (field == "url") {
        spec.url = strip_quotes(value);
      } else if (field == "target_dir") {
        spec.target_dir = strip_quotes(value);
      } else if (field == "bins") {
        spec.bins = parse_string_array(value);
      } else if (field == "os") {
        spec.os = parse_string_array(value);
      }
    }
  }

  for (auto &[name, tool] : tools_by_name) {
    (void)name;
    out_tools->push_back(std::move(tool));
  }
  for (auto &[id, spec] : install_by_id) {
    (void)id;
    if (!common::trim(spec.kind).empty()) {
      out_install->push_back(std::move(spec));
    }
  }
}

} // namespace

std::string SkillLoader::extract_markdown_instructions(const std::string &markdown) {
  const std::string trimmed = common::trim(markdown);
  if (trimmed.empty()) {
    return "";
  }

  std::istringstream stream(trimmed);
  std::string line;
  bool in_instructions = false;
  std::ostringstream out;

  while (std::getline(stream, line)) {
    const std::string header = common::trim(line);
    if (header.size() > 3 && header.rfind("## ", 0) == 0) {
      const std::string section = common::to_lower(common::trim(header.substr(3)));
      if (!in_instructions &&
          (section == "instructions" || section == "usage" || section == "prompt")) {
        in_instructions = true;
        continue;
      }
      if (in_instructions) {
        break;
      }
    }

    if (in_instructions) {
      out << line << "\n";
    }
  }

  const std::string extracted = common::trim(out.str());
  return extracted.empty() ? trimmed : extracted;
}

common::Result<Skill> SkillLoader::load_skill_md(const std::filesystem::path &path,
                                                  const SkillLoadOptions &options) {
  std::filesystem::path skill_dir = path;
  std::filesystem::path md_path = path;
  if (std::filesystem::is_directory(path)) {
    skill_dir = path;
    md_path = path / "SKILL.md";
  }
  if (!std::filesystem::exists(md_path)) {
    return common::Result<Skill>::failure("SKILL.md not found");
  }

  auto content = read_file(md_path);
  if (!content.ok()) {
    return common::Result<Skill>::failure(content.error());
  }

  const FrontmatterParseResult parsed = parse_frontmatter_block(content.value());

  Skill skill;
  skill.location = skill_dir;
  skill.readme_path = md_path;
  skill.source = options.source;
  skill.name = lookup_value(parsed.doc, "name");
  if (skill.name.empty()) {
    skill.name = skill_dir.filename().string();
  }
  skill.description = lookup_value(parsed.doc, "description");
  if (skill.description == "|" || skill.description == ">") {
    skill.description.clear();
  }
  if (common::trim(skill.description).empty()) {
    skill.description = infer_description_from_body(parsed.body);
  }
  skill.version = lookup_value(parsed.doc, "version");
  if (skill.version.empty()) {
    skill.version = "1.0.0";
  }

  const std::string author = lookup_value(parsed.doc, "author");
  if (!author.empty()) {
    skill.author = author;
  }

  skill.tags = parse_string_array(lookup_value(parsed.doc, "tags"));
  skill.tools = parse_tool_specs(parsed.doc);
  skill.install_specs = parse_install_specs(parsed.doc);

  for (const auto &[key, value] : parsed.doc.values) {
    if (key.rfind("metadata.", 0) == 0) {
      skill.metadata[key.substr(9)] = value;
    }
  }

  skill.instructions_markdown = extract_markdown_instructions(parsed.body);
  if (!skill.instructions_markdown.empty()) {
    skill.prompts.push_back(skill.instructions_markdown);
  }

  if (common::trim(skill.name).empty()) {
    return common::Result<Skill>::failure("skill frontmatter missing name");
  }
  if (options.require_description && common::trim(skill.description).empty()) {
    return common::Result<Skill>::failure("skill frontmatter missing description");
  }

  return common::Result<Skill>::success(std::move(skill));
}

common::Result<Skill> SkillLoader::load_skill_toml(const std::filesystem::path &path,
                                                    const SkillLoadOptions &options) {
  std::filesystem::path skill_dir = path;
  std::filesystem::path toml_path = path;
  if (std::filesystem::is_directory(path)) {
    skill_dir = path;
    toml_path = path / "SKILL.toml";
  }
  if (!std::filesystem::exists(toml_path)) {
    return common::Result<Skill>::failure("SKILL.toml not found");
  }

  auto content = read_file(toml_path);
  if (!content.ok()) {
    return common::Result<Skill>::failure(content.error());
  }
  auto parsed = common::parse_toml(content.value());
  if (!parsed.ok()) {
    return common::Result<Skill>::failure(parsed.error());
  }

  Skill skill;
  skill.location = skill_dir;
  skill.source = options.source;
  skill.name = parsed.value().get_string("name");
  skill.description = parsed.value().get_string("description");
  skill.version = parsed.value().get_string("version", "1.0.0");
  const std::string author = parsed.value().get_string("author");
  if (!author.empty()) {
    skill.author = author;
  }
  skill.tags = parsed.value().get_string_array("tags");

  parse_toml_tool_sections(parsed.value(), &skill.tools, &skill.install_specs);

  for (const auto &[key, value] : parsed.value().values) {
    if (key.rfind("metadata.", 0) == 0) {
      skill.metadata[key.substr(9)] = strip_quotes(value);
    }
  }

  const auto prompt_file = skill_dir / "SKILL.md";
  if (std::filesystem::exists(prompt_file)) {
    auto md_loaded = load_skill_md(skill_dir, options);
    if (md_loaded.ok()) {
      merge_skill_values(skill, md_loaded.value());
      skill.readme_path = prompt_file;
    }
  }

  if (common::trim(skill.name).empty()) {
    return common::Result<Skill>::failure("skill manifest missing name");
  }
  if (options.require_description && common::trim(skill.description).empty()) {
    return common::Result<Skill>::failure("skill manifest missing description");
  }

  return common::Result<Skill>::success(std::move(skill));
}

common::Result<Skill> SkillLoader::load_skill(const std::filesystem::path &skill_dir,
                                               const SkillLoadOptions &options) {
  const auto toml = skill_dir / "SKILL.toml";
  if (std::filesystem::exists(toml)) {
    return load_skill_toml(skill_dir, options);
  }
  return load_skill_md(skill_dir, options);
}

} // namespace ghostclaw::skills
