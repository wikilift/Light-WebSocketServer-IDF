#include "ws_light_server.h"

/**
 * This function initializes and starts the WebSocket server with default parameters.
 * The server will start in Access Point mode with the default SSID "default_ssid" and 
 * password "default_password". Ping to client is enabled by default.
 */
extern "C" void app_main(void) {
    WSLightServer &server = WSLightServer::getInstance();
    server.start();
}