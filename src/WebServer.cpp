/**
 * @file WebServer.cpp
 * @brief Async Web Server Implementation with Real-time Telemetry Dashboard
 */

#include "WebServer.h"
#include "Config.h"
#include <Arduino.h>
#include <ArduinoJson.h>

/* ============================================================================
 * INLINE HTML / JAVASCROPT SINGLE-PAGE APPLICATION
 * ============================================================================ */

const char* ApparatusWebServer::INDEX_HTML = R"HTML(
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>The Apparatus - Control Dashboard</title>
    <style>
        * { box-sizing: border-box; margin: 0; padding: 0; }
        body { 
            font-family: 'Cormorant Garamond', Georgia, serif; 
            background: #F9F7F2; 
            color: #113329; 
            line-height: 1.6;
        }
        .container { max-width: 1200px; margin: 0 auto; padding: 20px; }
        header { text-align: center; padding: 30px 0; border-bottom: 2px solid #D4AF37; margin-bottom: 30px; }
        h1 { font-size: 2.5rem; color: #113329; letter-spacing: 2px; }
        .subtitle { color: #666; font-style: italic; margin-top: 10px; }
        
        .grid { display: grid; grid-template-columns: repeat(auto-fit, minmax(350px, 1fr)); gap: 20px; }
        .panel { 
            background: white; 
            border: 1px solid #e0e0e0; 
            border-radius: 8px; 
            padding: 20px; 
            box-shadow: 0 2px 4px rgba(0,0,0,0.05);
        }
        .panel h2 { 
            font-size: 1.3rem; 
            color: #113329; 
            border-bottom: 1px solid #D4AF37; 
            padding-bottom: 10px; 
            margin-bottom: 15px; 
        }
        
        /* State Display */
        .state-display { text-align: center; padding: 20px; }
        .state-badge { 
            display: inline-block; 
            padding: 15px 40px; 
            border-radius: 50px; 
            font-size: 1.5rem; 
            font-weight: 600; 
            letter-spacing: 2px;
            text-transform: uppercase;
            transition: all 0.3s ease;
        }
        .state-0 { background: #e8f5e9; color: #2e7d32; border: 2px solid #a5d6a7; }
        .state-1 { background: #e3f2fd; color: #1565c0; border: 2px solid #90caf9; }
        .state-2 { background: #fff3e0; color: #e65100; border: 2px solid #ffb74d; }
        .state-3 { background: #fce4ec; color: #c62828; border: 2px solid #f8bbd0; animation: pulse 1s infinite; }
        @keyframes pulse { 0%, 100% { transform: scale(1); } 50% { transform: scale(1.02); } }
        
        /* Metrics */
        .metrics { display: grid; grid-template-columns: repeat(2, 1fr); gap: 15px; }
        .metric { background: #fafafa; padding: 15px; border-radius: 6px; border-left: 3px solid #D4AF37; }
        .metric-label { font-size: 0.8rem; color: #666; text-transform: uppercase; letter-spacing: 1px; }
        .metric-value { font-size: 1.5rem; font-weight: 600; color: #113329; font-family: monospace; }
        
        /* Bar Graph - Stationary Energy */
        .energy-bars { display: flex; flex-direction: column; gap: 8px; }
        .energy-bar-row { display: flex; align-items: center; gap: 10px; }
        .energy-bar-label { width: 50px; font-size: 0.8rem; color: #666; font-family: monospace; }
        .energy-bar-container { flex: 1; height: 24px; background: #f0f0f0; border-radius: 12px; overflow: hidden; position: relative; }
        .energy-bar { height: 100%; background: linear-gradient(90deg, #D4AF37, #e8c56d); border-radius: 12px; transition: width 100ms ease; }
        .energy-bar-value { width: 60px; text-align: right; font-size: 0.75rem; font-family: monospace; color: #666; }
        
        /* Oscilloscope Canvas */
        .scope-container { position: relative; }
        .scope-canvas { width: 100%; height: 200px; background: #fafafa; border: 1px solid #e0e0e0; border-radius: 4px; }
        .scope-legend { display: flex; justify-content: space-around; margin-top: 10px; font-size: 0.75rem; color: #666; }
        .scope-legend span { display: flex; align-items: center; gap: 5px; }
        .scope-legend .color { width: 16px; height: 3px; border-radius: 2px; }
        .color-biquad { background: #1565c0; }
        .color-agc { background: #e65100; }
        
        /* Config Form */
        .config-form { display: grid; grid-template-columns: repeat(2, 1fr); gap: 15px; }
        .form-group { display: flex; flex-direction: column; gap: 5px; }
        .form-group.full { grid-column: span 2; }
        label { font-size: 0.8rem; color: #666; text-transform: uppercase; letter-spacing: 1px; }
        input[type="number"], input[type="text"], select { 
            padding: 10px; 
            border: 1px solid #ddd; 
            border-radius: 4px; 
            font-family: inherit; 
            font-size: 1rem;
            background: white;
        }
        input:focus, select:focus { outline: none; border-color: #D4AF37; box-shadow: 0 0 0 2px rgba(212,175,55,0.2); }
        .btn { 
            grid-column: span 2; 
            padding: 12px; 
            background: #113329; 
            color: #F9F7F2; 
            border: none; 
            border-radius: 4px; 
            font-family: inherit; 
            font-size: 1rem; 
            cursor: pointer; 
            transition: background 0.2s;
        }
        .btn:hover { background: #0d261e; }
        .btn:disabled { background: #999; cursor: not-allowed; }
        .btn-secondary { background: #666; }
        .btn-secondary:hover { background: #555; }
        
        /* Connection Status */
        .connection-status { 
            display: flex; 
            align-items: center; 
            justify-content: center; 
            gap: 10px; 
            padding: 10px; 
            margin-bottom: 20px; 
            border-radius: 4px; 
            font-size: 0.9rem;
        }
        .connected { background: #e8f5e9; color: #2e7d32; }
        .disconnected { background: #fce4ec; color: #c62828; }
        .status-dot { width: 10px; height: 10px; border-radius: 50%; }
        .status-dot.connected { background: #4caf50; }
        .status-dot.disconnected { background: #f44336; }
        
        @media (max-width: 768px) {
            .config-form { grid-template-columns: 1fr; }
            .form-group.full, .btn { grid-column: span 1; }
            .metrics { grid-template-columns: 1fr; }
        }
    </style>
</head>
<body>
    <div class="container">
        <header>
            <h1>The Apparatus</h1>
            <p class="subtitle">Interactive Multimedia Art Installation — Control Dashboard</p>
        </header>
        
        <div class="connection-status disconnected" id="connectionStatus">
            <span class="status-dot disconnected"></span>
            <span id="statusText">Connecting...</span>
        </div>
        
        <div class="grid">
            <!-- State & Key Metrics -->
            <div class="panel">
                <h2>System State</h2>
                <div class="state-display">
                    <div class="state-badge state-0" id="stateBadge">IDLE</div>
                    <div class="metrics" style="margin-top: 20px;">
                        <div class="metric">
                            <div class="metric-label">PWM Output</div>
                            <div class="metric-value" id="pwmOutput">0</div>
                        </div>
                        <div class="metric">
                            <div class="metric-label">Pi Trigger</div>
                            <div class="metric-value" id="piTrigger">LOW</div>
                        </div>
                        <div class="metric">
                            <div class="metric-label">Distance (Raw)</div>
                            <div class="metric-value" id="distRaw">— cm</div>
                        </div>
                        <div class="metric">
                            <div class="metric-label">Distance (Filtered)</div>
                            <div class="metric-value" id="distFiltered">— cm</div>
                        </div>
                        <div class="metric">
                            <div class="metric-label">Base PWM</div>
                            <div class="metric-value" id="basePwm">0.00</div>
                        </div>
                        <div class="metric">
                            <div class="metric-label">Gamma Shaped</div>
                            <div class="metric-value" id="gammaShaped">0.000</div>
                        </div>
                    </div>
                </div>
            </div>
            
            <!-- Stationary Energy Bar Graph -->
            <div class="panel">
                <h2>Radar: Stationary Energy (9 Gates × 75 cm)</h2>
                <div class="energy-bars" id="energyBars">
                    <!-- Populated by JS -->
                </div>
            </div>
            
            <!-- Respiration Oscilloscope -->
            <div class="panel" style="grid-column: span 2;">
                <h2>DSP Pipeline: Respiration Extraction</h2>
                <div class="scope-container">
                    <canvas class="scope-canvas" id="scopeCanvas" width="800" height="200"></canvas>
                    <div class="scope-legend">
                        <span><span class="color color-biquad"></span> Biquad Raw (Bandpass 0.1–0.5 Hz)</span>
                        <span><span class="color color-agc"></span> AGC Normalized (−1.0 to +1.0)</span>
                    </div>
                </div>
            </div>
            
            <!-- Configuration Panel -->
            <div class="panel" style="grid-column: span 2;">
                <h2>Calibration Configuration (Saved to NVS)</h2>
                <form class="config-form" id="configForm">
                    <div class="form-group">
                        <label>D_min (cm)</label>
                        <input type="number" id="cfg_D_min" step="1" min="10" max="500" required>
                    </div>
                    <div class="form-group">
                        <label>D_max (cm)</label>
                        <input type="number" id="cfg_D_max" step="1" min="50" max="675" required>
                    </div>
                    <div class="form-group">
                        <label>Hysteresis (cm)</label>
                        <input type="number" id="cfg_hysteresis" step="1" min="0" max="100">
                    </div>
                    <div class="form-group">
                        <label>Gamma Exponent</label>
                        <input type="number" id="cfg_gamma_exponent" step="0.01" min="0.1" max="5.0">
                    </div>
                    <div class="form-group">
                        <label>Breathing Depth M (0–1)</label>
                        <input type="number" id="cfg_breathing_depth_M" step="0.01" min="0" max="1">
                    </div>
                    <div class="form-group">
                        <label>Slew Rate (PWM/ms)</label>
                        <input type="number" id="cfg_slew_rate_limit" step="0.1" min="0.1" max="50">
                    </div>
                    <div class="form-group">
                        <label>PWM Min Clamp</label>
                        <input type="number" id="cfg_pwm_min_clamp" step="1" min="0" max="254">
                    </div>
                    <div class="form-group">
                        <label>PWM Max Clamp</label>
                        <input type="number" id="cfg_pwm_max_clamp" step="1" min="1" max="255">
                    </div>
                    <div class="form-group full">
                        <label>WiFi SSID (AP Mode)</label>
                        <input type="text" id="cfg_wifi_ssid" maxlength="31">
                    </div>
                    <div class="form-group full">
                        <label>WiFi Password (min 8 chars)</label>
                        <input type="text" id="cfg_wifi_password" maxlength="63">
                    </div>
                    <button type="submit" class="btn" id="saveBtn">Save Configuration to NVS</button>
                    <button type="button" class="btn btn-secondary" id="resetBtn">Reset to Defaults</button>
                </form>
                <div id="configStatus" style="margin-top: 15px; min-height: 20px;"></div>
            </div>
        </div>
    </div>

    <script>
        // ===== STATE MANAGEMENT =====
        let ws = null;
        let reconnectAttempts = 0;
        const MAX_RECONNECT_ATTEMPTS = 10;
        const RECONNECT_DELAY = 2000;
        
        // Oscilloscope buffers
        const SCOPE_MAX_POINTS = 200;
        let biquadBuffer = new Array(SCOPE_MAX_POINTS).fill(0);
        let agcBuffer = new Array(SCOPE_MAX_POINTS).fill(0);
        
        // Canvas setup
        const canvas = document.getElementById('scopeCanvas');
        const ctx = canvas.getContext('2d');
        const CANVAS_W = canvas.width;
        const CANVAS_H = canvas.height;
        const CENTER_Y = CANVAS_H / 2;
        const SCALE_Y = CANVAS_H * 0.4; // Scale for ±1.0
        
        // ===== WEBSOCKET =====
        function connectWebSocket() {
            const protocol = window.location.protocol === 'https:' ? 'wss:' : 'ws:';
            const wsUrl = `${protocol}//${window.location.host}/ws`;
            ws = new WebSocket(wsUrl);
            
            ws.onopen = () => {
                console.log('WebSocket connected');
                updateConnectionStatus(true);
                reconnectAttempts = 0;
                // Request current config
                ws.send(JSON.stringify({type: 'get_config'}));
            };
            
            ws.onclose = () => {
                console.log('WebSocket disconnected');
                updateConnectionStatus(false);
                if (reconnectAttempts < MAX_RECONNECT_ATTEMPTS) {
                    reconnectAttempts++;
                    setTimeout(connectWebSocket, RECONNECT_DELAY);
                }
            };
            
            ws.onerror = (err) => {
                console.error('WebSocket error:', err);
            };
            
            ws.onmessage = (event) => {
                try {
                    const data = JSON.parse(event.data);
                    handleMessage(data);
                } catch (e) {
                    console.error('Parse error:', e);
                }
            };
        }
        
        function updateConnectionStatus(connected) {
            const el = document.getElementById('connectionStatus');
            const dot = el.querySelector('.status-dot');
            const text = document.getElementById('statusText');
            if (connected) {
                el.className = 'connection-status connected';
                dot.className = 'status-dot connected';
                text.textContent = 'Connected — Live Telemetry';
            } else {
                el.className = 'connection-status disconnected';
                dot.className = 'status-dot disconnected';
                text.textContent = 'Disconnected — Reconnecting...';
            }
        }
        
        // ===== MESSAGE HANDLING =====
        function handleMessage(data) {
            switch (data.type) {
                case 'telemetry':
                    updateTelemetry(data.payload);
                    break;
                case 'config':
                    updateConfigForm(data.payload);
                    break;
                case 'config_saved':
                    showConfigStatus('Configuration saved to NVS!', 'success');
                    break;
                case 'config_error':
                    showConfigStatus('Error: ' + data.message, 'error');
                    break;
                case 'config_reset':
                    updateConfigForm(data.payload);
                    showConfigStatus('Configuration reset to defaults', 'success');
                    break;
            }
        }
        
        function updateTelemetry(p) {
            // State badge
            const badge = document.getElementById('stateBadge');
            badge.textContent = p.state_name || ['IDLE','MACRO','MICRO','CONTACT'][p.state];
            badge.className = `state-badge state-${p.state}`;
            
            // Metrics
            document.getElementById('pwmOutput').textContent = p.pwm_output;
            document.getElementById('piTrigger').textContent = p.pi_trigger ? 'HIGH' : 'LOW';
            document.getElementById('distRaw').textContent = p.distance_raw + ' cm';
            document.getElementById('distFiltered').textContent = p.distance_filtered.toFixed(1) + ' cm';
            document.getElementById('basePwm').textContent = p.base_pwm.toFixed(2);
            document.getElementById('gammaShaped').textContent = p.gamma_shaped.toFixed(3);
            
            // Energy bars (highlight peak gate used by DSP)
            updateEnergyBars(p.stationary_energy, p.peak_gate);
            
            // Oscilloscope
            updateOscilloscope(p.biquad_raw, p.agc_normalized);
        }
        
        function updateEnergyBars(energy, peakGate) {
            const container = document.getElementById('energyBars');
            const maxEnergy = Math.max(...energy, 1); // Avoid div by zero
            
            container.innerHTML = energy.map((val, i) => {
                const pct = (val / maxEnergy) * 100;
                const gateStart = i * 75;
                const gateEnd = gateStart + 75;
                const isPeak = (i === peakGate);
                return `
                    <div class="energy-bar-row">
                        <div class="energy-bar-label" style="${isPeak ? 'color:#113329;font-weight:bold;' : ''}">G${i}: ${gateStart}-${gateEnd}cm</div>
                        <div class="energy-bar-container">
                            <div class="energy-bar" style="width: ${pct}%; ${isPeak ? 'background: linear-gradient(90deg, #113329, #2e5c4d);' : ''}"></div>
                        </div>
                        <div class="energy-bar-value" style="${isPeak ? 'color:#113329;font-weight:bold;' : ''}">${val}${isPeak ? ' ◄' : ''}</div>
                    </div>
                `;
            }).join('');
        }
        
        function updateOscilloscope(biquadRaw, agcNormalized) {
            // Shift buffers
            biquadBuffer.push(biquadRaw);
            biquadBuffer.shift();
            agcBuffer.push(agcNormalized);
            agcBuffer.shift();
            
            // Draw
            ctx.clearRect(0, 0, CANVAS_W, CANVAS_H);
            
            // Grid lines
            ctx.strokeStyle = '#f0f0f0';
            ctx.lineWidth = 1;
            for (let y = 0; y <= CANVAS_H; y += 25) {
                ctx.beginPath();
                ctx.moveTo(0, y);
                ctx.lineTo(CANVAS_W, y);
                ctx.stroke();
            }
            // Center line
            ctx.strokeStyle = '#ddd';
            ctx.beginPath();
            ctx.moveTo(0, CENTER_Y);
            ctx.lineTo(CANVAS_W, CENTER_Y);
            ctx.stroke();
            
            // Biquad trace (blue)
            ctx.strokeStyle = '#1565c0';
            ctx.lineWidth = 1.5;
            ctx.beginPath();
            biquadBuffer.forEach((val, i) => {
                const x = (i / SCOPE_MAX_POINTS) * CANVAS_W;
                const y = CENTER_Y - val * SCALE_Y * 0.5; // Scale down biquad
                if (i === 0) ctx.moveTo(x, y);
                else ctx.lineTo(x, y);
            });
            ctx.stroke();
            
            // AGC trace (orange)
            ctx.strokeStyle = '#e65100';
            ctx.lineWidth = 2;
            ctx.beginPath();
            agcBuffer.forEach((val, i) => {
                const x = (i / SCOPE_MAX_POINTS) * CANVAS_W;
                const y = CENTER_Y - val * SCALE_Y;
                if (i === 0) ctx.moveTo(x, y);
                else ctx.lineTo(x, y);
            });
            ctx.stroke();
        }
        
        // ===== CONFIG FORM =====
        function updateConfigForm(cfg) {
            document.getElementById('cfg_D_min').value = cfg.D_min;
            document.getElementById('cfg_D_max').value = cfg.D_max;
            document.getElementById('cfg_hysteresis').value = cfg.hysteresis;
            document.getElementById('cfg_gamma_exponent').value = cfg.gamma_exponent;
            document.getElementById('cfg_breathing_depth_M').value = cfg.breathing_depth_M;
            document.getElementById('cfg_slew_rate_limit').value = cfg.slew_rate_limit;
            document.getElementById('cfg_pwm_min_clamp').value = cfg.pwm_min_clamp;
            document.getElementById('cfg_pwm_max_clamp').value = cfg.pwm_max_clamp;
            document.getElementById('cfg_wifi_ssid').value = cfg.wifi_ssid;
            document.getElementById('cfg_wifi_password').value = cfg.wifi_password;
        }
        
        function getConfigFromForm() {
            return {
                D_min: parseFloat(document.getElementById('cfg_D_min').value),
                D_max: parseFloat(document.getElementById('cfg_D_max').value),
                hysteresis: parseFloat(document.getElementById('cfg_hysteresis').value),
                gamma_exponent: parseFloat(document.getElementById('cfg_gamma_exponent').value),
                breathing_depth_M: parseFloat(document.getElementById('cfg_breathing_depth_M').value),
                slew_rate_limit: parseFloat(document.getElementById('cfg_slew_rate_limit').value),
                pwm_min_clamp: parseInt(document.getElementById('cfg_pwm_min_clamp').value),
                pwm_max_clamp: parseInt(document.getElementById('cfg_pwm_max_clamp').value),
                wifi_ssid: document.getElementById('cfg_wifi_ssid').value,
                wifi_password: document.getElementById('cfg_wifi_password').value
            };
        }
        
        function showConfigStatus(message, type) {
            const el = document.getElementById('configStatus');
            el.textContent = message;
            el.style.color = type === 'success' ? '#2e7d32' : '#c62828';
            setTimeout(() => { el.textContent = ''; }, 5000);
        }
        
        document.getElementById('configForm').addEventListener('submit', (e) => {
            e.preventDefault();
            const config = getConfigFromForm();
            ws.send(JSON.stringify({type: 'set_config', payload: config}));
            showConfigStatus('Saving...', 'info');
        });
        
        document.getElementById('resetBtn').addEventListener('click', () => {
            if (confirm('Reset all calibration to factory defaults?')) {
                ws.send(JSON.stringify({type: 'reset_config'}));
            }
        });
        
        // ===== INIT =====
        connectWebSocket();
        
        // Initialize energy bars placeholder
        for (let i = 0; i < 9; i++) {
            const gateStart = i * 75;
            const gateEnd = gateStart + 75;
            document.getElementById('energyBars').innerHTML += `
                <div class="energy-bar-row">
                    <div class="energy-bar-label">G${i}: ${gateStart}-${gateEnd}cm</div>
                    <div class="energy-bar-container"><div class="energy-bar" style="width: 0%"></div></div>
                    <div class="energy-bar-value">0</div>
                </div>
            `;
        }
    </script>
</body>
</html>
)HTML";

/* ============================================================================
 * CONSTRUCTOR / DESTRUCTOR
 * ============================================================================ */

ApparatusWebServer::ApparatusWebServer(uint16_t port) : _port(port) {}

ApparatusWebServer::~ApparatusWebServer() {
    end();
}

/* ============================================================================
 * INITIALIZATION
 * ============================================================================ */

bool ApparatusWebServer::begin() {
    _server = new AsyncWebServer(_port);
    _ws = new AsyncWebSocket("/ws");
    
    if (!_server || !_ws) {
        log_e("Failed to allocate web server objects");
        return false;
    }
    
    // WebSocket event handler
    _ws->onEvent([this](AsyncWebSocket* server, AsyncWebSocketClient* client, 
                        AwsEventType type, void* arg, uint8_t* data, size_t len) {
        _onWsEvent(server, client, type, arg, data, len);
    });
    
    _server->addHandler(_ws);
    
    // HTTP routes
    _setupRoutes();
    
    // Start server
    _server->begin();
    
    log_i("Web server started on port %d", _port);
    log_i("WebSocket endpoint: ws://<ip>/ws");
    
    return true;
}

void ApparatusWebServer::end() {
    if (_ws) {
        _ws->closeAll();
        delete _ws;
        _ws = nullptr;
    }
    if (_server) {
        _server->end();
        delete _server;
        _server = nullptr;
    }
}

/* ============================================================================
 * ROUTE SETUP
 * ============================================================================ */

void ApparatusWebServer::_setupRoutes() {
    // Main page
    _server->on("/", HTTP_GET, [](AsyncWebServerRequest* request) {
        request->send(200, "text/html", INDEX_HTML);
    });
    
    // REST API for config (alternative to WebSocket)
    _server->on("/api/config", HTTP_GET, [this](AsyncWebServerRequest* request) {
        handleConfigGet(request);
    });
    
    _server->on("/api/config", HTTP_POST, [this](AsyncWebServerRequest* request) {
        // Body parsing handled by onRequestBody
    }, nullptr, [this](AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) {
        // Parse JSON body
        JsonDocument doc;
        DeserializationError err = deserializeJson(doc, data, len);
        if (!err) {
            handleConfigSet(request, doc.as<JsonVariant>());
        } else {
            request->send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
        }
    });
    
    // 404
    _server->onNotFound([](AsyncWebServerRequest* request) {
        request->send(404, "text/plain", "Not Found");
    });
}

/* ============================================================================
 * WEBSOCKET EVENT HANDLER
 * ============================================================================ */

void ApparatusWebServer::_onWsEvent(AsyncWebSocket* server, AsyncWebSocketClient* client, 
                                    AwsEventType type, void* arg, uint8_t* data, size_t len) {
    switch (type) {
        case WS_EVT_CONNECT:
            log_i("WebSocket client #%u connected from %s", client->id(), client->remoteIP().toString().c_str());
            {
                std::lock_guard<std::mutex> lock(_ws_mutex);
                _ws_clients.push_back(client);
            }
            _sendConfigToClient(client);
            break;
            
        case WS_EVT_DISCONNECT:
            log_i("WebSocket client #%u disconnected", client->id());
            {
                std::lock_guard<std::mutex> lock(_ws_mutex);
                auto it = std::find(_ws_clients.begin(), _ws_clients.end(), client);
                if (it != _ws_clients.end()) {
                    _ws_clients.erase(it);
                }
            }
            break;
            
        case WS_EVT_DATA:
            _handleWsMessage(client, data, len);
            break;
            
        case WS_EVT_PONG:
        case WS_EVT_ERROR:
            break;
    }
}

void ApparatusWebServer::_handleWsMessage(AsyncWebSocketClient* client, uint8_t* data, size_t len) {
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, data, len);
    if (err) {
        log_w("WebSocket JSON parse error: %s", err.c_str());
        return;
    }
    
    const char* type = doc["type"] | "";
    
    if (strcmp(type, "get_config") == 0) {
        _sendConfigToClient(client);
    } else if (strcmp(type, "set_config") == 0) {
        JsonVariant payload = doc["payload"];
        if (!payload.isNull()) {
            // This will be handled by the main loop via a callback
            // For now, we'll need a callback mechanism
            // The main.cpp will register a config callback
        }
    } else if (strcmp(type, "reset_config") == 0) {
        // Reset to defaults - main loop handles this
    }
}

void ApparatusWebServer::_sendConfigToClient(AsyncWebSocketClient* client) {
    JsonDocument doc;
    doc["type"] = "config";
    
    JsonObject payload = doc["payload"].to<JsonObject>();
    payload["D_min"] = g_config.D_min;
    payload["D_max"] = g_config.D_max;
    payload["hysteresis"] = g_config.hysteresis;
    payload["gamma_exponent"] = g_config.gamma_exponent;
    payload["breathing_depth_M"] = g_config.breathing_depth_M;
    payload["slew_rate_limit"] = g_config.slew_rate_limit;
    payload["pwm_min_clamp"] = g_config.pwm_min_clamp;
    payload["pwm_max_clamp"] = g_config.pwm_max_clamp;
    payload["wifi_ssid"] = g_config.wifi_ssid;
    payload["wifi_password"] = g_config.wifi_password;
    
    String output;
    serializeJson(doc, output);
    client->text(output);
}

void ApparatusWebServer::_broadcastConfig() {
    JsonDocument doc;
    doc["type"] = "config";
    
    JsonObject payload = doc["payload"].to<JsonObject>();
    payload["D_min"] = g_config.D_min;
    payload["D_max"] = g_config.D_max;
    payload["hysteresis"] = g_config.hysteresis;
    payload["gamma_exponent"] = g_config.gamma_exponent;
    payload["breathing_depth_M"] = g_config.breathing_depth_M;
    payload["slew_rate_limit"] = g_config.slew_rate_limit;
    payload["pwm_min_clamp"] = g_config.pwm_min_clamp;
    payload["pwm_max_clamp"] = g_config.pwm_max_clamp;
    payload["wifi_ssid"] = g_config.wifi_ssid;
    payload["wifi_password"] = g_config.wifi_password;
    
    String output;
    serializeJson(doc, output);
    _ws->textAll(output);
}

/* ============================================================================
 * TELEMETRY BROADCAST
 * ============================================================================ */

void ApparatusWebServer::broadcastTelemetry(const TelemetryPacket& packet) {
    uint32_t now = millis();
    if (now - _last_broadcast < _telemetry_interval_ms) {
        return;  // Rate limit
    }
    _last_broadcast = now;
    
    if (_ws_clients.empty()) return;
    
    JsonDocument doc;
    doc["type"] = "telemetry";
    
    JsonObject payload = doc["payload"].to<JsonObject>();
    payload["state"] = packet.state;
    payload["state_name"] = STATE_NAMES[packet.state];
    payload["distance_raw"] = packet.distance_raw;
    payload["distance_filtered"] = packet.distance_filtered;
    
    JsonArray energy_arr = payload["stationary_energy"].to<JsonArray>();
    for (int i = 0; i < RADAR_GATE_COUNT; i++) {
        energy_arr.add(packet.stationary_energy[i]);
    }
    
    payload["biquad_raw"] = packet.biquad_raw;
    payload["agc_normalized"] = packet.agc_normalized;
    payload["pwm_output"] = packet.pwm_output;
    payload["pi_trigger"] = packet.pi_trigger;
    payload["base_pwm"] = packet.base_pwm;
    payload["gamma_shaped"] = packet.gamma_shaped;
    payload["peak_gate"] = packet.peak_gate;
    payload["timestamp_ms"] = packet.timestamp_ms;
    
    String output;
    serializeJson(doc, output);
    
    std::lock_guard<std::mutex> lock(_ws_mutex);
    for (auto* client : _ws_clients) {
        if (client && client->status() == WS_CONNECTED) {
            client->text(output);
        }
    }
}

void ApparatusWebServer::update() {
    // Clean up disconnected clients
    std::lock_guard<std::mutex> lock(_ws_mutex);
    _ws_clients.erase(
        std::remove_if(_ws_clients.begin(), _ws_clients.end(),
            [](AsyncWebSocketClient* c) { return c->status() != WS_CONNECTED; }),
        _ws_clients.end()
    );
}

/* ============================================================================
 * CONFIG HANDLERS (REST API)
 * ============================================================================ */

bool ApparatusWebServer::handleConfigGet(AsyncWebServerRequest* request) {
    JsonDocument doc;
    doc["D_min"] = g_config.D_min;
    doc["D_max"] = g_config.D_max;
    doc["hysteresis"] = g_config.hysteresis;
    doc["gamma_exponent"] = g_config.gamma_exponent;
    doc["breathing_depth_M"] = g_config.breathing_depth_M;
    doc["slew_rate_limit"] = g_config.slew_rate_limit;
    doc["pwm_min_clamp"] = g_config.pwm_min_clamp;
    doc["pwm_max_clamp"] = g_config.pwm_max_clamp;
    doc["wifi_ssid"] = g_config.wifi_ssid;
    doc["wifi_password"] = g_config.wifi_password;
    
    String output;
    serializeJson(doc, output);
    request->send(200, "application/json", output);
    return true;
}

bool ApparatusWebServer::handleConfigSet(AsyncWebServerRequest* request, JsonVariant json) {
    // Update config struct (main loop will save to NVS)
    if (json["D_min"].is<float>()) g_config.D_min = json["D_min"].as<float>();
    if (json["D_max"].is<float>()) g_config.D_max = json["D_max"].as<float>();
    if (json["hysteresis"].is<float>()) g_config.hysteresis = json["hysteresis"].as<float>();
    if (json["gamma_exponent"].is<float>()) g_config.gamma_exponent = json["gamma_exponent"].as<float>();
    if (json["breathing_depth_M"].is<float>()) g_config.breathing_depth_M = json["breathing_depth_M"].as<float>();
    if (json["slew_rate_limit"].is<float>()) g_config.slew_rate_limit = json["slew_rate_limit"].as<float>();
    if (json["pwm_min_clamp"].is<int>()) g_config.pwm_min_clamp = json["pwm_min_clamp"].as<int>();
    if (json["pwm_max_clamp"].is<int>()) g_config.pwm_max_clamp = json["pwm_max_clamp"].as<int>();
    if (json["wifi_ssid"].is<const char*>()) strlcpy(g_config.wifi_ssid, json["wifi_ssid"].as<const char*>(), sizeof(g_config.wifi_ssid));
    if (json["wifi_password"].is<const char*>()) strlcpy(g_config.wifi_password, json["wifi_password"].as<const char*>(), sizeof(g_config.wifi_password));
    
    // Broadcast updated config to all WS clients
    _broadcastConfig();
    
    request->send(200, "application/json", "{\"status\":\"ok\"}");
    return true;
}

void ApparatusWebServer::notifyConfigChanged() {
    _broadcastConfig();
}