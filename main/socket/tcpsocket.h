#ifndef _TCPSOCKET_H_
#define _TCPSOCKET_H_
#include "socket.h"
#include <mutex>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

class TcpSocket : public Socket
{
public:
    TcpSocket();
    ~TcpSocket();
    void connect(const char* host, int port, void* arg = nullptr) override;
    void connectAsync(const char* host, int port, void* arg = nullptr) override;
    void reconnect() override;
    int send(const char* data, unsigned int len, uint8_t type) override;
    void disconnect() override;
    bool isConnected() override;

    void setSocketListener(SocketListener* listener) { m_pListener = listener; }
    virtual void onDataReceived(uint8_t* data, size_t len) = 0;
    void onConnected();
    // Internal use only
    bool recvData();
protected:
    SocketListener*     m_pListener = nullptr;
private:
    void onDisconnected();
    void setState(ConnectState state) { m_eState = state; }
    void dataReceived(uint8_t* data, size_t len);
    void bufferResize(uint32_t size);
    void createRecvThread();
    int                 m_socket = -7;
    std::string         m_host;
    int                 m_port;
    uint8_t*            m_pSendBuffer = nullptr;
    uint32_t            m_nSendBufferSize = 0;
    uint8_t*            m_pRecvBuffer = nullptr;
    const size_t        m_nRecvBufferSize = 1024 * 4;
    int32_t             m_timeDiff = 0;
    ConnectState        m_eState = DISCONNECTED;
    std::mutex          m_socketMutex;
    TaskHandle_t        m_recvHandler = nullptr;
};



#endif