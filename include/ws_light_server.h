/**
 * @file ws_light_server.h
 * @brief WSLightServer class for managing a single WebSocket connection on ESP32.
 *
 * This file provides a lightweight WebSocket server implementation for the ESP32
 * using the ESP-IDF framework. It supports handling a single connected client,
 * with text and binary message processing, ping/pong keep-alive, and connection monitoring.
 *
 * @author Daniel Giménez
 * @date 2024-08-05
 * @date updated 2025-03-24 - Full revamplicense MIT License
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

#pragma once

#include <esp_event.h>
#include <esp_log.h>
#include <esp_wifi.h>
#include <lwip/sockets.h>
#include <string>
#include <functional>
#include "ws_types.h"
#include "freertos/timers.h"

#ifndef WS_MAX_FRAME_SIZE
/**
 * @brief Maximum expected size for incoming or outgoing WebSocket frames.
 *
 * This value defines the static buffer size allocated for receiving and sending WebSocket frames.
 * Internally, the server uses two buffers of this size (RX and TX), so memory usage will be roughly 2× this value.
 *
 * @warning Increasing this value is not recommended due to ESP32 memory constraints. If a larger frame is received,
 * the server will handle it dynamically by allocating and freeing heap memory as needed.
 */
#define WS_MAX_FRAME_SIZE (16 * 1024)
#endif

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
};

/**
 * @class WSLightServer
 * @brief Lightweight WebSocket server for ESP32 supporting a single client.
 *
 * This class provides a WebSocket server implementation using the ESP-IDF framework. It supports
 * basic WebSocket operations, including message handling (text and binary), ping/pong keep-alive,
 * client connection events, and optional AP/station Wi-Fi initialization.
 */

class WSLightServer
{
public:
    /**
     * @brief Returns the singleton instance of the server.
     * @return WSLightServer& Reference to the singleton.
     */
    static WSLightServer &getInstance();

    /**
     * @brief Starts the WebSocket server with Wi-Fi and socket configuration.
     *
     * If called with no arguments, the server starts in Access Point mode with:
     * - SSID: "Wikilift ssid"
     * - Password: "myAwesomePwd123456"
     * - Port: 80
     * - Ping interval: 6000 ms
     * - Max inactivity timeout: 50000 ms
     * - Ping/pong enabled
     *
     * @param ssid Wi-Fi SSID. Default: "Wikilift ssid".
     * @param password Wi-Fi password. Default: "myAwesomePwd123456".
     * @param port Listening port for the server. Default: 80.
     * @param ping_interval_ms Ping interval in ms. Default: 6000.
     * @param max_inactivity_ms Inactivity timeout in ms. Default: 50000.
     * @param isAp Whether to run in AP mode (true) or STA mode (false). Default: true.
     * @param enable_ping_pong Enable ping/pong keep-alive. Default: true.
     * @param extra_config Optional callback for additional Wi-Fi config. Default: nullptr.
     *
     * @return esp_err_t ESP_OK on success, ESP_FAIL otherwise.
     */

    esp_err_t start(const char *ssid = "Wikilift ssid",
                    const char *password = "myAwesomePwd123456",
                    uint16_t port = 80,
                    uint64_t ping_interval_ms = 6000,
                    uint64_t max_inactivity_ms = 50000,
                    bool isAp = true,
                    bool enable_ping_pong = true,
                    std::function<void()> extra_config = nullptr);

    /**
     * @brief Checks whether a client is currently connected.
     * @return true if a client is connected, false otherwise.
     */
    bool isClientConnected() const;

    /**
     * @brief Registers a callback for incoming text messages.
     * @param cb Function to handle (socket, text).
     */
    void onTextMessage(std::function<void(int, const std::string &)> cb);

    /**
     * @brief Registers a callback for incoming binary messages.
     * @param cb Function to handle (socket, data pointer, data length).
     */
    void onBinaryMessage(std::function<void(int, uint8_t *, size_t)> cb);

    /**
     * @brief Registers a callback for received ping messages.
     * @param cb Function to handle (socket).
     */
    void onPingMessage(std::function<void(int)> cb);

    /**
     * @brief Registers a callback for received pong messages.
     * @param cb Function to handle (socket).
     */
    void onPongMessage(std::function<void(int)> cb);

    /**
     * @brief Registers a callback for client disconnection.
     * @param cb Function to handle (socket).
     */
    void onCloseMessage(std::function<void(int)> cb);

    /**
     * @brief Registers a callback for when a new client connects.
     * @param cb Function to handle (socket).
     */
    void onClientConnected(std::function<void(int)> cb);

    /**
     * @brief Registers a callback for when a client disconnects.
     * @param cb Function to handle (socket).
     */
    void onClientDisconnected(std::function<void(int)> cb);

    /**
     * @brief Sends a text message of specified length to the connected client.
     * @param text Pointer to the text data.
     * @param length Length of the text (does not include null terminator).
     * @return esp_err_t ESP_OK if successful.
     */
    esp_err_t sendTextMessage(const char *text, size_t length);

    /**
     * @brief Sends a null-terminated text message to the connected client.
     * @param text C-style string.
     * @return esp_err_t ESP_OK if successful.
     */

    esp_err_t sendTextMessage(const char *text);

    /**
     * @brief Sends a binary message to the connected client.
     * @param data Pointer to the binary data.
     * @param length Length of the data.
     * @return esp_err_t ESP_OK if successful.
     */
    esp_err_t sendBinaryMessage(const uint8_t *data, size_t length);

