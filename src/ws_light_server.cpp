/**
 * @file ws_light_server.cpp
 * @brief WSLightServer server class implementation.
 *
 * This  defines the WSLightServer class implementation.
 *
 * @author Daniel Giménez
 * @date 2024-08-05
 * @license MIT License
 *
 * @par License:
 *
 * MIT License
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "ws_light_server.h"
#include <cstring>
#include <lwip/netdb.h>
#include <mbedtls/base64.h>
#include <mbedtls/sha1.h>
#include <nvs_flash.h>
#include <esp_wifi.h>
#include <esp_event.h>
#include <esp_netif.h>
#include <esp_log.h>
#include <errno.h>
#include <sys/socket.h>

static const char *TAG = "WikiliftSocketServer";
WSLightServer *WSLightServer::instance = nullptr;

WSLightServer &WSLightServer::getInstance()
{
    if (!instance)
    {
        instance = new WSLightServer();
    }
    return *instance;
}
WSLightServer::WSLightServer()
    : server_sock(-1),
      client_sock(-1),
      isAp(false),
      ping_pong_enabled(true),
      ping_interval_ms(30000),
      max_inactivity_ms(60000),
      port(80),
      sockMutex(xSemaphoreCreateMutex()),
      lastRxMs(0),
      clientTaskHandle(nullptr),
      pingTaskHandle(nullptr),
      running(false),
      gotIpRegistered(false)
{
}

bool WSLightServer::isClientConnected() const
{
    int s;
    xSemaphoreTake(sockMutex, portMAX_DELAY);
    s = client_sock;
    xSemaphoreGive(sockMutex);
    return (s > 0);
}

void WSLightServer::onTextMessage(std::function<void(int, const std::string &)> cb) { text_message_cb = cb; }
void WSLightServer::onBinaryMessage(std::function<void(int, uint8_t *, size_t)> cb) { binary_message_cb = cb; }
void WSLightServer::onPingMessage(std::function<void(int)> cb) { ping_cb = cb; }
void WSLightServer::onPongMessage(std::function<void(int)> cb) { pong_cb = cb; }
void WSLightServer::onCloseMessage(std::function<void(int)> cb) { close_cb = cb; }
void WSLightServer::onClientConnected(std::function<void(int)> cb) { client_connected_cb = cb; }
void WSLightServer::onClientDisconnected(std::function<void(int)> cb) { client_disconnected_cb = cb; }

void WSLightServer::got_ip_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    ip_event_got_ip_t *event = reinterpret_cast<ip_event_got_ip_t *>(event_data);
    ESP_LOGI("WiFi", "Connected with IP: " IPSTR, IP2STR(&event->ip_info.ip));
}

esp_err_t WSLightServer::wifi_init(const char *ssid,
                                   const char *password,
                                   bool isAp,
                                   std::function<void()> extra_config)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    }
    else
    {
        ESP_ERROR_CHECK(err);
    }

    ESP_ERROR_CHECK(esp_netif_init());

    err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE)
    {
        ESP_ERROR_CHECK(err);
    }
    if (wifi_netif_ != nullptr && netif_is_ap_ != isAp)
    {
        esp_netif_destroy(wifi_netif_);
        wifi_netif_ = nullptr;
    }

    if (wifi_netif_ == nullptr)
    {
        wifi_netif_ = isAp ? esp_netif_create_default_wifi_ap()
                           : esp_netif_create_default_wifi_sta();
        if (wifi_netif_ == nullptr)
        {
            ESP_LOGE(TAG, "Failed to create default wifi netif");
            return ESP_FAIL;
        }
        netif_is_ap_ = isAp;
    }

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    err = esp_wifi_init(&cfg);
    if (err != ESP_OK && err != ESP_ERR_WIFI_INIT_STATE)
    {
        ESP_ERROR_CHECK(err);
    }

    wifi_config_t wifi_config = {};
    if (isAp)
    {
        strncpy((char *)wifi_config.ap.ssid, ssid, sizeof(wifi_config.ap.ssid) - 1);
        wifi_config.ap.ssid[sizeof(wifi_config.ap.ssid) - 1] = '\0';

        strncpy((char *)wifi_config.ap.password, password, sizeof(wifi_config.ap.password) - 1);
        wifi_config.ap.password[sizeof(wifi_config.ap.password) - 1] = '\0';

        wifi_config.ap.max_connection = 4;
        wifi_config.ap.authmode = WIFI_AUTH_WPA_WPA2_PSK;

        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
    }
    else
    {
        strncpy((char *)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid) - 1);
        wifi_config.sta.ssid[sizeof(wifi_config.sta.ssid) - 1] = '\0';

        strncpy((char *)wifi_config.sta.password, password, sizeof(wifi_config.sta.password) - 1);
        wifi_config.sta.password[sizeof(wifi_config.sta.password) - 1] = '\0';

        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));

        if (!gotIpRegistered)
        {
            ESP_ERROR_CHECK(esp_event_handler_register(
                IP_EVENT, IP_EVENT_STA_GOT_IP,
                &got_ip_handler, this));
            gotIpRegistered = true;
        }
    }

    if (extra_config)
    {
        extra_config();
    }

    ESP_ERROR_CHECK(esp_wifi_start());
    if (!isAp)
    {
        vTaskDelay(pdMS_TO_TICKS(1000));
        ESP_ERROR_CHECK(esp_wifi_connect());
    }

    return ESP_OK;
}

esp_err_t WSLightServer::send_handshake_response(int sock, const char *request)
{
    const char *key_start = strcasestr(request, "Sec-WebSocket-Key: ");
    if (!key_start)
    {
        ESP_LOGE(TAG, "Sec-WebSocket-Key not found");
        return ESP_FAIL;
    }
    key_start += 19;

    const char *key_end = strpbrk(key_start, "\r\n");
    if (!key_end)
    {
        ESP_LOGE(TAG, "Malformed Sec-WebSocket-Key header");
        return ESP_FAIL;
    }

    char key[64] = {0};
    size_t key_length = key_end - key_start;
    if (key_length >= sizeof(key) - 37)
    {
        ESP_LOGE(TAG, "Sec-WebSocket-Key too long");
        return ESP_FAIL;
    }

    strncpy(key, key_start, key_length);
    key[key_length] = '\0';
    strcat(key, "258EAFA5-E914-47DA-95CA-C5AB0DC85B11");

    unsigned char sha1_res[20];
    mbedtls_sha1((const unsigned char *)key, strlen(key), sha1_res);

    unsigned char base64_res[64];
    size_t base64_len;
    if (mbedtls_base64_encode(base64_res, sizeof(base64_res), &base64_len, sha1_res, sizeof(sha1_res)) != 0)
    {
        ESP_LOGE(TAG, "Base64 encoding failed");
        return ESP_FAIL;
    }

    char response[256];
    int response_len = snprintf(response, sizeof(response),
                                "HTTP/1.1 101 Switching Protocols\r\n"
                                "Upgrade: websocket\r\n"
                                "Connection: Upgrade\r\n"
                                "Sec-WebSocket-Accept: %s\r\n\r\n",
                                base64_res);

    if (send(sock, response, response_len, 0) < 0)
    {
        ESP_LOGE(TAG, "Failed to send handshake response");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Handshake successful with client %d", sock);
    return ESP_OK;
}

static bool is_websocket_request(const char *req)
{
    return (strcasestr(req, "Upgrade: websocket") != nullptr);
}

static int recv_http_request(int sock, char *buffer, size_t buflen)
{
    int r = recv(sock, buffer, buflen - 1, 0);
    if (r > 0)
    {
        buffer[r] = '\0';
    }
    return r;
}
esp_err_t WSLightServer::start(const char *ssid,
                               const char *password,
                               uint16_t port,
                               uint64_t ping_interval_ms,
                               uint64_t max_inactivity_ms,
                               bool isAp,
                               bool enable_ping_pong,
                               std::function<void()> extra_config,
                               uint32_t stackSize)
{
    if (running)
    {
        ESP_LOGW(TAG, "start() called while already running");
        return ESP_ERR_INVALID_STATE;
    }

    if (sockMutex == nullptr)
    {
        ESP_LOGE(TAG, "sockMutex is null");
        return ESP_FAIL;
    }

    this->port = port;
    this->ping_interval_ms = ping_interval_ms;
    this->isAp = isAp;

    this->ping_pong_enabled = enable_ping_pong && (ping_interval_ms > 0);
    this->max_inactivity_ms = (this->ping_pong_enabled) ? max_inactivity_ms : 0;

    xSemaphoreTake(sockMutex, portMAX_DELAY);
    client_sock = -1;
    server_sock = -1;
    lastRxMs = 0;
    clientTaskHandle = nullptr;
    pingTaskHandle = nullptr;
    xSemaphoreGive(sockMutex);

    running = true;

    const esp_err_t ret = wifi_init(ssid, password, isAp, extra_config);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "wifi_init failed: %s (%d)", esp_err_to_name(ret), (int)ret);
        running = false;
        return ret;
    }

    TaskHandle_t ct = nullptr;
    BaseType_t ok1 = xTaskCreate(&WSLightServer::handle_client_wrapper,
                                 "ws_client_task",
                                 stackSize,
                                 this,
                                 5,
                                 &ct);

    if (ok1 != pdPASS)
    {
        ESP_LOGE(TAG, "xTaskCreate(ws_client_task) FAILED. stackParam=%u (FreeRTOS words!), freeHeap=%u",
                 (unsigned)stackSize, (unsigned)esp_get_free_heap_size());

        running = false;
        if (!isAp)
        {
            (void)esp_wifi_disconnect();
        }
        (void)esp_wifi_stop();
        (void)esp_wifi_deinit();

        return ESP_FAIL;
    }

    xSemaphoreTake(sockMutex, portMAX_DELAY);
    clientTaskHandle = ct;
    xSemaphoreGive(sockMutex);

    if (ping_pong_enabled)
    {
        TaskHandle_t pt = nullptr;
        BaseType_t ok2 = xTaskCreate(&WSLightServer::ping_task_wrapper,
                                     "ws_ping_task",
                                     2048,
                                     this,
                                     5,
                                     &pt);

        if (ok2 != pdPASS)
        {
            ESP_LOGE(TAG, "xTaskCreate(ws_ping_task) FAILED. freeHeap=%u",
                     (unsigned)esp_get_free_heap_size());
            (void)stop();
            return ESP_FAIL;
        }

        xSemaphoreTake(sockMutex, portMAX_DELAY);
        pingTaskHandle = pt;
        xSemaphoreGive(sockMutex);
    }

    return ESP_OK;
}

void WSLightServer::ping_task_wrapper(void *arg)
{
    static_cast<WSLightServer *>(arg)->ping_task();
}

void WSLightServer::ping_task()
{
    while (running)
    {
        int s = -1;

        xSemaphoreTake(sockMutex, portMAX_DELAY);
        s = client_sock;
        xSemaphoreGive(sockMutex);

        if (s > 0 && ping_pong_enabled)
        {
            uint8_t ping_payload[4];
            const uint32_t rnd = esp_random();
            memcpy(ping_payload, &rnd, sizeof(rnd));

            xSemaphoreTake(sockMutex, portMAX_DELAY);
            const bool canSend = (client_sock == s && running);
            xSemaphoreGive(sockMutex);

            if (canSend)
            {
                sendFrame(s, ping_payload, sizeof(ping_payload), HTTPD_WS_TYPE_PING);
            }
        }

        vTaskDelay(pdMS_TO_TICKS(ping_interval_ms));
    }

    xSemaphoreTake(sockMutex, portMAX_DELAY);
    pingTaskHandle = nullptr;
    xSemaphoreGive(sockMutex);

    vTaskDelete(nullptr);
}

void WSLightServer::handle_client_wrapper(void *arg)
{
    static_cast<WSLightServer *>(arg)->handle_client();
}

void WSLightServer::handle_client()
{
    int s = socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0)
    {
        xSemaphoreTake(sockMutex, portMAX_DELAY);
        clientTaskHandle = nullptr;
        xSemaphoreGive(sockMutex);
        vTaskDelete(nullptr);
        return;
    }

    xSemaphoreTake(sockMutex, portMAX_DELAY);
    server_sock = s;
    xSemaphoreGive(sockMutex);

    int opt = 1;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(s, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) != 0)
    {
        shutdown(s, SHUT_RDWR);
        close(s);

        xSemaphoreTake(sockMutex, portMAX_DELAY);
        server_sock = -1;
        clientTaskHandle = nullptr;
        xSemaphoreGive(sockMutex);

        vTaskDelete(nullptr);
        return;
    }

    if (listen(s, 1) != 0)
    {
        shutdown(s, SHUT_RDWR);
        close(s);

        xSemaphoreTake(sockMutex, portMAX_DELAY);
        server_sock = -1;
        clientTaskHandle = nullptr;
        xSemaphoreGive(sockMutex);

        vTaskDelete(nullptr);
        return;
    }

    while (running)
    {
        sockaddr_in caddr;
        socklen_t caddr_len = sizeof(caddr);

        int accepted = accept(s, reinterpret_cast<sockaddr *>(&caddr), &caddr_len);
        if (accepted < 0)
        {
            if (!running)
            {
                break;
            }
            continue;
        }

        xSemaphoreTake(sockMutex, portMAX_DELAY);
        client_sock = accepted;
        lastRxMs = esp_timer_get_time() / 1000;
        xSemaphoreGive(sockMutex);

        if (client_connected_cb)
        {
            client_connected_cb(accepted);
        }

        char req[512];
        int r = recv_http_request(accepted, req, sizeof(req));

        if (r <= 0 || !is_websocket_request(req) || send_handshake_response(accepted, req) != ESP_OK)
        {
            shutdown(accepted, SHUT_RDWR);
            close(accepted);

            xSemaphoreTake(sockMutex, portMAX_DELAY);
            if (client_sock == accepted)
                client_sock = -1;
            xSemaphoreGive(sockMutex);

            continue;
        }

        struct timeval tv;
        tv.tv_sec = 0;
        tv.tv_usec = 250 * 1000;
        setsockopt(accepted, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

        while (running)
        {
            int frame_len = readFrame(accepted, rxBuffer, WS_MAX_FRAME_SIZE);

            if (frame_len == -2)
            {
                xSemaphoreTake(sockMutex, portMAX_DELAY);
                if (client_sock == accepted)
                    lastRxMs = esp_timer_get_time() / 1000;
                xSemaphoreGive(sockMutex);
                continue;
            }

            if (frame_len <= 0)
            {
                if (frame_len < 0 && (errno == EWOULDBLOCK || errno == EAGAIN))
                {
                    int64_t last = 0;
                    xSemaphoreTake(sockMutex, portMAX_DELAY);
                    if (client_sock == accepted)
                        last = lastRxMs;
                    xSemaphoreGive(sockMutex);

                    const int64_t now = esp_timer_get_time() / 1000;
                    if (max_inactivity_ms > 0 && last > 0 && (uint64_t)(now - last) > max_inactivity_ms)
                    {
                        if (client_disconnected_cb)
                        {
                            client_disconnected_cb(accepted);
                        }

                        shutdown(accepted, SHUT_RDWR);
                        close(accepted);

                        xSemaphoreTake(sockMutex, portMAX_DELAY);
                        if (client_sock == accepted)
                            client_sock = -1;
                        xSemaphoreGive(sockMutex);

                        break;
                    }

                    continue;
                }

                shutdown(accepted, SHUT_RDWR);
                close(accepted);

                if (client_disconnected_cb)
                {
                    client_disconnected_cb(accepted);
                }

                xSemaphoreTake(sockMutex, portMAX_DELAY);
                if (client_sock == accepted)
                    client_sock = -1;
                xSemaphoreGive(sockMutex);

                break;
            }

            xSemaphoreTake(sockMutex, portMAX_DELAY);
            if (client_sock == accepted)
                lastRxMs = esp_timer_get_time() / 1000;
            xSemaphoreGive(sockMutex);

            ws_type_t opcode;
            uint8_t *payload;
            size_t payloadLen;

            if (!decodeFrameInPlace(rxBuffer, frame_len, opcode, payload, payloadLen))
            {
                continue;
            }

            switch (opcode)
            {
            case HTTPD_WS_TYPE_TEXT:
                if (text_message_cb)
                {
                    text_message_cb(accepted, std::string(reinterpret_cast<char *>(payload), payloadLen));
                }
                break;

            case HTTPD_WS_TYPE_BINARY:
                if (binary_message_cb)
                {
                    binary_message_cb(accepted, payload, payloadLen);
                }
                break;

            case HTTPD_WS_TYPE_PING:
                if (ping_cb)
                {
                    ping_cb(accepted);
                }
                xSemaphoreTake(sockMutex, portMAX_DELAY);
                if (client_sock == accepted && accepted > 0 && running)
                {
                    sendFrame(accepted, payload, payloadLen, HTTPD_WS_TYPE_PONG);
                }
                xSemaphoreGive(sockMutex);
                break;

            case HTTPD_WS_TYPE_PONG:
                if (pong_cb)
                {
                    pong_cb(accepted);
                }
                break;

            case HTTPD_WS_TYPE_CLOSE:
                if (close_cb)
                {
                    close_cb(accepted);
                }

                xSemaphoreTake(sockMutex, portMAX_DELAY);
                if (client_sock == accepted && accepted > 0 && running)
                {
                    sendFrame(accepted, nullptr, 0, HTTPD_WS_TYPE_CLOSE);
                }
                xSemaphoreGive(sockMutex);

                shutdown(accepted, SHUT_RDWR);
                close(accepted);

                xSemaphoreTake(sockMutex, portMAX_DELAY);
                if (client_sock == accepted)
                    client_sock = -1;
                xSemaphoreGive(sockMutex);

                break;

            default:
                break;
            }

            int current = -1;
            xSemaphoreTake(sockMutex, portMAX_DELAY);
            current = client_sock;
            xSemaphoreGive(sockMutex);

            if (current < 0)
            {
                break;
            }
        }
    }

    int ss2;
    xSemaphoreTake(sockMutex, portMAX_DELAY);
    ss2 = server_sock;
    server_sock = -1;
    xSemaphoreGive(sockMutex);

    if (ss2 >= 0)
    {
        shutdown(ss2, SHUT_RDWR);
        close(ss2);
    }

    xSemaphoreTake(sockMutex, portMAX_DELAY);
    clientTaskHandle = nullptr;
    xSemaphoreGive(sockMutex);
    vTaskDelete(nullptr);
}

int WSLightServer::readFrame(int sock, uint8_t *buf, size_t bufsize)
{
    int offset = 0;
    int needed = 2;

    while (offset < needed)
    {
        int r = recv(sock, buf + offset, needed - offset, 0);
        if (r <= 0)
            return r;
        offset += r;
    }

    const uint8_t opcode = buf[0] & 0x0F;
    const bool masked = (buf[1] & 0x80) != 0;

    size_t payloadLen = buf[1] & 0x7F;
    int headerLen = 2;

    if (payloadLen == 126)
    {
        headerLen += 2;
    }
    else if (payloadLen == 127)
    {
        headerLen += 8;
    }

    if (masked)
    {
        headerLen += 4;
    }

    int totalNeeded = headerLen;
    while (offset < totalNeeded)
    {
        int r = recv(sock, buf + offset, totalNeeded - offset, 0);
        if (r <= 0)
            return r;
        offset += r;
    }

    if ((buf[1] & 0x7F) == 126)
    {
        payloadLen = (static_cast<size_t>(buf[2]) << 8) | static_cast<size_t>(buf[3]);
    }
    else if ((buf[1] & 0x7F) == 127)
    {
        uint64_t bigLen = 0;
        for (int i = 0; i < 8; i++)
        {
            bigLen = (bigLen << 8) | static_cast<uint64_t>(buf[2 + i]);
        }
        if (bigLen > static_cast<uint64_t>(SIZE_MAX))
        {
            ESP_LOGE(TAG, "Payload too large for size_t");
            return -1;
        }
        payloadLen = static_cast<size_t>(bigLen);
    }

    totalNeeded = headerLen + static_cast<int>(payloadLen);

    if (static_cast<size_t>(totalNeeded) > bufsize)
    {
        if (debug)
        {
            ESP_LOGW(TAG, "Frame too large (%u bytes), accumulating in buffer", (unsigned)payloadLen);
        }

        uint8_t *bigBuffer = (uint8_t *)pvPortMalloc(payloadLen);
        if (!bigBuffer)
        {
            ESP_LOGE(TAG, "Could not allocate memory for large frame");
            return -1;
        }

        size_t bytesReceived = 0;
        while (bytesReceived < payloadLen)
        {
            size_t toRead = (payloadLen - bytesReceived > WS_MAX_FRAME_SIZE)
                                ? WS_MAX_FRAME_SIZE
                                : (payloadLen - bytesReceived);

            int r = recv(sock, bigBuffer + bytesReceived, toRead, 0);
            if (r <= 0)
            {
                vPortFree(bigBuffer);
                return r;
            }

            bytesReceived += static_cast<size_t>(r);
        }

        if (opcode == HTTPD_WS_TYPE_BINARY && binary_message_cb)
        {
            binary_message_cb(sock, bigBuffer, payloadLen);
        }
        else if (opcode == HTTPD_WS_TYPE_TEXT && text_message_cb)
        {
            text_message_cb(sock, std::string(reinterpret_cast<char *>(bigBuffer), payloadLen));
        }

        vPortFree(bigBuffer);
        return -2;
    }

    while (offset < totalNeeded)
    {
        int r = recv(sock, buf + offset, totalNeeded - offset, 0);
        if (r <= 0)
            return r;
        offset += r;
    }

    return offset;
}

bool WSLightServer::decodeFrameInPlace(uint8_t *buf,
                                       int length,
                                       ws_type_t &opcode,
                                       uint8_t *&payload,
                                       size_t &payloadLen)
{
    if (length < 2)
    {
        ESP_LOGE(TAG, "Frame too short");
        return false;
    }

    opcode = static_cast<ws_type_t>(buf[0] & 0x0F);
    uint8_t tmpLen = (buf[1] & 0x7F);
    int headerLen = 2;

    if (tmpLen == 126)
    {
        if (length < 4)
        {
            ESP_LOGE(TAG, "Very short frame for 126");
            return false;
        }
        payloadLen = (buf[2] << 8) | buf[3];
        headerLen += 2;
    }
    else if (tmpLen == 127)
    {
        if (length < 10)
        {
            ESP_LOGE(TAG, "Very short frame for 127");
            return false;
        }
        uint64_t bigLen = 0;
        for (int i = 0; i < 8; i++)
        {
            bigLen = (bigLen << 8) | buf[2 + i];
        }
        if (bigLen > SIZE_MAX)
        {
            ESP_LOGE(TAG, "Payload greater than SIZE_MAX");
            return false;
        }
        payloadLen = (size_t)bigLen;
        headerLen += 8;
    }
    else
    {
        payloadLen = tmpLen;
    }

    if (buf[1] & 0x80)
    {
        headerLen += 4;
    }

    if ((int)(payloadLen + headerLen) > length)
    {
        ESP_LOGE(TAG, "Incomplete frame: Expected %u bytes, but received %u bytes",
                 (unsigned)(payloadLen + headerLen), (unsigned)length);
        return false;
    }

    if (buf[1] & 0x80)
    {
        uint8_t *mask = &buf[headerLen - 4];
        int dataStart = headerLen;
        for (size_t i = 0; i < payloadLen; i++)
        {
            buf[dataStart + i] ^= mask[i % 4];
        }
    }

    payload = &buf[headerLen];
    return true;
}

void WSLightServer::sendFrame(int sock,
                              const uint8_t *data,
                              size_t length,
                              ws_type_t opcode, bool fin)
{

    int offset = 0;

    uint8_t finBit = fin ? 0x80 : 0x00;
    txBuffer[offset++] = finBit | static_cast<uint8_t>(opcode);

    if (length <= 125)
    {
        txBuffer[offset++] = (uint8_t)length;
    }
    else if (length <= 65535)
    {
        txBuffer[offset++] = 126;
        txBuffer[offset++] = (uint8_t)((length >> 8) & 0xFF);
        txBuffer[offset++] = (uint8_t)(length & 0xFF);
    }
    else
    {
        txBuffer[offset++] = 127;
        uint64_t bigLen = (uint64_t)length;
        for (int i = 7; i >= 0; i--)
        {
            txBuffer[offset++] = (uint8_t)((bigLen >> (8 * i)) & 0xFF);
        }
    }

    if (data && length > 0)
    {
        memcpy(&txBuffer[offset], data, length);
        offset += length;
    }

    send(sock, txBuffer, offset, 0);
}

esp_err_t WSLightServer::sendTextMessage(const char *text, size_t length)
{
    int s;
    xSemaphoreTake(sockMutex, portMAX_DELAY);
    s = client_sock;
    xSemaphoreGive(sockMutex);

    if (s < 0)
    {
        return ESP_FAIL;
    }

    sendFrame(s, reinterpret_cast<const uint8_t *>(text), length, HTTPD_WS_TYPE_TEXT);
    return ESP_OK;
}

esp_err_t WSLightServer::sendTextMessage(const char *text)
{
    return sendTextMessage(text, strlen(text));
}

esp_err_t WSLightServer::sendBinaryMessage(const uint8_t *data, size_t length)
{
    int s;
    xSemaphoreTake(sockMutex, portMAX_DELAY);
    s = client_sock;
    xSemaphoreGive(sockMutex);

    if (s < 0)
    {
        return ESP_FAIL;
    }

    sendFrame(s, data, length, HTTPD_WS_TYPE_BINARY);
    return ESP_OK;
}

esp_err_t WSLightServer::sendVideoFrame(const uint8_t *data, size_t length)
{
    int s;
    xSemaphoreTake(sockMutex, portMAX_DELAY);
    s = client_sock;
    const bool isRunning = running;
    xSemaphoreGive(sockMutex);

    if (!isRunning || s < 0)
    {
        return ESP_FAIL;
    }

    size_t bytesSent = 0;
    bool firstFragment = true;

    while (bytesSent < length)
    {
        xSemaphoreTake(sockMutex, portMAX_DELAY);
        const bool stillRunning = running && (client_sock == s);
        xSemaphoreGive(sockMutex);

        if (!stillRunning)
        {
            return ESP_FAIL;
        }

        const size_t chunkSize = ((length - bytesSent) > WS_MAX_FRAME_SIZE)
                                     ? WS_MAX_FRAME_SIZE
                                     : (length - bytesSent);

        const ws_type_t opCode = firstFragment ? HTTPD_WS_TYPE_BINARY : HTTPD_WS_TYPE_CONTINUATION;
        const bool isLastFragment = ((bytesSent + chunkSize) == length);

        sendFrame(s, data + bytesSent, chunkSize, opCode, isLastFragment);

        bytesSent += chunkSize;
        firstFragment = false;
    }

    return ESP_OK;
}

esp_err_t WSLightServer::stop()
{
    if (!running)
    {
        return ESP_OK;
    }

    running = false;

    closeClientSockSafe();

    int ss;
    xSemaphoreTake(sockMutex, portMAX_DELAY);
    ss = server_sock;
    server_sock = -1;
    xSemaphoreGive(sockMutex);

    if (ss >= 0)
    {
        shutdown(ss, SHUT_RDWR);
        close(ss);
    }

    const int64_t t0 = esp_timer_get_time() / 1000;
    for (;;)
    {
        TaskHandle_t ct;
        TaskHandle_t pt;

        xSemaphoreTake(sockMutex, portMAX_DELAY);
        ct = clientTaskHandle;
        pt = pingTaskHandle;
        xSemaphoreGive(sockMutex);

        if (ct == nullptr && pt == nullptr)
        {
            break;
        }

        const int64_t now = esp_timer_get_time() / 1000;
        if ((now - t0) > 2000)
        {
            break;
        }

        vTaskDelay(pdMS_TO_TICKS(50));
    }

    if (!isAp)
    {
        (void)esp_wifi_disconnect();
    }

    (void)esp_wifi_stop();
    (void)esp_wifi_deinit();

    if (gotIpRegistered)
    {
        (void)esp_event_handler_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, &got_ip_handler);
        gotIpRegistered = false;
    }

    if (wifi_netif_ != nullptr)
    {
        esp_netif_destroy(wifi_netif_);
        wifi_netif_ = nullptr;
    }

    xSemaphoreTake(sockMutex, portMAX_DELAY);
    const bool tasksStopped = (clientTaskHandle == nullptr && pingTaskHandle == nullptr);
    xSemaphoreGive(sockMutex);

    return tasksStopped ? ESP_OK : ESP_FAIL;
}
