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
#include <sys/socket.h>

static const char *TAG = "WSLightServer";
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
      client_sock(-1) 
{
}

bool WSLightServer::isClientConnected() const
{
    return (client_sock > 0);
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
    ESP_LOGI("WiFi", "Conectado con IP: " IPSTR, IP2STR(&event->ip_info.ip));
}

esp_err_t WSLightServer::wifi_init(const char *ssid,
                                   const char *password,
                                   bool isAp,
                                   std::function<void()> extra_config)
{
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    if (isAp)
    {
        esp_netif_create_default_wifi_ap();
    }
    else
    {
        esp_netif_create_default_wifi_sta();
    }

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

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

        ESP_ERROR_CHECK(esp_event_handler_register(
            IP_EVENT, IP_EVENT_STA_GOT_IP,
            &got_ip_handler, this));
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
    if (debug)

    {
        ESP_LOGI(TAG, "Handshake successful with client %d", sock);
    }
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
                               std::function<void()> extra_config)
{
    this->port = port;
    this->ping_interval_ms = ping_interval_ms;
    this->max_inactivity_ms = max_inactivity_ms;
    this->isAp = isAp;
    this->ping_pong_enabled = enable_ping_pong;

    esp_err_t ret = wifi_init(ssid, password, isAp, extra_config);
    if (ret != ESP_OK)
    {
        return ret;
    }

    xTaskCreate(&WSLightServer::handle_client_wrapper, "ws_client_task", 4096, this, 5, nullptr);
    if (ping_pong_enabled)
    {
        xTaskCreate(&WSLightServer::ping_task_wrapper, "ws_ping_task", 2048, this, 5, nullptr);
    }
    return ESP_OK;
}

void WSLightServer::ping_task_wrapper(void *arg)
{
    static_cast<WSLightServer *>(arg)->ping_task();
}

