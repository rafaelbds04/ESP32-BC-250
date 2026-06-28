/**
 * @file web_server.h
 * @brief Servidor Web assíncrono com interface dark-mode para controle da PSU
 *
 * Usa ESPAsyncWebServer para servir uma interface HTML mobile-friendly.
 * A página é embarcada diretamente no código (sem SPIFFS) para simplicidade.
 *
 * ENDPOINTS:
 *   GET /        → Página HTML com botão toggle e status
 *   GET /toggle  → Alterna estado da PSU, retorna JSON {"state":"ON"/"OFF"}
 *   GET /status  → Retorna JSON com estado atual {"state":"ON"/"OFF"}
 *
 * A interface usa JavaScript fetch() para atualizar o status sem reload,
 * com polling a cada 2 segundos para manter a sincronização.
 */

#ifndef WEB_SERVER_H
#define WEB_SERVER_H

#include <ESPAsyncWebServer.h>
#include <fauxmoESP.h>
#include "config.h"
#include "psu_control.h"

extern fauxmoESP fauxmo;

// Instância global do servidor web assíncrono
AsyncWebServer server(WEB_SERVER_PORT);

// =============================================================================
// HTML DA INTERFACE WEB (embarcado como string literal)
// =============================================================================

/**
 * Interface dark-mode, mobile-friendly, com botão toggle animado.
 *
 * Design:
 * - Fundo gradiente escuro (#0a0a1a → #1a1a2e)
 * - Botão toggle com transição CSS (verde ON, vermelho OFF)
 * - Card com glassmorphism sutil
 * - Polling JavaScript a cada 2s para manter status sincronizado
 * - Responsivo via viewport meta tag
 */
