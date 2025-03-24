#include "ws_light_server.h"

/**
 * @brief Minimal startup example for the WebSocket server.
 *
 * This function initializes and starts the WebSocket server using default parameters.
 * By default:
 * - The server runs in Access Point mode.
 * - SSID: "Wikilift ssid"
 * - Password: "myAwesomePwd123456"
 * - Port: 80
 * - Ping interval: 6000 ms
 * - Inactivity timeout: 50000 ms
 * - Ping/pong keep-alive is enabled.
 *
 * No callbacks are registered in this minimal example.
 */
extern "C" void app_main(void) {
    WSLightServer &server = WSLightServer::getInstance();
    server.start();
}