void WSLightServer::ping_task()
{
    for (;;)
    {
        if (client_sock > 0 && ping_pong_enabled)
        {
            uint8_t ping_payload[4];
            uint32_t rnd = esp_random();
            memcpy(ping_payload, &rnd, 4);

            sendFrame(client_sock, ping_payload, 4, HTTPD_WS_TYPE_PING);
            if (debug)
            {
                ESP_LOGI("WSLightServer", " Sending PING to client %d: %02X %02X %02X %02X", client_sock,
                         ping_payload[0], ping_payload[1], ping_payload[2], ping_payload[3]);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(ping_interval_ms));
    }
}

void WSLightServer::handle_client_wrapper(void *arg)
{
    static_cast<WSLightServer *>(arg)->handle_client();
}

void WSLightServer::handle_client()
{
    server_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (server_sock < 0)
    {
        ESP_LOGE(TAG, "Error creating socket");
        vTaskDelete(nullptr);
        return;
    }

    int opt = 1;
    setsockopt(server_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(server_sock, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) != 0)
    {
        ESP_LOGE(TAG, "Socket bind failed");
        close(server_sock);
        vTaskDelete(nullptr);
        return;
    }

    if (listen(server_sock, 1) != 0)
    {
        ESP_LOGE(TAG, "Socket listen failed");
        close(server_sock);
        vTaskDelete(nullptr);
        return;
    }

    ESP_LOGI(TAG, "Server listening on port %d", port);

    for (;;)
    {

        sockaddr_in caddr;
        socklen_t caddr_len = sizeof(caddr);

        client_sock = accept(server_sock, reinterpret_cast<sockaddr *>(&caddr), &caddr_len);
        if (client_sock < 0)
        {
            ESP_LOGE(TAG, "Accept error");
            continue;
        }

        if (client_connected_cb)
        {
            client_connected_cb(client_sock);
        }
        ESP_LOGI(TAG, "Client %d connected", client_sock);

        char req[512];
        int r = recv_http_request(client_sock, req, sizeof(req));
        if (debug)
        {
            ESP_LOGI(TAG, "Received HTTP request:\n%s", req);
        }

        if (r <= 0)
        {
            if (debug)
            {
                ESP_LOGE(TAG, "Request length is invalid, len of request: %d", r);
            }
            close(client_sock);
            client_sock = -1;
            continue;
        }
        if (!is_websocket_request(req))
        {
            if (debug)
            {
                ESP_LOGE(TAG, "Wrong websocket request: %d", r);
            }
            close(client_sock);
            client_sock = -1;
            continue;
        }
        if (send_handshake_response(client_sock, req) != ESP_OK)
        {
            ESP_LOGE(TAG, "Handshake NO completed with client %d", client_sock);
            close(client_sock);
            client_sock = -1;
            continue;
        }
        if (debug)
        {
            ESP_LOGW(TAG, "Handshake completed with client %d", client_sock);
        }

        for (;;)
        {
            int frame_len = readFrame(client_sock, rxBuffer, WS_MAX_FRAME_SIZE);
            if (debug)
            {
                ESP_LOGE("Debug", "Frame readed with len %d", frame_len);
            }

            if (frame_len == -2)
            {

                continue;
            }
            else if (frame_len <= 0)
            {

                close(client_sock);
                if (client_disconnected_cb)
                {
                    client_disconnected_cb(client_sock);
                }
                client_sock = -1;
                break;
            }

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
                    text_message_cb(client_sock, std::string(reinterpret_cast<char *>(payload), payloadLen));
                }
                else
                {
                    ESP_LOGI("WSLightServer", "Text received: %s", (std::string(reinterpret_cast<char *>(payload)).c_str()));
                }
                break;

            case HTTPD_WS_TYPE_BINARY:
                if (binary_message_cb)
                {
                    binary_message_cb(client_sock, payload, payloadLen);
                }
                else
                {
                    ESP_LOGI("WSLightServer", "Binary received (%d of bytes)", payloadLen);
                }
                break;

            case HTTPD_WS_TYPE_PING:
                if (ping_cb)
                {
                    ping_cb(client_sock);
                }
                if (client_sock > 0)
                {
                    if (debug)
                    {
                        ESP_LOGI("WSLightServer", "PING received from %d, sending PONG", client_sock);
                    }
                    sendFrame(client_sock, payload, payloadLen, HTTPD_WS_TYPE_PONG);
                }
                break;

            case HTTPD_WS_TYPE_PONG:
                if (pong_cb)
                {
                    pong_cb(client_sock);
                }
                if (debug)
                {
                    ESP_LOGI("WSLightServer", " PONG received from %d", client_sock);
                }
                break;

            case HTTPD_WS_TYPE_CLOSE:
                if (close_cb)
                {
                    close_cb(client_sock);
                }
                sendFrame(client_sock, nullptr, 0, HTTPD_WS_TYPE_CLOSE);
                close(client_sock);
                client_sock = -1;
                break;

            default:
                if (debug)
                {
                    ESP_LOGI("WSLightServer", " Unknown received from client: %d", client_sock);
                }
                break;
            }

            if (client_sock < 0)
            {

                break;
            }
        }
    }
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

    uint8_t opcode = buf[0] & 0x0F;
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

    headerLen += 4;

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
        payloadLen = (buf[2] << 8) | buf[3];
    }
    else if ((buf[1] & 0x7F) == 127)
    {
        uint64_t bigLen = 0;
        for (int i = 0; i < 8; i++)
        {
            bigLen = (bigLen << 8) | buf[2 + i];
        }
        if (bigLen > SIZE_MAX)
        {
            ESP_LOGE(TAG, "Payload too large for size_t");
            return -1;
        }
        payloadLen = static_cast<size_t>(bigLen);
    }

    totalNeeded = headerLen + payloadLen;

    if (totalNeeded > bufsize)
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

            bytesReceived += r;
        }

        if (opcode == HTTPD_WS_TYPE_BINARY && binary_message_cb)
        {
            binary_message_cb(client_sock, bigBuffer, payloadLen);
        }
        else if (opcode == HTTPD_WS_TYPE_TEXT && text_message_cb)
        {
            text_message_cb(client_sock, std::string(reinterpret_cast<char *>(bigBuffer), payloadLen));
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
    if (client_sock < 0)
    {
        return ESP_FAIL;
    }

    sendFrame(client_sock, (const uint8_t *)text, length, HTTPD_WS_TYPE_TEXT);
    return ESP_OK;
}

esp_err_t WSLightServer::sendTextMessage(const char *text)
{
    if (client_sock < 0)
    {
        return ESP_FAIL;
    }

    sendFrame(client_sock, (const uint8_t *)text, strlen(text), HTTPD_WS_TYPE_TEXT);
    return ESP_OK;
}

esp_err_t WSLightServer::sendBinaryMessage(const uint8_t *data, size_t length)
{
    if (client_sock < 0)
    {
        return ESP_FAIL;
    }
    sendFrame(client_sock, data, length, HTTPD_WS_TYPE_BINARY);
    return ESP_OK;
}
esp_err_t WSLightServer::sendVideoFrame(const uint8_t *data, size_t length)
{
    if (client_sock < 0)
    {
        return ESP_FAIL;
    }

    size_t bytesSent = 0;
    bool firstFragment = true;

    while (bytesSent < length)
    {

        size_t chunkSize = ((length - bytesSent) > WS_MAX_FRAME_SIZE)
                               ? WS_MAX_FRAME_SIZE
                               : (length - bytesSent);

        ws_type_t opCode = firstFragment ? HTTPD_WS_TYPE_BINARY : HTTPD_WS_TYPE_CONTINUATION;

        bool isLastFragment = ((bytesSent + chunkSize) == length);

        sendFrame(client_sock, data + bytesSent, chunkSize, opCode, isLastFragment);

        bytesSent += chunkSize;
        firstFragment = false;
    }

    return ESP_OK;
}