    /**
     * @brief Sends a raw video frame (treated as binary) to the connected client.
     * @param data Pointer to the frame buffer.
     * @param length Size of the frame buffer.
     * @return esp_err_t ESP_OK if successful.
     */
    esp_err_t sendVideoFrame(const uint8_t *data, size_t length);

    /**
     * @brief If set to true, enables debug-level logging in the implementation.
     */
    static constexpr bool debug = false;

private:
    /**
     * @brief Private constructor for singleton pattern.
     */
    WSLightServer();

    /**
     * @brief Pointer to the singleton instance.
     */
    static WSLightServer *instance;

    /**
     * @brief Static wrapper to call the instance's client handler.
     * @param arg Unused argument (typically nullptr).
     */
    static void handle_client_wrapper(void *arg);

    /**
     * @brief Handles incoming data and WebSocket interaction for the current client.
     */
    void handle_client();

    /**
     * @brief Static wrapper to launch the ping task.
     * @param arg Unused argument (typically nullptr).
     */
    static void ping_task_wrapper(void *arg);

    /**
     * @brief Task that periodically sends ping frames to the client (if enabled).
     */
    void ping_task();

    /**
     * @brief Wi-Fi event handler for IP acquisition.
     * @param arg Unused.
     * @param event_base Event base (e.g., IP_EVENT).
     * @param event_id Event ID (e.g., IP_EVENT_STA_GOT_IP).
     * @param event_data Pointer to event-specific data.
     */
    static void got_ip_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data);

    /**
     * @brief Sends the WebSocket handshake response to a new client.
     * @param sock Socket descriptor of the client.
     * @param request Raw HTTP upgrade request.
     * @return esp_err_t ESP_OK on success, ESP_FAIL otherwise.
     */
    static esp_err_t send_handshake_response(int sock, const char *request);

    /**
     * @brief Static receive buffer used for reading frames.
     * Memory is allocated statically with WS_MAX_FRAME_SIZE.
     */
    uint8_t rxBuffer[WS_MAX_FRAME_SIZE];

    /**
     * @brief Static transmit buffer used for sending frames.
     * Memory is allocated statically with WS_MAX_FRAME_SIZE.
     */
    uint8_t txBuffer[WS_MAX_FRAME_SIZE];

    /**
     * @brief Initializes the Wi-Fi stack in either AP or STA mode.
     * @param ssid SSID of the network.
     * @param password Network password.
     * @param isAp Whether to use Access Point mode.
     * @param extra_config Optional configuration function to be called after init.
     * @return esp_err_t ESP_OK on success.
     */
    esp_err_t wifi_init(const char *ssid,
                        const char *password,
                        bool isAp,
                        std::function<void()> extra_config);

    /**
     * @brief Reads a WebSocket frame from a socket.
     * @param sock Socket to read from.
     * @param buf Destination buffer.
     * @param bufsize Size of the buffer.
     * @return int Number of bytes read, or -1 on error.
     */
    int readFrame(int sock, uint8_t *buf, size_t bufsize);

    /**
     * @brief Decodes a WebSocket frame in-place from a buffer.
     * @param buf Pointer to the raw frame.
     * @param length Length of the frame.
     * @param opcode [out] Type of frame (text, binary, etc.).
     * @param payload [out] Pointer to extracted payload.
     * @param payloadLen [out] Length of payload.
     * @return true if decoding succeeded.
     */
    bool decodeFrameInPlace(uint8_t *buf, int length,
                            ws_type_t &opcode,
                            uint8_t *&payload,
                            size_t &payloadLen);

    /**
     * @brief Sends a WebSocket frame to a client.
     * @param sock Target socket.
     * @param data Pointer to data.
     * @param length Length of data.
     * @param opcode Frame type (text, binary, etc.).
     * @param fin Whether this is the final frame in a message (default: true).
     */
    void sendFrame(int sock,
                   const uint8_t *data,
                   size_t length,
                   ws_type_t opcode,
                   bool fin = true);

    /**
     * @brief Callback for received text messages.
     */
    std::function<void(int, const std::string &)> text_message_cb;

    /**
     * @brief Callback for received binary messages.
     */
    std::function<void(int, uint8_t *, size_t)> binary_message_cb;

    /**
     * @brief Callback for received ping frames.
     */
    std::function<void(int)> ping_cb;

    /**
     * @brief Callback for received pong frames.
     */
    std::function<void(int)> pong_cb;

    /**
     * @brief Callback when a client initiates a close.
     */
    std::function<void(int)> close_cb;

    /**
     * @brief Callback when a new client connects.
     */
    std::function<void(int)> client_connected_cb;

    /**
     * @brief Callback when the client disconnects.
     */
    std::function<void(int)> client_disconnected_cb;

    /**
     * @brief Server listening socket.
     */
    int server_sock;

    /**
     * @brief Socket of the currently connected client.
     */
    int client_sock;

    /**
     * @brief Whether the server is operating in AP mode.
     */
    bool isAp;

    /**
     * @brief Whether ping/pong mechanism is enabled.
     */
    bool ping_pong_enabled;

    /**
     * @brief Ping interval in milliseconds (0 disables).
     */
    uint64_t ping_interval_ms;

    /**
     * @brief Max inactivity time before disconnection.
     */
    uint64_t max_inactivity_ms;

    /**
     * @brief TCP port on which the server listens.
     */
    uint16_t port;
};
