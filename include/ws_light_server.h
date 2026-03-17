
#pragma once

#include <esp_event.h>
#include <esp_log.h>
#include <esp_wifi.h>
#include <lwip/sockets.h>
#include <string>
#include <functional>
#include "ws_types.h"
#include "freertos/timers.h"
#include "freertos/semphr.h"
#include "esp_timer.h"

#define WS_MAX_FRAME_SIZE (16 * 1024)

/**
 * @struct ws_message_t
 * @brief Structure to hold a WebSocket message.
 */
struct ws_message_t
{
    int client_sock; /**< Socket of the client */
    uint8_t *data;   /**< Pointer to the message data */
    size_t length;   /**< Length of the message */
    ws_type_t type;  /**< Type of the message */
    bool broadcast;  /**< Broadcast flag */
};
class WSLightServer
{
public:
    static WSLightServer &getInstance();

    esp_err_t start(const char *ssid,
                    const char *password,
                    uint16_t port,
                    uint64_t ping_interval_ms,
                    uint64_t max_inactivity_ms,
                    bool isAp,
                    bool enable_ping_pong,
                    std::function<void()> extra_config, uint32_t stackSize = 4096);

    bool isClientConnected() const;

    void onTextMessage(std::function<void(int, const std::string &)> cb);
    void onBinaryMessage(std::function<void(int, uint8_t *, size_t)> cb);
    void onPingMessage(std::function<void(int)> cb);
    void onPongMessage(std::function<void(int)> cb);
    void onCloseMessage(std::function<void(int)> cb);
    void onClientConnected(std::function<void(int)> cb);
    void onClientDisconnected(std::function<void(int)> cb);

    esp_err_t sendTextMessage(const char *text, size_t length);
    esp_err_t sendTextMessage(const char *text);
    esp_err_t sendBinaryMessage(const uint8_t *data, size_t length);

    esp_err_t sendVideoFrame(const uint8_t *data, size_t length);

    static constexpr bool debug = false;
    esp_err_t stop(void);

private:
    WSLightServer();
    static WSLightServer *instance;
    static void handle_client_wrapper(void *);
    void handle_client();
    static void ping_task_wrapper(void *);
    void ping_task();

    static void got_ip_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data);
    static esp_err_t send_handshake_response(int sock, const char *request);

    uint8_t rxBuffer[WS_MAX_FRAME_SIZE];
    uint8_t txBuffer[WS_MAX_FRAME_SIZE];

    esp_err_t wifi_init(const char *ssid,
                        const char *password,
                        bool isAp,
                        std::function<void()> extra_config);

    int readFrame(int sock, uint8_t *buf, size_t bufsize);
    bool decodeFrameInPlace(uint8_t *buf, int length,
                            ws_type_t &opcode,
                            uint8_t *&payload,
                            size_t &payloadLen);
    void sendFrame(int sock,
                   const uint8_t *data,
                   size_t length,
                   ws_type_t opcode, bool fin = true);

    std::function<void(int, const std::string &)> text_message_cb;
    std::function<void(int, uint8_t *, size_t)> binary_message_cb;
    std::function<void(int)> ping_cb;
    std::function<void(int)> pong_cb;
    std::function<void(int)> close_cb;
    std::function<void(int)> client_connected_cb;
    std::function<void(int)> client_disconnected_cb;

    int server_sock;
    int client_sock;
    bool isAp;
    bool ping_pong_enabled;
    uint64_t ping_interval_ms;
    uint64_t max_inactivity_ms;
    uint16_t port;
    SemaphoreHandle_t sockMutex;
    int64_t lastRxMs;

    int64_t nowMs() const
    {
        return esp_timer_get_time() / 1000;
    }

    int getClientSockSafe()
    {
        int s;
        xSemaphoreTake(sockMutex, portMAX_DELAY);
        s = client_sock;
        xSemaphoreGive(sockMutex);
        return s;
    }

    void setClientSockSafe(int s)
    {
        xSemaphoreTake(sockMutex, portMAX_DELAY);
        client_sock = s;
        xSemaphoreGive(sockMutex);
    }

    void closeClientSockSafe()
    {
        int s;
        xSemaphoreTake(sockMutex, portMAX_DELAY);
        s = client_sock;
        client_sock = -1;
        xSemaphoreGive(sockMutex);

        if (s >= 0)
        {
            shutdown(s, SHUT_RDWR);
            close(s);
        }
    }
    TaskHandle_t clientTaskHandle;
    TaskHandle_t pingTaskHandle;
    volatile bool running;
    bool gotIpRegistered;

    esp_netif_t *wifi_netif_{nullptr};
    bool netif_is_ap_{false};
};
