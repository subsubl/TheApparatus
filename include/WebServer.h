/**
 * @file WebServer.h
 * @brief Async Web Server with WebSocket for Telemetry and Configuration
 * 
 * Serves a single-page application (inline HTML/JS) for real-time monitoring
 * and calibration of The Apparatus.
 */

#ifndef WEB_SERVER_H
#define WEB_SERVER_H

#include "Config.h"
#include <ESPAsyncWebServer.h>
#include <AsyncTCP.h>
#include <ArduinoJson.h>

/* ============================================================================
 * WEB SERVER CLASS
 * ============================================================================ */

class ApparatusWebServer {
public:
    ApparatusWebServer(uint16_t port = 80);
    ~ApparatusWebServer();
    
    // Initialize and start server
    bool begin();
    void end();
    
    // Main loop - call periodically for cleanup
    void update();
    
    // Telemetry broadcasting (called from main loop at 20 Hz)
    void broadcastTelemetry(const TelemetryPacket& packet);
    
    // Configuration management
    bool handleConfigGet(AsyncWebServerRequest* request);
    bool handleConfigSet(AsyncWebServerRequest* request, JsonVariant json);
    void notifyConfigChanged();
    
    // Client count
    size_t getWebSocketClientCount() const { return _ws_clients.size(); }
    
private:
    AsyncWebServer* _server = nullptr;
    AsyncWebSocket* _ws = nullptr;
    uint16_t _port;
    
    // WebSocket clients
    std::vector<AsyncWebSocketClient*> _ws_clients;
    std::mutex _ws_mutex;
    
    // Telemetry timing
    uint32_t _last_broadcast = 0;
    uint16_t _telemetry_interval_ms = 50;  // 20 Hz = 50ms
    
    // Private methods
    void _setupRoutes();
    void _onWsEvent(AsyncWebSocket* server, AsyncWebSocketClient* client, 
                    AwsEventType type, void* arg, uint8_t* data, size_t len);
    void _handleWsMessage(AsyncWebSocketClient* client, uint8_t* data, size_t len);
    void _sendConfigToClient(AsyncWebSocketClient* client);
    void _broadcastConfig();
    
    // Static callback wrappers
    static void _wsEventWrapper(AsyncWebSocket* server, AsyncWebSocketClient* client, 
                                AwsEventType type, void* arg, uint8_t* data, size_t len);
    
    // HTML content
    static const char* INDEX_HTML;
};

#endif // WEB_SERVER_H