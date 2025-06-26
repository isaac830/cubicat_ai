
/*
* @author       Isaac
* @date         2025-06-09
* @license      MIT License
* @copyright    Copyright (c) 2025 Deer Valley
* @description  MCP server for cubicat
*/
#ifndef _MCP_SERVER_H_
#define _MCP_SERVER_H_  
#include <string>
#include "cubicat.h"
#include <functional>
#include <list>
#include <unordered_map>
#include "mcp_tool.h"
#include "proto_socket.h"

using namespace cubicat;

class MCPServer
{
public:
    MCPServer();
    ~MCPServer();

    MCPToolPtr createTool(const std::string& name, const std::string& description) {
        auto tool = MCPToolPtr(new MCPTool(name, description));
        m_toolsList.push_back(tool);
        return tool;
    }
    std::string eval(cJSON* call);
    void setSocket(ProtoSocket* pSocket) { m_pSocket = pSocket; }
    // Main thread loop
    void loop();
private:
    SemaphoreHandle_t                   m_taskLock = nullptr;
    EventGroupHandle_t                  m_eventGroup = nullptr;
    std::list<std::function<void()>>    m_mainThreadTasks;

    std::string getToolListJson(const char* id);
    void foregroundTask(std::function<void()> callback);
    MCPToolPtr getTool(const std::string& name);
    void initTools();

    std::vector<NodePtr> getAllObjects();
    void moveObject(uint32_t id, int x, int y);
    void operateAppliance(uint16_t conn_id, uint16_t chr_id, uint32_t op_code, uint32_t value);
    const std::vector<BLEDevice>& searchAppliance();
    void changeRole();
    void roleAction(const char* action);
    void remainder(uint32_t timestamp, std::string content);
    void sendTextChat(const std::string& chat);
    void changeScene(const std::string& sceneName);
    void showClock(bool show);
    ProtoSocket*                        m_pSocket = nullptr;
    // Response buffer
    char*                               m_pBuffer;
    std::vector<MCPToolPtr>             m_toolsList;
    
};


#endif