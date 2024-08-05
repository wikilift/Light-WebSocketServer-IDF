/**
 * @file ws_types.h
 * @brief WebSocket Types and Client Info Definitions.
 *
 *@author Daniel Giménez
 *@date 2024-08-05
 *
 * This file contains the definitions for WebSocket frame types and client information.
 */

#pragma once

/**
 * @enum ws_type_t
 * @brief Enumeration of WebSocket frame types.
 */
typedef enum {
    HTTPD_WS_TYPE_CONTINUE   = 0x0,  /**< Continuation frame. */
    HTTPD_WS_TYPE_TEXT       = 0x1,  /**< Text frame. */
    HTTPD_WS_TYPE_BINARY     = 0x2,  /**< Binary frame. */
    HTTPD_WS_TYPE_CLOSE      = 0x8,  /**< Close frame. */
    HTTPD_WS_TYPE_PING       = 0x9,  /**< Ping frame. */
    HTTPD_WS_TYPE_PONG       = 0xA   /**< Pong frame. */
} ws_type_t;

/**
 * @enum ws_client_info_t
 * @brief Enumeration of client information types.
 */
typedef enum {
    HTTPD_WS_CLIENT_INVALID        = 0x0,  /**< Invalid client. */
    HTTPD_WS_CLIENT_HTTP           = 0x1,  /**< HTTP client. */
    HTTPD_WS_CLIENT_WEBSOCKET      = 0x2   /**< WebSocket client. */
} ws_client_info_t;
