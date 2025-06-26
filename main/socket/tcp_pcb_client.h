#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "lwip/err.h"
#include "lwip/tcp.h"
#include "esp_log.h"

#define TAG "TCP_CLIENT"

#define RECONNECT_DELAY_MS 5000

typedef struct {
    struct tcp_pcb *pcb;
    bool connected = false;
    bool connecting = false;
    SemaphoreHandle_t mutex = xSemaphoreCreateMutex();
} tcp_client_t;

static tcp_client_t tcp_client;

// TCP回调
extern void tcp_recv_data(void* arg, const void *data, size_t len);
extern err_t tcp_connected_cb(void *arg, struct tcp_pcb *tpcb, err_t err);

static err_t tcp_recv_callback(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err)
{
    if (p == NULL) {
        // 连接关闭
        ESP_LOGI(TAG, "Connection closed by server");
        tcp_client.connected = false;
        return ERR_OK;
    }

    // 处理接收到的数据
    tcp_recv_data(arg, p->payload, p->tot_len);

    // 确认数据已处理
    tcp_recved(tpcb, p->tot_len);
    pbuf_free(p);
    return ERR_OK;
}

// TCP错误回调
static void tcp_err_callback(void *arg, err_t err)
{
    ESP_LOGE(TAG, "TCP error: %d", err);
    tcp_client.connected = false;
}

// TCP发送回调
static err_t tcp_sent_callback(void *arg, struct tcp_pcb *tpcb, u16_t len)
{
    ESP_LOGI(TAG, "Sent %d bytes", len);
    return ERR_OK;
}

// 连接到服务器
static bool tcp_client_connect(const char* host, int port, void* arg)
{
    if (tcp_client.connecting || xSemaphoreTake(tcp_client.mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return false;
    }
    // 创建新的PCB
    if (tcp_client.pcb != NULL) {
        if (tcp_client.connected) {
            xSemaphoreGive(tcp_client.mutex);
            ESP_LOGI(TAG, "Already connected to server");
            return true;
        }
        tcp_abort(tcp_client.pcb);
        tcp_client.pcb = NULL;
    }
    tcp_client.pcb = tcp_new();
    tcp_client.connecting = true;
    if (tcp_client.pcb == NULL) {
        LOGE("Failed to create PCB");
        xSemaphoreGive(tcp_client.mutex);
        return false;
    }

    // 设置回调
    tcp_arg(tcp_client.pcb, arg);
    tcp_recv(tcp_client.pcb, tcp_recv_callback);
    tcp_err(tcp_client.pcb, tcp_err_callback);
    tcp_sent(tcp_client.pcb, tcp_sent_callback);

    // 解析服务器IP
    ip_addr_t server_ip;
    if (ipaddr_aton(host, &server_ip) == 0) {
        ESP_LOGE(TAG, "Invalid server IP: %s", host);
        tcp_close(tcp_client.pcb);
        tcp_client.pcb = NULL;
        xSemaphoreGive(tcp_client.mutex);
        return false;
    }

    // 发起连接
    err_t err = tcp_connect(tcp_client.pcb, &server_ip, port, tcp_connected_cb);
    tcp_client.connecting = false;
    if (err != ERR_OK) {
        ESP_LOGE(TAG, "Connect failed: %d", err);
        tcp_close(tcp_client.pcb);
        tcp_client.pcb = NULL;
        xSemaphoreGive(tcp_client.mutex);
        return false;
    }

    tcp_client.connected = true;
    ESP_LOGI(TAG, "Connecting to %s:%d...", host, port);
    xSemaphoreGive(tcp_client.mutex);
    return true;
}

// 发送数据
static int tcp_client_send(const void *data, size_t len)
{
    if (xSemaphoreTake(tcp_client.mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return -1;
    }

    if (!tcp_client.connected || tcp_client.pcb == NULL) {
        xSemaphoreGive(tcp_client.mutex);
        return -1;
    }
    int qc = tcp_sndqueuelen(tcp_client.pcb);
    printf("queue len: %d max queue %d valid send buf %d\n", qc, TCP_SND_QUEUELEN, tcp_sndbuf(tcp_client.pcb));
    err_t err = tcp_write(tcp_client.pcb, data, len, TCP_WRITE_FLAG_COPY);
    if (err != ERR_OK) {
        ESP_LOGE(TAG, "Write failed: %d", err);
        xSemaphoreGive(tcp_client.mutex);
        return -1;
    }

    err = tcp_output(tcp_client.pcb);
    if (err != ERR_OK) {
        ESP_LOGE(TAG, "Output failed: %d", err);
        xSemaphoreGive(tcp_client.mutex);
        return -1;
    }

    xSemaphoreGive(tcp_client.mutex);
    return len;
}

// 断开连接
static void tcp_client_disconnect(void)
{
    if (xSemaphoreTake(tcp_client.mutex, pdMS_TO_TICKS(200)) != pdTRUE) {
        return;
    }

    if (tcp_client.pcb != NULL) {
        tcp_arg(tcp_client.pcb, NULL);
        tcp_recv(tcp_client.pcb, NULL);
        tcp_err(tcp_client.pcb, NULL);
        tcp_sent(tcp_client.pcb, NULL);
        
        if (tcp_client.connected) {
            tcp_close(tcp_client.pcb);
        } else {
            tcp_abort(tcp_client.pcb);
        }
        
        tcp_client.pcb = NULL;
    }

    tcp_client.connected = false;
    xSemaphoreGive(tcp_client.mutex);
}
