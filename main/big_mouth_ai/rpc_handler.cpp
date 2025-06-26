#include "big_mouth_ai.h"

#define DEFINE_RPC_HANDLER(proto, pre_fix, funcBody) \
void BigMouthAI::on##proto(Rpc__Request* req) { \
    auto msg = pre_fix##__unpack(nullptr, req->serialized_data.len, req->serialized_data.data); \
    funcBody \
    pre_fix##__free_unpacked(msg, nullptr); \
}

#define REGISTER_RPC_HANDLER(proto) \
    m_rpcHandlers[#proto] = std::bind(&BigMouthAI::on##proto, this, std::placeholders::_1);

void BigMouthAI::registerRpcHandler() {
    REGISTER_RPC_HANDLER(Rpc__LoginResult);
    REGISTER_RPC_HANDLER(Rpc__AssistantConfig);
    REGISTER_RPC_HANDLER(Rpc__Msg);
    REGISTER_RPC_HANDLER(Rpc__BytesMsg);
    REGISTER_RPC_HANDLER(Rpc__Configs);
}

DEFINE_RPC_HANDLER(Rpc__LoginResult, rpc__login_result, {
    
})

DEFINE_RPC_HANDLER(Rpc__Configs, rpc__configs, {

})

DEFINE_RPC_HANDLER(Rpc__AssistantConfig, rpc__assistant_config, {
    auto root = cJSON_Parse(msg->json);
    if (root)
        onServerHello(root);
    else
        printf("json parse error: %s\n", msg->json);
})

DEFINE_RPC_HANDLER(Rpc__Msg, rpc__msg, {
    auto root = cJSON_Parse(msg->text);
    if (root) {
        printf("json root: %s\n",msg->text);
        onJsonData(root);
        cJSON_Delete(root);
    } else {
        printf("json parse error: %s\n", msg->text);
    }
})

DEFINE_RPC_HANDLER(Rpc__BytesMsg, rpc__bytes_msg, {
    onBinaryData((char*)msg->data.data, msg->data.len);
})