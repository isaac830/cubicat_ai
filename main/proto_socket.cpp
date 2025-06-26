#include "proto_socket.h"
#include "utils/logger.h"
#include <arpa/inet.h>
#include "socket/compress.h"
#include "core/memory_allocator.h"
#include "utils/helper.h"

#define MAX_RECV_BUFF_SIZE 1024 * 32
// data in big endian
#define READ_PROTOCOL_LEN(data) \
        (static_cast<int>(data[0]) << 24) | \
        (static_cast<int>(data[1]) << 16) | \
        (static_cast<int>(data[2]) << 8)  | \
        (static_cast<int>(data[3]));

#define IMPLEMENTSENDMESSAGE(MSG,pre_fix) \
void ProtoSocket::send(const char* method,Rpc__##MSG* msg) { \
    if (!isConnected()) \
        return; \
    Rpc__Request req = RPC__REQUEST__INIT; \
    req.method = (char*)method; \
    req.protoname = (char*)#MSG; \
    auto size = pre_fix##__get_packed_size(msg); \
    uint8_t* msgBuffer = nullptr; \
    if (size > 0) { \
        msgBuffer = (uint8_t*)malloc(size); \
        size = pre_fix##__pack(msg, msgBuffer); \
    } \
    req.serialized_data.len = size; \
    req.serialized_data.data = msgBuffer; \
    auto _size = rpc__request__get_packed_size(&req); \
    uint8_t* reqBuffer = (uint8_t*)malloc(_size); \
    _size = rpc__request__pack(&req, reqBuffer); \
    assert(_size > 0); \
    size_t compressed_len = 0; \
    auto compressed_data = defl(reqBuffer, _size, &compressed_len, Z_BEST_SPEED); \
    if (compressed_data) { \
        int ret = send((const char*)compressed_data, compressed_len, true); \
        free(compressed_data); \
    } else { \
        LOGE("compress error\n"); \
    } \
    if (msgBuffer) \
        free(msgBuffer); \
    free(reqBuffer); \
}
ProtoSocket::ProtoSocket() {
    m_pDataBuffer = (uint8_t*)psram_malloc(MAX_RECV_BUFF_SIZE);
    assert(m_pDataBuffer);
}
ProtoSocket::~ProtoSocket() {
    if (m_pDataBuffer) {
        free(m_pDataBuffer);
    }
}
void ProtoSocket::ping() {
    Rpc__Ping ping = RPC__PING__INIT;
    send("ping", &ping);
}
void ProtoSocket::onDataReceived(uint8_t* data, size_t len) {
    memcpy(m_pDataBuffer + m_nDataLen, data, len);
    m_nDataLen += len;
    assert(m_nDataLen <= MAX_RECV_BUFF_SIZE);
    if (m_nDataLen <= 4) {
        return;
    }
    size_t protocolLen = READ_PROTOCOL_LEN(m_pDataBuffer);
    size_t packetLen = protocolLen + 4;
    while (m_nDataLen >= packetLen) {
        // decompress data
        // jump over 4 bytes
        uint8_t* protocolData = m_pDataBuffer + 4;
        size_t decompressedLen = 0;
        uint8_t* decompressedData = infl(protocolData, protocolLen, &decompressedLen);
        if(!decompressedData) {
            assert(false);
            break;
        }
        Rpc__Request* req = rpc__request__unpack(nullptr, decompressedLen, decompressedData); 
        free(decompressedData);
        if (req) {
            m_timeDiff = req->servertime - timeNow();
            if (m_pListener) {
                ((ProtoSocketListener*)m_pListener)->onRequest(req);
            }
            rpc__request__free_unpacked(req, nullptr);
        }
        m_nDataLen -= packetLen;
        memmove(m_pDataBuffer, m_pDataBuffer + packetLen, m_nDataLen);
        protocolLen = READ_PROTOCOL_LEN(m_pDataBuffer);
        packetLen = protocolLen + 4;
    }
}

//**************** Implement send methods begin ***************
IMPLEMENTSENDMESSAGE(Request, rpc__request)
IMPLEMENTSENDMESSAGE(Login, rpc__login)
IMPLEMENTSENDMESSAGE(Msg, rpc__msg)
IMPLEMENTSENDMESSAGE(Ping, rpc__ping)
IMPLEMENTSENDMESSAGE(BytesMsg, rpc__bytes_msg)
//**************** Implement send methods end   ***************