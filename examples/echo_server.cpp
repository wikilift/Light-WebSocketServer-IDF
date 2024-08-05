
#include "ws_light_server.h"

/**
 * This function initializes and starts the WebSocket server with default parameters.
 * The server will start in Access Point mode with the default SSID "default_ssid" and 
 * password "default_password".
 * Ping to client is enabled by default.
 * It also sets up the necessary callbacks to handle incoming WebSocket messages and 
 * implements an echo functionality.
 */
extern "C" void app_main(void) {
    WSLightServer &server = WSLightServer::getInstance();

    // Start the WebSocket server
    server.start("default_ssid", "default_password", 8080, 25000, 60000, true, []()
                 { ESP_LOGW("WSLightServer", "Test extra config"); });

    // Set the callback for text messages
    server.onTextMessage([&server](int client_sock, const std::string &message) {
        ESP_LOGI("EchoServer", "Received text message: %s", message.c_str());
        server.sendTextMessage(message);
    });

    // Set the callback for binary messages
    server.onBinaryMessage([&server](int client_sock, const std::vector<uint8_t> &message) {
        ESP_LOGI("EchoServer", "Received binary message");
        esp_log_buffer_hex("Received WS binary", message.data(), message.size());
        server.sendBinaryMessage(message.data(), message.size());
    });

    // Set the callback for when a client connects
    server.onClientConnected([](int sockfd) {
        ESP_LOGI("EchoServer", "Client connected: %d", sockfd);
    });

    // Set the callback for when a client disconnects
    server.onClientDisconnected([](int sockfd) {
        ESP_LOGI("EchoServer", "Client disconnected: %d", sockfd);
    });
}