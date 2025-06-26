#include "mcp_tool.h"

MCPTool::MCPTool(const std::string& name, const std::string& description)
: m_name(name), m_description(description)
{
}

MCPTool* MCPTool::addParameter(const std::string& name, ParamType type, const std::string& description) {
    m_inParams.push_back({ name, type, description });
    return this;
}

MCPTool* MCPTool::setExecutor(ToolExecutor executor) {
    m_function = executor;
    return this;
}

std::string MCPTool::toJson() {
    std::string required;
    for (int i = 0; i < m_inParams.size(); i++) {
        required += "\"" + m_inParams[i].name + "\"";
        if (i < m_inParams.size() - 1) {
            required += ",";
        }
    }
    std::string json = "{"
    "\"name\": \"" + m_name + "\","
    "\"description\": \"" + m_description + "\","
    "\"parameters\": {"
    "\"type\": \"object\","
    "\"properties\": {";
    for (int i = 0; i < m_inParams.size(); i++) {
        json += "\"" + m_inParams[i].name + "\": {"
        "\"type\": \"" + std::to_string(m_inParams[i].type) + "\","
        "\"description\": \"" + m_inParams[i].description + "\"";
        if (i < m_inParams.size() - 1) {
            json += "},";
        } else {
            json += "}";
        }
    }
    json += "},"
    "\"required\": [" + required + "]"
    "}"
    "}";
    return json;
}
bool isInteger(const std::string& s) {
    if (s.empty())
        return false;
    size_t i = 0;
    if (s[0] == '+' || s[0] == '-')
        i++; 
    for (; i < s.size(); i++) {
        if (!isdigit(s[i])) return false;
    }
    return true;
}
InputParam getParamValue(const InputParamProto& proto, cJSON* json) {
    InputParam param;
    param.proto = proto;
    switch (proto.type)
    {
    case ParamType::STRING:
    {
        param.value = (const char*)cJSON_GetObjectItem(json, proto.name.c_str())->valuestring;
    }
        break;
    case ParamType::INT:
    {
        auto obj = cJSON_GetObjectItem(json, proto.name.c_str());
        if (obj->valuestring) {
            if (isInteger(obj->valuestring)) {
                param.value = atoi(obj->valuestring);
                break;
            }
        }
        param.value = obj->valueint;
    }
        break;
    case ParamType::FLOAT:
    {
        param.value = cJSON_GetObjectItem(json, proto.name.c_str())->valuedouble;
    }
        break;
    case ParamType::INT2:
    {
        auto item = cJSON_GetObjectItem(json, proto.name.c_str());
        int x = cJSON_GetArrayItem(item, 0)->valueint;
        int y = cJSON_GetArrayItem(item, 1)->valueint;
        param.value = std::array<int, 2>({ x, y });
    }
        break;
    case ParamType::FLOAT2:
    {
        auto item = cJSON_GetObjectItem(json, proto.name.c_str());
        float x = cJSON_GetArrayItem(item, 0)->valuedouble;
        float y = cJSON_GetArrayItem(item, 1)->valuedouble;
        param.value = std::array<float, 2>({ x, y });
    }
        break;
    case ParamType::INT3:
    {
        auto item = cJSON_GetObjectItem(json, proto.name.c_str());
        int x = cJSON_GetArrayItem(item, 0)->valueint;
        int y = cJSON_GetArrayItem(item, 1)->valueint;
        int z = cJSON_GetArrayItem(item, 2)->valueint;
        param.value = std::array<int, 3>({ x, y, z });
    }
        break;
    case ParamType::FLOAT3:
    {
        auto item = cJSON_GetObjectItem(json, proto.name.c_str());
        float x = cJSON_GetArrayItem(item, 0)->valuedouble;
        float y = cJSON_GetArrayItem(item, 1)->valuedouble;
        float z = cJSON_GetArrayItem(item, 2)->valuedouble;
        param.value = std::array<float, 3>({ x, y, z });
    }
        break;
    default:
        break;
    }
    return param;
}

MCPJSONObject MCPTool::execute(cJSON* jsonParam) {
    InputParamMap inParams;
    for (auto& param : m_inParams) {
        auto item = cJSON_GetObjectItem(jsonParam, param.name.c_str());
        if (!item) {
            printf("Missing parameter: %s\n", param.name.c_str());
            continue;
        }
        inParams[param.name] = getParamValue(param, jsonParam);
    }
    MCPJSONObject outObj;
    m_function(inParams, outObj);
    return outObj;
}