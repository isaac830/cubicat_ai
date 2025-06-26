/*
* @author       Isaac
* @date         2025-06-09
* @license      MIT License
* @copyright    Copyright (c) 2025 Deer Valley
* @description  MCP server for cubicat
*/
#include "mcp_server.h"
#include "core/memory_allocator.h"
#include "utils/logger.h"
#include "ble_client.h"
#include "ble_service_defines.h"
#include "ble_op_desc.h"
#include "cubicat_spine.h"
#include "esp_timer.h"
#include "../rpc/msg.pb-c.h"

#define BUFF_SIZE 1024 * 4
#define LOAD_TASK_EVENT 1
extern uint32_t g_bgNodeId;
extern uint32_t g_clockNodeId;
int roleIndex = 0;

MCPServer::MCPServer()
{
    m_pBuffer = (char*)psram_prefered_malloc(BUFF_SIZE);
    m_eventGroup = xEventGroupCreate();
    m_taskLock = xSemaphoreCreateBinary();
    xSemaphoreGive(m_taskLock);
    initTools();
}

MCPServer::~MCPServer()
{
    free(m_pBuffer);
    vEventGroupDelete(m_eventGroup);
    vSemaphoreDelete(m_taskLock);
}

void MCPServer::loop() {
    auto bits = xEventGroupWaitBits(m_eventGroup, LOAD_TASK_EVENT, pdTRUE, pdTRUE, 0);
    if (bits & LOAD_TASK_EVENT) {
        xSemaphoreTake(m_taskLock, portMAX_DELAY);
        auto task = m_mainThreadTasks.front();
        m_mainThreadTasks.pop_front();
        xSemaphoreGive(m_taskLock);
        task();
    }
}

void MCPServer::foregroundTask(std::function<void()> callback) {
    xSemaphoreTake(m_taskLock, portMAX_DELAY);
    m_mainThreadTasks.push_back(callback);
    xEventGroupSetBits(m_eventGroup, LOAD_TASK_EVENT);
    xSemaphoreGive(m_taskLock);
}

MCPToolPtr MCPServer::getTool(const std::string& name) {
    for (auto& tool : m_toolsList) {
        if (tool->getName() == name)
            return tool;
    }
    return MCPToolPtr(nullptr);
}

std::string toolListPrefix = R"(
{
    "jsonrpc": "2.0",
    "result": {
        "tools": [
)";

const char* toolListPostfix = R"(
        ]
    },
    "id": "%s",
    "type": "mcp"
})";

std::string MCPServer::getToolListJson(const char* id) {
    std::string toolString = toolListPrefix;
    for (int i=0; i<m_toolsList.size(); i++) {
        auto& tool = m_toolsList[i];
        toolString += tool->toJson();
        if (i < m_toolsList.size() - 1)
            toolString += ",";
    }
    char buff[strlen(toolListPostfix) + 20] = {0};
    sprintf(buff, toolListPostfix, id);
    std::string tail(buff);
    return toolString + tail;
}

void MCPObjectToJson(cJSON* json, MCPJSONObject& obj) {
    for (auto& param : obj.params) {
        if (param.type == ParamType::STRING) {
            cJSON_AddStringToObject(json, param.name.c_str(), std::get<std::string>(param.value).c_str());
        } else if (param.type == ParamType::INT) {
            if (std::holds_alternative<int>(param.value)) {
                cJSON_AddNumberToObject(json, param.name.c_str(), std::get<int>(param.value));
            } else if (std::holds_alternative<unsigned int>(param.value)) {
                cJSON_AddNumberToObject(json, param.name.c_str(), std::get<unsigned int>(param.value));
            } else {
                cJSON_AddNumberToObject(json, param.name.c_str(), std::get<unsigned short>(param.value));
            }
        } else if (param.type == ParamType::FLOAT) {
            cJSON_AddNumberToObject(json, param.name.c_str(), std::get<float>(param.value));
        } else if (param.type == ParamType::INT2) {
            char str[32] = {0};
            auto int2 = std::get<std::array<int, 2>>(param.value);
            sprintf(str, "[%d, %d]", int2[0], int2[1]);
            cJSON_AddStringToObject(json, param.name.c_str(), str);
        } else if (param.type == ParamType::FLOAT2) {
            char str[32] = {0};
            auto float2 = std::get<std::array<float, 2>>(param.value);
            sprintf(str, "[%f, %f]", float2[0], float2[1]);
            cJSON_AddStringToObject(json, param.name.c_str(), str);
        } else if (param.type == ParamType::FLOAT3) {
            char str[32] = {0};
            auto float3 = std::get<std::array<float, 3>>(param.value);
            sprintf(str, "[%f, %f, %f]", float3[0], float3[1], float3[2]);
            cJSON_AddStringToObject(json, param.name.c_str(), str);
        } else if (param.type == ParamType::INT3) {
            char str[32] = {0};
            auto int3 = std::get<std::array<int, 3>>(param.value);
            sprintf(str, "[%d, %d, %d]", int3[0], int3[1], int3[2]);
            cJSON_AddStringToObject(json, param.name.c_str(), str);
        } else if (param.type == ParamType::OBJLIST) {
            auto array = cJSON_AddArrayToObject(json, "objects");
            for (auto& obj : std::get<std::vector<MCPJSONObject>>(param.value)) {
                cJSON* item = cJSON_CreateObject();
                MCPObjectToJson(item, obj);
                cJSON_AddItemToArray(array, item);
            }
        }
    }
}