const char HTML_PAGE[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="pt-BR">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0, user-scalable=no">
    <title>ESP32 BC-250 Controller</title>
    <style>
        /* === RESET & BASE === */
        *, *::before, *::after { box-sizing: border-box; margin: 0; padding: 0; }

        body {
            font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, sans-serif;
            background: linear-gradient(135deg, #0a0a1a 0%, #1a1a2e 50%, #16213e 100%);
            color: #e0e0e0;
            min-height: 100vh;
            display: flex;
            flex-direction: column;
            align-items: center;
            justify-content: center;
            padding: 20px;
            -webkit-tap-highlight-color: transparent;
        }

        /* === CARD PRINCIPAL === */
        .card {
            background: rgba(255, 255, 255, 0.05);
            backdrop-filter: blur(10px);
            -webkit-backdrop-filter: blur(10px);
            border: 1px solid rgba(255, 255, 255, 0.1);
            border-radius: 24px;
            padding: 40px 32px;
            width: 100%;
            max-width: 380px;
            text-align: center;
            box-shadow: 0 8px 32px rgba(0, 0, 0, 0.4);
        }

        /* === HEADER === */
        .logo {
            font-size: 2.2em;
            margin-bottom: 4px;
        }

        h1 {
            font-size: 1.4em;
            font-weight: 600;
            color: #ffffff;
            margin-bottom: 4px;
        }

        .subtitle {
            font-size: 0.85em;
            color: #888;
            margin-bottom: 32px;
        }

        /* === STATUS INDICATOR === */
        .status-container {
            margin-bottom: 32px;
        }

        .status-dot {
            display: inline-block;
            width: 12px;
            height: 12px;
            border-radius: 50%;
            margin-right: 8px;
            vertical-align: middle;
            transition: all 0.4s ease;
        }

        .status-dot.on {
            background: #00e676;
            box-shadow: 0 0 12px rgba(0, 230, 118, 0.6);
        }

        .status-dot.off {
            background: #ff5252;
            box-shadow: 0 0 12px rgba(255, 82, 82, 0.4);
        }

        .status-text {
            font-size: 1.1em;
            font-weight: 500;
            vertical-align: middle;
            transition: color 0.3s ease;
        }

        /* === TOGGLE BUTTON === */
        .toggle-btn {
            position: relative;
            display: inline-block;
            width: 120px;
            height: 60px;
            margin-bottom: 24px;
            cursor: pointer;
        }

        .toggle-btn input { display: none; }

        .toggle-slider {
            position: absolute;
            inset: 0;
            background: linear-gradient(135deg, #2a2a3a, #1a1a2a);
            border-radius: 30px;
            border: 2px solid rgba(255, 255, 255, 0.1);
            transition: all 0.4s cubic-bezier(0.4, 0, 0.2, 1);
        }

        .toggle-slider::before {
            content: '';
            position: absolute;
            width: 48px;
            height: 48px;
            left: 4px;
            top: 4px;
            background: linear-gradient(135deg, #ff5252, #d32f2f);
            border-radius: 50%;
            transition: all 0.4s cubic-bezier(0.4, 0, 0.2, 1);
            box-shadow: 0 2px 8px rgba(0, 0, 0, 0.3);
        }

        input:checked + .toggle-slider {
            border-color: rgba(0, 230, 118, 0.3);
            box-shadow: 0 0 20px rgba(0, 230, 118, 0.15);
        }

        input:checked + .toggle-slider::before {
            transform: translateX(60px);
            background: linear-gradient(135deg, #00e676, #00c853);
            box-shadow: 0 2px 12px rgba(0, 230, 118, 0.4);
        }

        /* === POWER ICON === */
        .power-icon {
            font-size: 2.4em;
            margin-bottom: 16px;
            transition: all 0.3s ease;
        }

        .power-icon.on { color: #00e676; text-shadow: 0 0 20px rgba(0, 230, 118, 0.5); }
        .power-icon.off { color: #ff5252; text-shadow: 0 0 20px rgba(255, 82, 82, 0.3); }

        /* === INFO FOOTER === */
        .info {
            margin-top: 16px;
            padding-top: 16px;
            border-top: 1px solid rgba(255, 255, 255, 0.08);
        }

        .info-row {
            display: flex;
            justify-content: space-between;
            font-size: 0.78em;
            color: #666;
            margin-bottom: 4px;
        }

        .info-row span:last-child { color: #999; }

        /* === TOAST NOTIFICATION === */
        .toast {
            position: fixed;
            bottom: 30px;
            left: 50%;
            transform: translateX(-50%) translateY(100px);
            background: rgba(0, 0, 0, 0.85);
            color: #fff;
            padding: 12px 24px;
            border-radius: 12px;
            font-size: 0.9em;
            opacity: 0;
            transition: all 0.3s ease;
            z-index: 1000;
            backdrop-filter: blur(10px);
        }

        .toast.show {
            transform: translateX(-50%) translateY(0);
            opacity: 1;
        }

        /* === LOADING SPINNER === */
        .loading { pointer-events: none; opacity: 0.6; }

        @keyframes pulse {
            0%, 100% { opacity: 1; }
            50% { opacity: 0.5; }
        }

        .pulse { animation: pulse 1.5s ease-in-out infinite; }
    </style>
</head>
<body>
    <div class="card" id="mainCard">
        <div class="logo">⚡</div>
        <h1>BC-250 Controller</h1>
        <p class="subtitle">ESP32 Smart PSU Control</p>

        <!-- Power Icon -->
        <div class="power-icon off" id="powerIcon">⏻</div>

        <!-- Toggle Switch -->
        <label class="toggle-btn" id="toggleBtn">
            <input type="checkbox" id="toggleSwitch">
            <span class="toggle-slider"></span>
        </label>

        <!-- Status Indicator -->
        <div class="status-container">
            <span class="status-dot off" id="statusDot"></span>
            <span class="status-text" id="statusText">DESLIGADA</span>
        </div>

        <!-- Info Footer -->
        <div class="info">
            <div class="info-row">
                <span>Firmware</span>
                <span>v%FW_VERSION%</span>
            </div>
            <div class="info-row">
                <span>Uptime</span>
                <span id="uptime">--</span>
            </div>
            <div class="info-row">
                <span>IP</span>
                <span id="ipAddr">--</span>
            </div>
        </div>
    </div>

    <div class="toast" id="toast"></div>

    <script>
        // === ELEMENTOS DO DOM ===
        const toggleSwitch = document.getElementById('toggleSwitch');
        const toggleBtn = document.getElementById('toggleBtn');
        const statusDot = document.getElementById('statusDot');
        const statusText = document.getElementById('statusText');
        const powerIcon = document.getElementById('powerIcon');
        const mainCard = document.getElementById('mainCard');
        const toast = document.getElementById('toast');
        const uptimeEl = document.getElementById('uptime');
        const ipEl = document.getElementById('ipAddr');

        let isRequesting = false;

        // === ATUALIZA UI COM BASE NO ESTADO ===
        function updateUI(state) {
            const isOn = state === 'ON';
            toggleSwitch.checked = isOn;
            statusDot.className = 'status-dot ' + (isOn ? 'on' : 'off');
            statusText.textContent = isOn ? 'LIGADA' : 'DESLIGADA';
            powerIcon.className = 'power-icon ' + (isOn ? 'on' : 'off');
        }

        // === TOGGLE VIA FETCH (SEM RELOAD) ===
        toggleBtn.addEventListener('click', async function(e) {
            e.preventDefault();
            if (isRequesting) return;
            isRequesting = true;
            mainCard.classList.add('loading');

            try {
                const res = await fetch('/toggle');
                const data = await res.json();
                updateUI(data.state);
                showToast('Fonte ' + (data.state === 'ON' ? 'LIGADA ✅' : 'DESLIGADA 🔴'));
            } catch (err) {
                showToast('Erro de conexão ⚠️');
            } finally {
                isRequesting = false;
                mainCard.classList.remove('loading');
            }
        });

        // === POLLING DE STATUS (A CADA 2s) ===
        async function pollStatus() {
            try {
                const res = await fetch('/status');
                const data = await res.json();
                updateUI(data.state);
                if (data.uptime) uptimeEl.textContent = data.uptime;
                if (data.ip) ipEl.textContent = data.ip;
            } catch (err) {
                // Silencioso — reconecta no próximo poll
            }
        }

        setInterval(pollStatus, 2000);
        pollStatus(); // Primeira chamada imediata

        // === TOAST NOTIFICATION ===
        function showToast(msg) {
            toast.textContent = msg;
            toast.classList.add('show');
            setTimeout(() => toast.classList.remove('show'), 2500);
        }
    </script>
</body>
</html>
)rawliteral";

// =============================================================================
// FUNÇÕES DE UTILIDADE
// =============================================================================

/**
 * @brief Formata o uptime em string legível (Xd Xh Xm Xs)
 */
String formatUptime() {
    unsigned long totalSec = millis() / 1000;
    unsigned long days = totalSec / 86400;
    unsigned long hours = (totalSec % 86400) / 3600;
    unsigned long mins = (totalSec % 3600) / 60;
    unsigned long secs = totalSec % 60;

    String result = "";
    if (days > 0) result += String(days) + "d ";
    if (hours > 0 || days > 0) result += String(hours) + "h ";
    result += String(mins) + "m " + String(secs) + "s";
    return result;
}

// =============================================================================
// INICIALIZAÇÃO DO SERVIDOR WEB
// =============================================================================

/**
 * @brief Configura e inicia o ESPAsyncWebServer
 *
 * Registra os endpoints e inicia o servidor na porta configurada.
 * DEVE ser chamado após o WiFi estar conectado.
 */
void initWebServer() {
    // --- Página principal (GET /) ---
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
        String html = String(HTML_PAGE);
        html.replace("%FW_VERSION%", FW_VERSION);
        request->send(200, "text/html", html);
    });

    // --- Toggle da PSU (GET /toggle) ---
    server.on("/toggle", HTTP_GET, [](AsyncWebServerRequest *request) {
        togglePSU("Web Interface");
        String json = "{\"state\":\"" + String(psuState ? "ON" : "OFF") + "\"}";
        request->send(200, "application/json", json);
    });

    // --- Status da PSU (GET /status) ---
    server.on("/status", HTTP_GET, [](AsyncWebServerRequest *request) {
        String json = "{";
        json += "\"state\":\"" + String(psuState ? "ON" : "OFF") + "\",";
        json += "\"uptime\":\"" + formatUptime() + "\",";
        json += "\"ip\":\"" + WiFi.localIP().toString() + "\",";
        json += "\"rssi\":" + String(WiFi.RSSI()) + ",";
        json += "\"heap\":" + String(ESP.getFreeHeap());
        json += "}";
        request->send(200, "application/json", json);
    });

    // --- Hook para FauxmoESP (Alexa) ---
    server.onRequestBody([](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
        bool isGet = (strcmp(request->methodToString(), "GET") == 0);
        String body = "";
        if (data && len > 0) {
            char *buf = (char *)malloc(len + 1);
            if (buf) {
                memcpy(buf, data, len);
                buf[len] = '\0';
                body = String(buf);
                free(buf);
            }
        }
        if (fauxmo.process(request->client(), isGet, request->url(), body)) {
            return;
        }
    });

    // --- 404 Handler (também repassa requisições da Alexa) ---
    server.onNotFound([](AsyncWebServerRequest *request) {
        bool isGet = (strcmp(request->methodToString(), "GET") == 0);
        String body = "";
        if (request->hasParam("body", true)) {
            body = request->getParam("body", true)->value();
        }
        if (fauxmo.process(request->client(), isGet, request->url(), body)) {
            return;
        }
        request->send(404, "text/plain", "404 — Não encontrado");
    });

    server.begin();
    Serial.printf("[WEB] Servidor iniciado na porta %d\n", WEB_SERVER_PORT);
}

#endif // WEB_SERVER_H
