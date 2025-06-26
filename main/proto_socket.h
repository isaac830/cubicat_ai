#ifndef _PROTO_SOCKET_H_
#define _PROTO_SOCKET_H_
#include "socket/tcpsocket.h"
#include "rpc/msg.pb-c.h"


#define DECLARESENDMESSAGE(MSG) \
    void send(const char* method, MSG* msg);

class ProtoSocketListener : public SocketListener {
public:
    virtual void onRequest(Rpc__Request*) = 0;
};
        

class ProtoSocket : public TcpSocket {
public:
    ProtoSocket();
    ~ProtoSocket();
    DECLARESENDMESSAGE(Rpc__Request)
    DECLARESENDMESSAGE(Rpc__Login)
    DECLARESENDMESSAGE(Rpc__Msg)
    DECLARESENDMESSAGE(Rpc__Ping)
    DECLARESENDMESSAGE(Rpc__BytesMsg)
public:
    void onDataReceived(uint8_t* data, size_t len) override;
    // 每隔一段时间调用，免得被服务器踢掉
    void ping();
private:
    using TcpSocket::send;
    uint8_t*        m_pDataBuffer = nullptr;
    uint32_t        m_nDataLen = 0;
    int32_t         m_timeDiff = 0;
};

#endif