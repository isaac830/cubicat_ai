#include "tcpsocket.h"
#include "lwip/def.h"
#include "lwip/netdb.h"
#include "lwip/sockets.h"
#include "lwip/ip_addr.h"
#include "lwip/init.h"
#include "lwip/dns.h"
#include <arpa/inet.h>
#include "utils/logger.h"
#include "core/memory_allocator.h"
#include "utils/helper.h"
#include "tcp_pcb_client.h"

#define USE_TCP_PCB_CLIENT 0


bool getHostIp(const char* host, in_addr* ip, int *count) {
    struct addrinfo hints, *res, *p;
    // clear hints struct
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_INET;     // IPv4 
    hints.ai_socktype = SOCK_STREAM; // TCP
    int status = getaddrinfo(host, NULL, &hints, &res);
    if (status != 0) {
        LOGW("getaddrinfo error: %d\n", (status));
        return false;
    } else {
        // traverse all address structures
        int c = 0;
        for (p = res; p != nullptr; p = p->ai_next) {
            // IPv4 ignore IPv6
            if (p->ai_family == AF_INET)
            { 
                struct sockaddr_in *ipv4 = reinterpret_cast<struct sockaddr_in *>(p->ai_addr);
                *ip++ = ipv4->sin_addr;
                c++;
                char ipstr[INET6_ADDRSTRLEN];
                inet_ntop(AF_INET, &(ipv4->sin_addr), ipstr, sizeof ipstr);
                LOGI("ip: %s\n", ipstr);
            }
        }
        if (count) {
            *count = c;
        }
        return c > 0;
    }
}

TcpSocket::TcpSocket() {
}

TcpSocket::~TcpSocket() {
    if (m_pRecvBuffer) {
        free(m_pRecvBuffer);
        m_pRecvBuffer = nullptr;
    }
    if (m_pSendBuffer) {
        free(m_pSendBuffer);
        m_pSendBuffer = nullptr;
    }
}

void TcpSocket::onConnected() {
        setState(CONNECTED);
        if (m_pListener) {
            m_pListener->onConnected(this);
        }
}
#if USE_TCP_PCB_CLIENT
void tcp_recv_data(void* arg, const void *data, size_t len) {
    ((TcpSocket*)arg)->onDataReceived((uint8_t*)data, len);
}

void TcpSocket::connect(const char* host, int port, void* arg) {
    if (m_eState == DISCONNECTED) {
        tcp_client_connect(host, port, this);
    }
}
err_t tcp_connected_cb(void *arg, struct tcp_pcb *tpcb, err_t err) {
    ((TcpSocket*)arg)->onConnected();
    return ERR_OK;
}
#else
void TcpSocket::connect(const char* host, int port, void* arg) {
    if (m_eState == DISCONNECTED) {
        sa_family_t family = AF_INET;
        m_socket = socket(family, SOCK_STREAM, 0);
        if (m_socket == -1) {
            LOGE("failed to create socket!!\n");
            return;
        }
        // disable nagle
        int enable = 1; 
        setsockopt(m_socket, IPPROTO_TCP, TCP_NODELAY, &enable, sizeof(int));
        m_host = host;
        m_port = port;
        setState(CONNECTING);
        sockaddr_in serverAddress{
            .sin_family = family,
            .sin_port = htons(port),
        };
        if (inet_pton(family, host, &(serverAddress.sin_addr)) <= 0) {
            LOGI("host name: %s\n", host);
            in_addr ip[10];
            if (getHostIp(host, ip, nullptr)) {
                serverAddress.sin_addr = ip[0];
            } else {
                printf("Failed to get host ip\n");
                vTaskDelay(500 / portTICK_PERIOD_MS);
                reconnect();
                return;
            }
        }
        // 连接到服务器
        std::unique_lock<std::mutex> lock(m_socketMutex);
        LOGI("Connecting to server: %s:%d\n", host, port);
        int err = ::connect(m_socket, (struct sockaddr *)&serverAddress, sizeof(serverAddress));
        lock.unlock();
        if (err < 0) {
            LOGE("Connect error: %d\n", err);
            perror("Failed to connect to server");
            vTaskDelay(500 / portTICK_PERIOD_MS);
            reconnect();
            return;
        }
        LOGI("Server connected!");
        createRecvThread();
        onConnected();
    }
}