std::string MCPServer::eval(cJSON* root) {
    auto itemMethod = cJSON_GetObjectItem(root, "method");
    auto itemId =     cJSON_GetObjectItem(root, "id");
    auto itemParam = cJSON_GetObjectItem(root, "params");
    if (!itemMethod || !itemId || ! itemParam) {
        LOGE("MCP 协议属性缺失 !itemMethod || !itemId || ! itemParam\n");
        return "";
    }
    std::string methodName = itemMethod->valuestring;
    if (methodName == "tools/list") {
        return getToolListJson(itemId->valuestring);
    } 

    cJSON* ret = cJSON_CreateObject();
    cJSON_AddStringToObject(ret, "id", itemId->valuestring);
    cJSON_AddStringToObject(ret, "type", "mcp");
    auto result = cJSON_AddObjectToObject(ret, "result");
    auto tool = getTool(methodName);
    if (tool) {
        MCPJSONObject outObj = tool->execute(itemParam);
        MCPObjectToJson(result, outObj);
    }
    auto succ = cJSON_PrintPreallocated(ret, m_pBuffer, BUFF_SIZE, 0);
    cJSON_Delete(ret);
    return succ? m_pBuffer : "";
}

void MCPServer::initTools() {
    createTool("get_all_objects", "获取场景里所有的物体的id,名字和位置信息")
    ->setExecutor([this](const InputParamMap& __unused, MCPJSONObject& outObj) {
        outObj.addParam("screen_size", ParamType::INT2, std::array<int, 2>({ CUBICAT.lcd.width(), CUBICAT.lcd.height()}));
        auto& objs = outObj.addParam("objects", ParamType::OBJLIST, std::vector<MCPJSONObject>());
        for (auto objNode : getAllObjects()) {
            auto& obj = objs.emplace_back();
            obj.addParam("name", ParamType::STRING, objNode->getName());
            obj.addParam("id", ParamType::INT, objNode->getId());
            obj.addParam("pos", ParamType::FLOAT2, std::array<float, 2>({ objNode->getPosition().x, objNode->getPosition().y }));
        }
    });

    createTool("move_object", "通过物体的id来移动位置,如果没有id用get_all_objects来获取")
    ->addParameter("id", ParamType::INT, "物体id")->addParameter("pos", ParamType::INT2, "平面位置信息,格式为[x, y]")
    ->setExecutor([this](const InputParamMap& inParams, MCPJSONObject& outObj) {
        int id = inParams.at("id").cast<int>();
        auto pos = inParams.at("pos").cast<std::array<int, 2>>();
        printf("move object %d to %d, %d\n", id, pos[0], pos[1]);
        moveObject(id, pos[0], pos[1]);
    });

    createTool("role_action", "设置角色动作")->addParameter("action", ParamType::STRING, 
        "角色动作,攻击:ATTACK1,大招绝招:HYPER_SKILL1,待机等待:idle")
        ->setExecutor([this](const InputParamMap& inParams, MCPJSONObject& outObj) {
            auto action = inParams.at("action").cast<const char*>();
            roleAction(action);
    });

    createTool("change_role", "切换角色")->setExecutor([this](const InputParamMap& inParams, MCPJSONObject& outObj) {
        changeRole();
    });

    createTool("search_appliance", "查询附近智能蓝牙设备的名称name,连接conn_id,特征chr_ids,和操作码")
    ->setExecutor([this](const InputParamMap& inParams, MCPJSONObject& outObj) {
        auto& appliance = searchAppliance();
        auto& objList = outObj.addParam("objects", ParamType::OBJLIST, std::vector<MCPJSONObject>());
        for (auto& app : appliance) {
            auto& obj = objList.emplace_back();
            obj.addParam("name", ParamType::STRING, app.name);
            obj.addParam("conn_id", ParamType::INT, app.connHandle);
            auto& servList = obj.addParam("services", ParamType::OBJLIST, std::vector<MCPJSONObject>());
            for (auto& serv : app.services) {
                // 只处理Cubicat ble service
                if (serv.uuid != CUBICAT_SERVICE_UUID) {
                    continue;
                }
                auto& serviceObj = servList.emplace_back();
                auto& chrList = serviceObj.addParam("characteristics", ParamType::OBJLIST, std::vector<MCPJSONObject>());
                for (auto& chr : serv.chrData) {
                    // 只处理Cubicat protocol characteristic
                    if (chr.uuid != CUBICAT_PROTOCOL_CHAR_UUID) {
                        continue;
                    }
                    auto& chrObj = chrList.emplace_back();
                    chrObj.addParam("chr_id", ParamType::INT, chr.uuid);
                    auto& opList = chrObj.addParam("op_codes", ParamType::OBJLIST, std::vector<MCPJSONObject>());
                    for (unsigned int op : chr.opCodes) {
                        auto& opObj = opList.emplace_back();
                        opObj.addParam("op_code", ParamType::INT, op);
                        opObj.addParam("description", ParamType::STRING, OpDescCN[op]);
                    }
                }
            }
        }
    });

    createTool("operate_appliance", "操控家用电器,比如蓝牙灯,蓝牙空调等,如果没有设备id先用search_appliance来获取id信息,禁止瞎编id")
    ->setExecutor([this](const InputParamMap& inParams, MCPJSONObject& outObj) {
        auto conn_id = inParams.at("conn_id").cast<int>();
        auto chr_id = inParams.at("chr_id").cast<int>();
        auto op_code = inParams.at("op_code").cast<int>();
        auto value = inParams.at("value").cast<int>();
        if (conn_id && chr_id && op_code)
            operateAppliance(conn_id, chr_id, op_code, value);
    })->addParameter("conn_id", ParamType::INT, "设备连接id")->addParameter("chr_id", ParamType::INT, "设备特征id")
    ->addParameter("op_code", ParamType::INT, "操作码")->addParameter("value", ParamType::INT, "操作值");
    // remainder 
    createTool("remainder", "定时提醒你做什么事情的工具")->addParameter("time", ParamType::INT, "计时时间,单位秒")
    ->addParameter("content", ParamType::STRING, "提醒内容")
    ->setExecutor([this](const InputParamMap& inParams, MCPJSONObject& outObj) {
        auto time = inParams.at("time").cast<int>();
        auto content = inParams.at("content").cast<const char*>();
        remainder(time, content);
    });
    // 切换场景
    createTool("get_all_scenes", "获取所有场景信息")
    ->setExecutor([this](const InputParamMap& inParams, MCPJSONObject& outObj) {
        std::string sceneNames[] = {"office", "cafe"};
        auto& objList = outObj.addParam("scenes", ParamType::OBJLIST, std::vector<MCPJSONObject>());
        for (auto& sceneName : sceneNames) {
            auto& obj = objList.emplace_back();
            obj.addParam("scene", ParamType::STRING, sceneName);
        }
    });
    createTool("change_scene", "切换场景")->addParameter("scene", ParamType::STRING, "场景名称,必须使用get_all_scenes获取,不可自行随意编造")
    ->setExecutor([this](const InputParamMap& inParams, MCPJSONObject& outObj) {
        auto sceneName = inParams.at("scene").cast<const char*>();
        changeScene(sceneName);
    });
    // 隐藏显示时钟
    createTool("show_clock", "隐藏/显示时钟")->addParameter("show", ParamType::INT, "是否显示时钟,0:不显示,1:显示")
    ->setExecutor([this](const InputParamMap& inParams, MCPJSONObject& outObj) {
        showClock(inParams.at("show").cast<int>() == 1);
    });
}

