#include "yaml_utils.hpp"
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <algorithm>

namespace yaml_utils {

// Recursively convert YAML::Node → nlohmann::json.
// Scalar type inference: null → json(nullptr), bool → json(bool),
// int → json(int64), double → json(double), else → json(string).
nlohmann::json yamlNodeToJson(const YAML::Node &node)
{
    switch (node.Type())
    {
    case YAML::NodeType::Null:
        return nullptr;

    case YAML::NodeType::Scalar:
    {
        const std::string &tag = node.Tag();
        std::string val = node.Scalar();

        // Explicit YAML tags take priority
        if (tag == "tag:yaml.org,2002:null" || val == "null" || val == "~" || val.empty())
            return nullptr;

        // Boolean
        if (val == "true" || val == "True" || val == "TRUE")
            return true;
        if (val == "false" || val == "False" || val == "FALSE")
            return false;

        // Try integer
        try
        {
            size_t pos = 0;
            int64_t i = std::stoll(val, &pos);
            if (pos == val.size())
                return i;
        }
        catch (...)
        {
        }

        // Try double
        try
        {
            size_t pos = 0;
            double d = std::stod(val, &pos);
            if (pos == val.size())
                return d;
        }
        catch (...)
        {
        }

        // Fallback: string
        return val;
    }

    case YAML::NodeType::Sequence:
    {
        nlohmann::json arr = nlohmann::json::array();
        for (const auto &child : node)
            arr.push_back(yamlNodeToJson(child));
        return arr;
    }

    case YAML::NodeType::Map:
    {
        nlohmann::json obj = nlohmann::json::object();
        for (const auto &kv : node)
            obj[kv.first.Scalar()] = yamlNodeToJson(kv.second);
        return obj;
    }

    default:
        return nullptr;
    }
}

nlohmann::json loadYamlAsJson(const std::string &path)
{
    YAML::Node root = YAML::LoadFile(path);
    return yamlNodeToJson(root);
}

nlohmann::json yamlStringToJson(const std::string &yamlStr)
{
    YAML::Node root = YAML::Load(yamlStr);
    return yamlNodeToJson(root);
}

// Recursively convert nlohmann::json → YAML::Node
YAML::Node jsonToYamlNode(const nlohmann::json &j)
{
    YAML::Node node;

    if (j.is_null())
    {
        node = YAML::Node(YAML::NodeType::Null);
        return node;
    }

    if (j.is_boolean())
    {
        node = j.get<bool>();
        return node;
    }

    if (j.is_number_integer() || j.is_number_unsigned())
    {
        node = j.get<int64_t>();
        return node;
    }

    if (j.is_number_float())
    {
        node = j.get<double>();
        return node;
    }

    if (j.is_string())
    {
        node = j.get<std::string>();
        return node;
    }

    if (j.is_array())
    {
        for (const auto &elem : j)
            node.push_back(jsonToYamlNode(elem));
        return node;
    }

    if (j.is_object())
    {
        for (auto it = j.begin(); it != j.end(); ++it)
            node[it.key()] = jsonToYamlNode(it.value());
        return node;
    }

    return node;
}

void saveJsonAsYaml(const nlohmann::json &j, const std::string &path)
{
    YAML::Node yamlNode = jsonToYamlNode(j);
    YAML::Emitter emitter;
    emitter << yamlNode;

    std::ofstream out(path);
    if (!out)
        throw std::runtime_error("Cannot open file for writing: " + path);
    out << emitter.c_str() << "\n";
}

static std::string toLower(const std::string &s)
{
    std::string result = s;
    std::transform(result.begin(), result.end(), result.begin(), ::tolower);
    return result;
}

nlohmann::json loadConfigFile(const std::string &path)
{
    std::string lower = toLower(path);

    if (lower.size() >= 5 && lower.substr(lower.size() - 5) == ".yaml")
        return loadYamlAsJson(path);
    if (lower.size() >= 4 && lower.substr(lower.size() - 4) == ".yml")
        return loadYamlAsJson(path);
    if (lower.size() >= 5 && lower.substr(lower.size() - 5) == ".json")
    {
        std::ifstream f(path);
        if (!f)
            throw std::runtime_error("Cannot open config file: " + path);
        nlohmann::json j;
        f >> j;
        return j;
    }

    // Try YAML first, fall back to JSON
    try
    {
        return loadYamlAsJson(path);
    }
    catch (...)
    {
        std::ifstream f(path);
        if (!f)
            throw std::runtime_error("Cannot open config file: " + path);
        nlohmann::json j;
        f >> j;
        return j;
    }
}

}