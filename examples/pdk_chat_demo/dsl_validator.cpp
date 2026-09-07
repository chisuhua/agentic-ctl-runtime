// dsl_validator.cpp - PDK Chat Demo DSL Schema 校验器实现
// 关联: examples/pdk_chat_demo/dsl_validator.h
//       openspec/changes/pdk-chat-demo-v1-recap/design.md §T2
//       openspec/changes/fix-markdown-parser-yaml/ (yaml fenced block 支持)
// 作者: Sisyphus (OhMyOpenCode), 2026-07-27
// 更新: 2026-07-30 — 接受可选 IToolRegistry*, 启用 MISSING_TOOL_DEPENDENCY 检查
// 更新: 2026-08-04 — fix-markdown-parser-yaml: 双格式自动检测 (bold + yaml fenced)

#include "dsl_validator.h"

#include <nlohmann/json.hpp>
#include <regex>
#include <set>
#include <stdexcept>

#include <yaml-cpp/yaml.h>
#include "common/utils/yaml_json.h"

#include <agenticdsl/contract/itool_registry.h>

namespace pdk_chat_demo {

// ============================================================
// 合法节点类型白名单
// ============================================================
static const std::set<std::string> VALID_NODE_TYPES = {
    "start", "end", "call_tool", "llm_generate", "llm_call", "dsl_call",
    "tool_call", "condition", "fork", "join", "assign", "resource",
    "generate_subgraph", "assert", "yield"
};

// ============================================================
// 必填 frontmatter 字段
// ============================================================
static const std::vector<std::string> REQUIRED_FIELDS = {
    "name", "version", "agent_loop"
};

// ============================================================
// 内部 helper
// ============================================================
std::string DslValidator::extract_frontmatter_value(const std::string& content,
                                                     const std::string& key) {
  // 匹配 **key**: value 格式（Markdown bold syntax）
  std::regex pattern(R"(\*\*)" + key + R"(\*\*:\s*(\S+))");
  std::smatch match;
  if (std::regex_search(content, match, pattern)) {
    return match[1].str();
  }
  return "";
}

std::string DslValidator::extract_nodes_json(const std::string& content) {
  // 找到 ## Nodes 节
  auto nodes_pos = content.find("## Nodes");
  if (nodes_pos == std::string::npos) {
    return "";
  }

  // 在 ## Nodes 之后找 ```json 代码块
  auto after_nodes = content.substr(nodes_pos);
  auto json_start = after_nodes.find("```json");
  if (json_start == std::string::npos) {
    // 尝试 ``` 无语言标识
    json_start = after_nodes.find("```");
    if (json_start == std::string::npos) {
      return "";
    }
  }

  auto code_start = after_nodes.find('\n', json_start);
  if (code_start == std::string::npos) {
    return "";
  }
  code_start += 1;

  auto code_end = after_nodes.find("```", code_start);
  if (code_end == std::string::npos) {
    return "";
  }

  return after_nodes.substr(code_start, code_end - code_start);
}

// ============================================================
// fix-markdown-parser-yaml: yaml fenced block 解析 (双格式支持)
// 扫描 ```yaml 代码块, 首行含 # --- BEGIN AgenticDSL --- 标记
// 返回块内纯 yaml 文本 (不含围栏), 未找到返回空
// ============================================================
static std::string extract_yaml_fenced_block(const std::string& content) {
  static const std::string fence_open = "```yaml";
  static const std::string begin_marker = "# --- BEGIN AgenticDSL ---";
  static const std::string fence_close = "```";

  size_t pos = 0;
  while (pos < content.size()) {
    size_t fence_pos = content.find(fence_open, pos);
    if (fence_pos == std::string::npos) return "";

    // 跳过 fence_open 自身, 定位到下一行开头 (支持 LF 和 CRLF)
    size_t line_start = fence_pos + fence_open.size();
    if (line_start < content.size() && content[line_start] == '\n') {
      line_start += 1;
    } else if (line_start + 1 < content.size() &&
               content[line_start] == '\r' && content[line_start + 1] == '\n') {
      line_start += 2;
    }

    size_t line_end = content.find('\n', line_start);
    if (line_end == std::string::npos) return "";
    std::string first_line = content.substr(line_start, line_end - line_start);
    const size_t first_nonspace = first_line.find_first_not_of(" \t\r");
    const size_t last_nonspace = first_line.find_last_not_of(" \t\r");
    const std::string trimmed =
        (first_nonspace == std::string::npos)
            ? ""
            : first_line.substr(first_nonspace, last_nonspace - first_nonspace + 1);

    if (trimmed == begin_marker) {
      size_t content_start = line_end + 1;
      size_t close_pos = content.find(fence_close, content_start);
      if (close_pos == std::string::npos) return "";
      return content.substr(content_start, close_pos - content_start);
    }
    pos = fence_pos + fence_open.size();
  }
  return "";
}

// ============================================================
// 主校验入口
// ============================================================
ValidationResult DslValidator::validate(const std::string& markdown_content,
                                       const agenticdsl::IToolRegistry* registry) {
  ValidationResult result;

  std::string yaml_block = extract_yaml_fenced_block(markdown_content);
  bool use_yaml_format = !yaml_block.empty();

  nlohmann::json yaml_json;
  bool yaml_json_ok = false;
  std::string yaml_error_path;

  if (use_yaml_format) {
    yaml_json_ok = yaml_block_to_json(yaml_block, yaml_json, yaml_error_path);
    if (!yaml_json_ok) {
      result.add_error("INVALID_YAML", yaml_error_path,
                       "yaml-cpp parse failure");
    }
  }

  for (const auto& field : REQUIRED_FIELDS) {
    bool present = false;
    if (use_yaml_format && yaml_json_ok) {
      present = extract_required_string_field(yaml_json, field);
    } else if (!use_yaml_format) {
      present = !extract_frontmatter_value(markdown_content, field).empty();
    }
    if (!present) {
      result.add_error("MISSING_REQUIRED_FIELD",
                       "frontmatter." + field,
                       "missing required field: " + field);
    }
  }

  nlohmann::json nodes;
  bool nodes_ok = false;

  if (use_yaml_format) {
    if (yaml_json_ok) {
      nodes_ok = extract_nodes_array(yaml_json, nodes);
      if (!nodes_ok) {
        result.add_error(
            "INVALID_YAML", "yaml_block.nodes",
            yaml_json.contains("nodes")
                ? "yaml 'nodes' field must be an array"
                : "missing 'nodes:' list in yaml block");
      }
    }
  } else {
    std::string nodes_json_text = extract_nodes_json(markdown_content);
    if (nodes_json_text.empty()) {
      result.add_error("MISSING_SECTION", "## Nodes",
                       "missing '## Nodes' section or JSON code block");
      return result;
    }
    try {
      nodes = nlohmann::json::parse(nodes_json_text);
    } catch (const nlohmann::json::parse_error& e) {
      result.add_error("PARSE_ERROR", "## Nodes",
                       "invalid JSON in Nodes section: " + std::string(e.what()));
      return result;
    }
    if (!nodes.is_array()) {
      result.add_error("PARSE_ERROR", "## Nodes",
                       "Nodes section must be a JSON array, got: " +
                           std::string(nodes.type_name()));
      return result;
    }
    nodes_ok = true;
  }

  if (!nodes_ok) {
    return result;
  }

  for (size_t i = 0; i < nodes.size(); ++i) {
    const auto& node = nodes[i];
    std::string node_path = "node[" + std::to_string(i) + "]";

    if (!node.contains("id") || !node["id"].is_string() ||
        node["id"].get<std::string>().empty()) {
      result.add_error("MISSING_REQUIRED_FIELD", node_path + ".id",
                       "node missing required field 'id'");
    }
    if (!node.contains("type") || !node["type"].is_string() ||
        node["type"].get<std::string>().empty()) {
      result.add_error("MISSING_REQUIRED_FIELD", node_path + ".type",
                       "node missing required field 'type'");
      continue;
    }

    std::string node_type = node["type"].get<std::string>();
    if (VALID_NODE_TYPES.find(node_type) == VALID_NODE_TYPES.end()) {
      result.add_error("INVALID_NODE_TYPE", node_path + ".type",
                       "unknown node type '" + node_type + "'");
    }

    if (node_type == "call_tool") {
      if (!node.contains("tool_name") || !node["tool_name"].is_string() ||
          node["tool_name"].get<std::string>().empty()) {
        result.add_error("MISSING_REQUIRED_FIELD", node_path + ".tool_name",
                         "call_tool node missing required field 'tool_name'");
      } else if (registry != nullptr &&
                 !registry->has_tool(node["tool_name"].get<std::string>())) {
        result.add_error(
            "MISSING_TOOL_DEPENDENCY", node_path + ".tool_name",
            "call_tool references unregistered tool '" +
                node["tool_name"].get<std::string>() + "'");
      }
    }
  }

  return result;
}

bool DslValidator::yaml_block_to_json(const std::string& yaml_text,
                                      nlohmann::json& out,
                                      std::string& error_path) {
  try {
    YAML::Node node = YAML::Load(yaml_text);
    out = agenticdsl::yaml_to_json(node);
    return true;
  } catch (const YAML::ParserException& e) {
    error_path = "yaml_block[" + std::to_string(e.mark.line) + ":" +
                 std::to_string(e.mark.column) + "]";
    return false;
  }
}

bool DslValidator::extract_required_string_field(const nlohmann::json& obj,
                                                 const std::string& key) {
  if (!obj.is_object()) return false;
  if (!obj.contains(key)) return false;
  const auto& v = obj.at(key);
  if (!v.is_string()) return false;
  return !v.get<std::string>().empty();
}

bool DslValidator::extract_nodes_array(const nlohmann::json& obj,
                                       nlohmann::json& out) {
  if (!obj.is_object()) return false;
  if (!obj.contains("nodes")) return false;
  const auto& nodes = obj.at("nodes");
  if (!nodes.is_array()) return false;
  out = nodes;
  return true;
}

}  // namespace pdk_chat_demo
