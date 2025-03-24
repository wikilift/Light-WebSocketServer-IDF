/**
 * @file ws_types.h
 * @brief Definitions for WebSocket frame types and client connection types.
 * 
 * This file contains enumerations used to represent different WebSocket frame types
 * as well as the classification of connected clients.
 * 
 * @author Daniel Giménez
 * @date 2024-08-05
 * @date updated 2025-03-24 - Full revamp
 */

 #pragma once

 /**
  * @enum ws_type_t
  * @brief WebSocket frame type.
  * 
  * Enumerates the different types of WebSocket frames as defined by the protocol.
  */
 typedef enum
 {
     HTTPD_WS_TYPE_CONTINUATION = 0x0, /**< Continuation frame. */
     HTTPD_WS_TYPE_TEXT         = 0x1, /**< Text frame. */
     HTTPD_WS_TYPE_BINARY       = 0x2, /**< Binary frame. */
     HTTPD_WS_TYPE_CLOSE        = 0x8, /**< Close frame. */
     HTTPD_WS_TYPE_PING         = 0x9, /**< Ping frame. */
     HTTPD_WS_TYPE_PONG         = 0xA  /**< Pong frame. */
 } ws_type_t;
 
 /**
  * @enum ws_client_info_t
  * @brief WebSocket client type.
  * 
  * Describes the type of client connected to the server,
  * distinguishing between standard HTTP and active WebSocket connections.
  */
 typedef enum
 {
     HTTPD_WS_CLIENT_INVALID   = 0x0, /**< Invalid or unrecognized client. */
     HTTPD_WS_CLIENT_HTTP      = 0x1, /**< Client connected via plain HTTP. */
     HTTPD_WS_CLIENT_WEBSOCKET = 0x2  /**< Client connected via WebSocket. */
 } ws_client_info_t;
 