std::vector<NodePtr> MCPServer::getAllObjects() {
    std::vector<NodePtr> objs;
    auto sceneMgr = CUBICAT.engine.getSceneManager();
    auto girl = sceneMgr->getObjectByName("girl");
    if (girl) {
        objs.push_back(girl);
    }
    return objs;
}
void MCPServer::moveObject(uint32_t id, int x, int y) {
    auto sceneMgr = CUBICAT.engine.getSceneManager();
    auto obj = sceneMgr->getObjectById(id);
    if (obj) {
        obj->cast<Node2D>()->setPosition(x, y);
    }
}

void MCPServer::operateAppliance(uint16_t conn_id, uint16_t chr_id, uint32_t op_code, uint32_t value) {
    printf("BLE 发送协议:  %d %d %ld %ld\n", conn_id, chr_id, op_code, value);
    auto protocol = BLEProtocol(op_code, value);
    CUBICAT.bluetooth.write(conn_id, chr_id, protocol);
}

const std::vector<BLEDevice>& MCPServer::searchAppliance() {
    return CUBICAT.bluetooth.getAllDevices();
}
void MCPServer::roleAction(const char* action) {
    auto sceneMgr = CUBICAT.engine.getSceneManager();
    auto role = sceneMgr->getObjectByName("girl");
    if (role) {
        SpineNode* spine = role.get()->cast<SpineNode>();
        if (strcmp(action, "idle") == 0) {
            spine->setAnimation(0, action, true);
        } else {
            spine->setAnimation(0, action, false);
            spine->addAnimation(0, "idle", true);
        }
    }
}

