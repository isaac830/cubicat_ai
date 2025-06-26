/*
* @author       Isaac
* @date         2025-06-09
* @license      MIT License
* @copyright    Copyright (c) 2025 Deer Valley
* @description  MCP server for cubicat
*/
#ifndef _MCP_TOOL_H_
#define _MCP_TOOL_H_
#include <string>
#include "core/shared_pointer.h"
#include <vector>
#include <map>
#include <functional>
#include "cjson/cJSON.h"
#include <any>
#include <variant>

enum ParamType {
    STRING,
    INT,
    FLOAT,
    INT2,
    FLOAT2,
    INT3,
    FLOAT3,
    OBJLIST
};

struct InputParamProto
{
    std::string     name;
    ParamType       type;
    std::string     description;
};
struct InputParam
{
    InputParamProto proto;
    std::any        value;
    template<typename T>
    T cast() const {
        return std::any_cast<T>(value);
    }
};

struct MCPJSONObject;
using ParamValue = std::variant<
    std::vector<MCPJSONObject>,  
    float,                    
    int,            
    unsigned int,      
    unsigned short,
    std::array<float, 1>,      
    std::array<int, 1>,           
    std::array<float, 2>,      
    std::array<int, 2>,        
    std::array<float, 3>,      
    std::array<int, 3>,        
    std::string               
>;
struct OutputParam
{
    std::string     name;
    ParamType       type;
    ParamValue      value;
};
struct MCPJSONObject {
    std::vector<OutputParam>    params;
    template<class T>
    T& addParam(const std::string& name, ParamType type, const T& value) {
        auto& p = params.emplace_back();
        p.name = name;
        p.type = type;
        p.value = value;
        return std::get<T>(p.value);
    }
};


using InputParamMap = std::map<std::string, InputParam>;

using ToolExecutor = std::function<void (const InputParamMap& inParams, MCPJSONObject& jsonObj)>;

class MCPTool
{
public:
    MCPTool(const std::string& name, const std::string& description);
    ~MCPTool() = default;

    MCPTool* addParameter(const std::string& name, ParamType type, const std::string& description);
    MCPTool* setExecutor(ToolExecutor function);
    std::string toJson();
    MCPJSONObject execute(cJSON* jsonParam);
    const std::string& getName() const { return m_name; }
private:

    std::string             m_name;
    std::string             m_description;
    std::vector<InputParamProto>  m_inParams;
    ToolExecutor            m_function;
};
using MCPToolPtr = SharedPtr<MCPTool>;

#endif