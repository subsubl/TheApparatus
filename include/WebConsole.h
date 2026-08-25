/**
 * @file WebConsole.h
 * @brief Async web server + WebSocket control console
 */

#ifndef WEB_CONSOLE_H
#define WEB_CONSOLE_H

#include "PinDefinitions.h"
#include <ESPAsyncWebServer.h>
#include <AsyncTCP.h>
#include <ArduinoJson.h>

class WebConsole {
public:
    WebConsole(uint16_t port = 80);
    ~WebConsole();

    bool begin();
    void end();
    void update();

    // 20 Hz telemetry broadcast
    void broadcastTelemetry(const TelemetryPacket& packet);

    size_t getClientCount();

    // GUI -> firmware command handling is wired via these hooks (set by main)
    void setRelayFireHandler(void (*fn)(uint8_t idx));
    void setRelayStopHandler(void (*fn)(uint8_t idx));

private:
    AsyncWebServer* _server = nullptr;
    AsyncWebSocket* _ws = nullptr;
    uint16_t _port;

    uint32_t _last_broadcast = 0;
    uint16_t _telemetry_interval_ms = 50;

    void (*_relay_fire)(uint8_t) = nullptr;
    void (*_relay_stop)(uint8_t) = nullptr;

    void _setupRoutes();
    void _onWsEvent(AsyncWebSocket* server, AsyncWebSocketClient* client,
                    AwsEventType type, void* arg, uint8_t* data, size_t len);
    void _handleWsMessage(AsyncWebSocketClient* client, uint8_t* data, size_t len);
    void _sendFullConfig(AsyncWebSocketClient* client);
    void _broadcastConfig();
};

#endif // WEB_CONSOLE_H