void TcpSocket::connectAsync(const char* host, int port, void* arg) {
    // TODO implement
}
#endif

void TcpSocket::reconnect() {
    disconnect();
    connect(m_host.c_str(), m_port);
}

void printHex(const void* ptr, size_t size) {
    const uint8_t* bytes = static_cast<const uint8_t*>(ptr);
    for (size_t i = 0; i < size; i++) {
        printf("%02x ", bytes[i]);
        if ((i + 1) % 16 == 0) printf("\n"); // 每16字节换行
    }
    printf("\n\n");
}

int TcpSocket::send(const char* data, unsigned int len, uint8_t _useless) {
    // protocal structure: length | data
    //                     4bytes | variant bytes
    std::lock_guard<std::mutex> lock(m_socketMutex);
    int packetLen = len + 4;
    bufferResize(packetLen); 
    int bigEndianSize = htonl(len);
    memcpy(m_pSendBuffer, &bigEndianSize, 4);
    memcpy(m_pSendBuffer+4, data, len);
#if USE_TCP_PCB_CLIENT
    auto ret = tcp_client_send(m_pSendBuffer, packetLen);
#else
    auto ret = ::send(m_socket, m_pSendBuffer, packetLen, 0);
#endif
    if (ret < 0) {
#if !USE_TCP_PCB_CLIENT
        if (errno == EAGAIN)
            LOGE("send error: EAGAIN\n");
#endif
        LOGE("send error: %d\n", ret);
    }
    return ret;
}

void TcpSocket::disconnect() {
    std::lock_guard<std::mutex> lock(m_socketMutex);
#if USE_TCP_PCB_CLIENT
    tcp_client_disconnect();
#else
    close(m_socket);
#endif
    m_recvHandler = nullptr;
    onDisconnected();
}
void TcpSocket::onDisconnected() {
    LOGW("TcpSocket disconnected");
    setState(DISCONNECTED);
    if (m_pListener) {
        m_pListener->onDisconnected();
    }
}

bool TcpSocket::isConnected() {
    return m_eState == CONNECTED;
}
void TcpSocket::bufferResize(uint32_t size) {
    if (size > m_nSendBufferSize) {
        if (m_pSendBuffer)
            m_pSendBuffer = (uint8_t*)psram_prefered_realloc(m_pSendBuffer, size);
        else
            m_pSendBuffer = (uint8_t*)psram_prefered_malloc(size);
        m_nSendBufferSize = size;
    }
}
void TcpSocket::createRecvThread() {
    if (!m_pRecvBuffer) {
        m_pRecvBuffer = (uint8_t*)psram_prefered_malloc(m_nRecvBufferSize);
    }
    xTaskCreatePinnedToCore([](void* arg){
        TcpSocket* socket = (TcpSocket*)arg;
        while (true)
        {
            if (socket->recvData()) {
                break;
            }
        }
        vTaskDelay(1);
        vTaskDelete(nullptr);
    },"tcp recv task",1024 * 16,this,0,&m_recvHandler,getSubCoreId());
}

bool TcpSocket::recvData() {
    fd_set readfds;
    // clear readfds
    FD_ZERO(&readfds);
    FD_SET(m_socket, &readfds);
    // set timeout 10ms
    struct timeval timeout;
    timeout.tv_sec = 0;      
    timeout.tv_usec = 10*1000;  
    // 调用 select
    int activity = select(m_socket + 1, &readfds, NULL, NULL, &timeout);
    if (activity < 0) {
        LOGI("socket: %d select error: %s", m_socket,strerror(errno));
        vTaskDelay(1000 / portTICK_PERIOD_MS);
        return true;
    } else if (activity == 0) {
        return false; 
    }
    // Check if socket is readable
    if (FD_ISSET(m_socket, &readfds)) {
        size_t bytesRead = 0;
        {
            std::lock_guard<std::mutex> lock(m_socketMutex);
            bytesRead = recv(m_socket, m_pRecvBuffer, m_nRecvBufferSize, 0);
        }
        if (bytesRead == -1) {
            disconnect();
            return true;
        } else if (bytesRead == 0) {
            // Connection closed
            LOGW("Connection closed by the server.\n");
            disconnect();
            return true;
        } else {
            onDataReceived(m_pRecvBuffer, bytesRead);
        }
    }
    return false;
}
