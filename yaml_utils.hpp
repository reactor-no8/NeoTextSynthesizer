#pragma once
#include <nlohmann/json.hpp>
#include <yaml-cpp/yaml.h>
#include <string>

namespace yaml_utils {

// Convert a YAML::Node to nlohmann::json recursively
nlohmann::json yamlNodeToJson(const YAML::Node &node);

// Load a YAML file and return as nlohmann::json
nlohmann::json loadYamlAsJson(const std::string &path);

// Parse a YAML string and return as nlohmann::json
nlohmann::json yamlStringToJson(const std::string &yamlStr);

// Convert nlohmann::json to YAML::Node recursively
YAML::Node jsonToYamlNode(const nlohmann::json &j);

// Save nlohmann::json as a YAML file
void saveJsonAsYaml(const nlohmann::json &j, const std::string &path);

// Detect file type by extension and load as nlohmann::json
// Supports .json, .yaml, .yml
nlohmann::json loadConfigFile(const std::string &path);

} // namespace yaml_utils