void MCPServer::changeRole() {
    foregroundTask([]() {
        auto role = CUBICAT.engine.getSceneManager()->getObjectByName("girl")->cast<SpineNode>();
        if (roleIndex % 2 == 0) {
            role->loadWithBinaryFile("/spiffs/seo_yoon/seo_yoon.skel", "/spiffs/seo_yoon/seo_yoon.atlas", 0.3);
            role->setPosition({CUBICAT.lcd.width() / 2.0f, -50});
        } else {
            role->loadWithBinaryFile("/spiffs/c131/c131_00.skel", "/spiffs/c131/c131_00.atlas", 0.135);
            role->setPosition({CUBICAT.lcd.width() / 2.0f, -40});
        }
        role->setAnimation(0, "idle", true);
        role->setZ(-100);
        roleIndex++;
    });
}

struct RemainderParam
{
    std::string         content;
    esp_timer_handle_t  timerHandle;
    MCPServer*          mcpServer;
};


void MCPServer::remainder(uint32_t time, std::string content) {
    const esp_timer_create_args_t args = {
        .callback = [](void* arg) {
            auto param = (RemainderParam*)arg;
            esp_timer_delete(param->timerHandle);
            std::string prompt = "不调用工具,直接提醒我" + param->content + "时间到了,必须加上具体提醒内容";
            param->mcpServer->sendTextChat(prompt);
            printf("remainder: %s\n", prompt.c_str());
            delete param;
        },
        .arg = new RemainderParam(content, nullptr, this),
        .dispatch_method = ESP_TIMER_TASK,
        .name = "remainder",
        .skip_unhandled_events = true
    };
    auto err = esp_timer_create(&args, &((RemainderParam*)args.arg)->timerHandle);
    if (err != ESP_OK) {
        printf("create timer failed : %d\n", err);
        delete ((RemainderParam*)args.arg);
        return;
    }
    err = esp_timer_start_once(((RemainderParam*)args.arg)->timerHandle, time * 1000 * 1000);
    if (err != ESP_OK) {
        printf("start timer failed : %d\n", err);
        delete ((RemainderParam*)args.arg);
        return;
    }
}

void MCPServer::sendTextChat(const std::string& chat) {
    if (!m_pSocket)
        return;
    std::string json = "{\"type\":\"text_chat\",\"text\":\"" + chat + "\"}";
    Rpc__Msg msg = RPC__MSG__INIT;
    msg.text = (char*)json.c_str();
    if (!m_pSocket->isConnected()) {
        m_pSocket->reconnect();
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
    if (m_pSocket->isConnected()) {
        m_pSocket->send("jsonMessage", &msg);
    }
}
void MCPServer::changeScene(const std::string& roomName) {
    foregroundTask([roomName]() {
        auto resMgr = CUBICAT.engine.getResourceManager();
        auto sceneMgr = CUBICAT.engine.getSceneManager();
        std::string texName = "/spiffs/" + roomName + ".png";
        auto sceneTex = resMgr->loadTexture(texName);
        auto sceneNode = sceneMgr->getObjectById(g_bgNodeId);
        sceneNode->getDrawable(0)->getMaterial()->setTexture(sceneTex);
    });
}
void MCPServer::showClock(bool show) {
    foregroundTask([show]() {
        auto sceneMgr = CUBICAT.engine.getSceneManager();
        auto clock = sceneMgr->getObjectById(g_clockNodeId);
        if (clock) {
            clock->setVisible(show);
        }
    });
}
