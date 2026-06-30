/**
 * @file web_server.h
 * @brief Servidor Web assíncrono com interface dark-mode para controle da PSU e Gamepad BT
 *
 * Usa ESPAsyncWebServer para servir uma interface HTML mobile-friendly.
 * A página é embarcada diretamente no código (sem SPIFFS) para simplicidade.
 *
 * ENDPOINTS:
 *   GET /               → Página HTML principal
 *   GET /toggle         → Alterna estado da PSU, retorna JSON {"state":"ON"/"OFF"}
 *   GET /status         → Retorna JSON com estado atual {"state":"ON"/"OFF"}
 *   GET /gamepad/status → Retorna JSON com MAC alvo, controles conectados e MACs escaneados recentemente
 *   GET /gamepad/save   → Salva novo MAC alvo na memória NVS / Preferences
 */

#ifndef WEB_SERVER_H
#define WEB_SERVER_H

#include <ESPAsyncWebServer.h>
#include <fauxmoESP.h>
#include "config.h"
#include "psu_control.h"
#include "gamepad_control.h"

extern fauxmoESP fauxmo;

// Instância global do servidor web assíncrono
AsyncWebServer server(WEB_SERVER_PORT);

// =============================================================================
// HTML DA INTERFACE WEB (embarcado como string literal)
// =============================================================================

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
            gap: 20px;
            -webkit-tap-highlight-color: transparent;
        }

        /* === CARD PRINCIPAL === */
        .card {
            background: rgba(255, 255, 255, 0.05);
            backdrop-filter: blur(10px);
            -webkit-backdrop-filter: blur(10px);
            border: 1px solid rgba(255, 255, 255, 0.1);
            border-radius: 24px;
            padding: 32px 28px;
            width: 100%;
            max-width: 380px;
            text-align: center;
            box-shadow: 0 8px 32px rgba(0, 0, 0, 0.4);
        }

        /* === HEADER === */
        .logo { font-size: 2.2em; margin-bottom: 4px; }
        h1 { font-size: 1.4em; font-weight: 600; color: #ffffff; margin-bottom: 4px; }
        h2 { font-size: 1.15em; font-weight: 600; color: #ffffff; margin-bottom: 12px; }
        .subtitle { font-size: 0.85em; color: #888; margin-bottom: 24px; }

        /* === STATUS INDICATOR === */
        .status-container { margin-bottom: 24px; }
        .status-dot {
            display: inline-block;
            width: 12px;
            height: 12px;
            border-radius: 50%;
            margin-right: 8px;
            vertical-align: middle;
            transition: all 0.4s ease;
        }
        .status-dot.on { background: #00e676; box-shadow: 0 0 12px rgba(0, 230, 118, 0.6); }
        .status-dot.off { background: #ff5252; box-shadow: 0 0 12px rgba(255, 82, 82, 0.4); }
        .status-text { font-size: 1.1em; font-weight: 500; vertical-align: middle; }

        /* === TOGGLE BUTTON === */
        .toggle-btn {
            position: relative;
            display: inline-block;
            width: 120px;
            height: 60px;
            margin-bottom: 20px;
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
        .power-icon { font-size: 2.4em; margin-bottom: 12px; transition: all 0.3s ease; }
        .power-icon.on { color: #00e676; text-shadow: 0 0 20px rgba(0, 230, 118, 0.5); }
        .power-icon.off { color: #ff5252; text-shadow: 0 0 20px rgba(255, 82, 82, 0.3); }

        /* === INFO FOOTER === */
        .info { margin-top: 16px; padding-top: 16px; border-top: 1px solid rgba(255, 255, 255, 0.08); }
        .info-row { display: flex; justify-content: space-between; font-size: 0.78em; color: #666; margin-bottom: 4px; }
        .info-row span:last-child { color: #999; }

        /* === FORM INPUT & BUTTONS === */
        .input-group { text-align: left; margin-bottom: 14px; }
        .input-label { display: block; font-size: 0.8em; color: #aaa; margin-bottom: 6px; }
        .text-input {
            width: 100%;
            padding: 10px 14px;
            background: rgba(255, 255, 255, 0.07);
            border: 1px solid rgba(255, 255, 255, 0.15);
            border-radius: 10px;
            color: #fff;
            font-family: monospace;
            font-size: 0.95em;
            outline: none;
            transition: border 0.3s ease;
        }
        .text-input:focus { border-color: #00e676; }

        .btn-group { display: flex; gap: 8px; margin-bottom: 12px; }
        .btn {
            flex: 1;
            padding: 10px;
            border: none;
            border-radius: 10px;
            color: #fff;
            font-weight: 600;
            font-size: 0.82em;
            cursor: pointer;
            transition: all 0.2s ease;
        }
        .btn-primary { background: linear-gradient(135deg, #00c853, #009624); }
        .btn-secondary { background: linear-gradient(135deg, #424242, #212121); border: 1px solid rgba(255, 255, 255, 0.1); color: #ccc; }
        .btn:active { transform: scale(0.98); }

        .gamepad-box {
            background: rgba(0,0,0,0.25);
            border-radius: 12px;
            padding: 12px;
            margin-bottom: 12px;
            text-align: left;
            font-size: 0.85em;
            border: 1px solid rgba(255,255,255,0.05);
        }

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
        .toast.show { transform: translateX(-50%) translateY(0); opacity: 1; }
        .loading { pointer-events: none; opacity: 0.6; }
    </style>
</head>
<body>

    <!-- CARD 1: CONTROLE DA FONTE -->
    <div class="card" id="mainCard">
        <div class="logo">⚡</div>
        <h1>BC-250 Controller</h1>
        <p class="subtitle">ESP32 Smart PSU Control</p>

        <div class="power-icon off" id="powerIcon">⏻</div>

        <label class="toggle-btn" id="toggleBtn">
            <input type="checkbox" id="toggleSwitch">
            <span class="toggle-slider"></span>
        </label>

        <div class="status-container">
            <span class="status-dot off" id="statusDot"></span>
            <span class="status-text" id="statusText">DESLIGADA</span>
        </div>

        <div class="info">
            <div class="info-row">
                <span>Firmware</span>
                <span id="fwVersion">v1.0.0</span>
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

    <!-- CARD 2: CONFIGURAÇÃO DO GAMEPAD (PS5 / 8BitDo / Xbox) -->
    <div class="card">
        <div class="logo">🎮</div>
        <h2>Controle Bluetooth</h2>
        <p class="subtitle" style="margin-bottom:16px;">Filtro de Wake-Up por MAC</p>

        <div class="btn-group" style="margin-bottom: 14px;">
            <button id="btnScanMac" class="btn" style="background: linear-gradient(135deg, #1e88e5, #1565c0); width:100%;">🔍 Buscar Controles (10s)</button>
        </div>

        <div class="gamepad-box">
            <div style="color: #888; margin-bottom: 6px; font-size: 0.9em;">Controle Conectado:</div>
            <div id="gamepadList" style="color: #00e676; font-weight: 500;">Nenhum conectado</div>
        </div>

        <div class="gamepad-box">
            <div style="color: #888; margin-bottom: 6px; font-size: 0.9em;">Sinais Bluetooth Próximos / Tentativas:</div>
            <div id="scannedList" style="color: #ffb74d; line-height: 1.4em;">Nenhum sinal detectado. Coloque o controle em pareamento.</div>
        </div>

        <div class="input-group">
            <label class="input-label">MAC Alvo do Controle (Wake Function):</label>
            <input type="text" id="macInput" class="text-input" placeholder="Ex: AA:BB:CC:DD:EE:FF" maxlength="17">
        </div>

        <div class="btn-group">
            <button id="btnSaveMac" class="btn btn-primary">Salvar MAC</button>
            <button id="btnPairMac" class="btn btn-secondary" style="border: 1px solid #ffb74d; color: #ffb74d;">Forçar Pareamento</button>
        </div>
        <div class="btn-group" style="margin-top:-6px;">
            <button id="btnClearMac" class="btn btn-secondary" style="width:100%;">Permitir Todos (Sem Filtro)</button>
        </div>

        <div id="macStatusBadge" style="font-size: 0.78em; margin-top: 8px; min-height: 20px;">Filtro: Carregando...</div>
    </div>

    <!-- CARD 3: ALEXA & UTILITÁRIOS -->
    <div class="card">
        <div class="logo">🏠</div>
        <h2>Alexa & Utilitários</h2>
        <p class="subtitle" style="margin-bottom:16px;">Configurações do Dispositivo</p>

        <div class="input-group">
            <label class="input-label">Nome do Dispositivo na Alexa:</label>
            <input type="text" id="alexaNameInput" class="text-input" placeholder="Ex: BC, Fonte" maxlength="20">
        </div>

        <div class="btn-group">
            <button id="btnSaveAlexa" class="btn btn-primary" style="width: 100%;">Salvar Nome</button>
        </div>

        <hr style="border: 0; border-top: 1px solid rgba(255,255,255,0.08); margin: 16px 0;">

        <div class="btn-group">
            <button id="btnIdentify" class="btn btn-secondary" style="width: 100%; border: 1px solid #00e676; color: #00e676;">📍 Localizar ESP32 (Piscar LED)</button>
        </div>
    </div>

    <div class="toast" id="toast"></div>

    <script>
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

        function updateUI(state) {
            const isOn = state === 'ON';
            toggleSwitch.checked = isOn;
            statusDot.className = 'status-dot ' + (isOn ? 'on' : 'off');
            statusText.textContent = isOn ? 'LIGADA' : 'DESLIGADA';
            powerIcon.className = 'power-icon ' + (isOn ? 'on' : 'off');
        }

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

        let alexaNameFilled = false;
        async function pollStatus() {
            try {
                const res = await fetch('/status');
                const data = await res.json();
                updateUI(data.state);
                if (data.uptime) uptimeEl.textContent = data.uptime;
                if (data.ip) ipEl.textContent = data.ip;
                if (data.version) document.getElementById('fwVersion').textContent = 'v' + data.version;
                
                // Preenche o input do nome da Alexa apenas uma vez no carregamento da pagina
                if (data.alexa_name && !alexaNameFilled && document.activeElement.id !== 'alexaNameInput') {
                    document.getElementById('alexaNameInput').value = data.alexa_name;
                    alexaNameFilled = true;
                }
            } catch (err) {}
        }

        async function pollGamepadStatus() {
            try {
                const res = await fetch('/gamepad/status');
                const data = await res.json();

                // 1. Atualizar controles totalmente conectados
                const listEl = document.getElementById('gamepadList');
                if (data.controllers && data.controllers.length > 0) {
                    let html = '';
                    data.controllers.forEach(c => {
                        html += `<div><strong>${c.model}</strong><br><span style="font-family:monospace; color:#aaa; font-size:0.9em;">${c.mac}</span> <a href="#" onclick="useMac('${c.mac}'); return false;" style="color:#00e676; font-weight:bold; font-size:0.85em; margin-left:6px; text-decoration:none;">[Usar MAC]</a></div>`;
                    });
                    listEl.innerHTML = html;
                } else {
                    listEl.innerHTML = '<span style="color:#888;">Nenhum controle conectado</span>';
                }

                // 2. Atualizar lista de MACs descobertos / tentativas capturadas via HCI
                const scannedEl = document.getElementById('scannedList');
                if (data.discovered && data.discovered.length > 0) {
                    let html = '';
                    data.discovered.forEach(mac => {
                        html += `<div style="margin-bottom: 4px; display:flex; justify-content:space-between; align-items:center;">
                            <span style="font-family:monospace;">${mac}</span>
                            <a href="#" onclick="useMac('${mac}'); return false;" style="color:#ffb74d; font-weight:bold; font-size:0.85em; text-decoration:none;">[Usar MAC]</a>
                        </div>`;
                    });
                    scannedEl.innerHTML = html;
                } else {
                    scannedEl.innerHTML = '<span style="color:#888;">Nenhum sinal detectado. Coloque o controle em pareamento.</span>';
                }

                // 3. Atualizar status do filtro NVS
                const badge = document.getElementById('macStatusBadge');
                if (data.target_mac && data.target_mac.length > 0) {
                    badge.innerHTML = `<span style="color:#00e676;">🔒 Filtro Ativo: <strong>${data.target_mac}</strong></span>`;
                    if (document.activeElement.id !== 'macInput') {
                        document.getElementById('macInput').value = data.target_mac;
                    }
                } else {
                    badge.innerHTML = `<span style="color:#ffb74d;">🔓 Filtro Desativado (Qualquer controle)</span>`;
                    if (document.activeElement.id !== 'macInput') {
                        document.getElementById('macInput').value = '';
                    }
                }
            } catch(e) {}
        }

        function useMac(mac) {
            document.getElementById('macInput').value = mac;
            showToast('MAC selecionado! Clique em Salvar.');
        }

        let isScanning = false;
        document.getElementById('btnScanMac').addEventListener('click', async () => {
            if (isScanning) return;
            isScanning = true;
            const btn = document.getElementById('btnScanMac');
            btn.style.opacity = '0.5';
            
            showToast('Iniciando varredura de controles Bluetooth... 📡');
            
            try {
                const res = await fetch('/gamepad/scan');
                const data = await res.json();
                if (data.status === 'scanning') {
                    let secondsLeft = 10;
                    const interval = setInterval(() => {
                        secondsLeft--;
                        if (secondsLeft <= 0) {
                            clearInterval(interval);
                            btn.textContent = '🔍 Buscar Controles (10s)';
                            btn.style.opacity = '1';
                            isScanning = false;
                            showToast('Busca concluída! Selecione seu controle na lista. 🎮');
                        } else {
                            btn.textContent = `Buscando... (${secondsLeft}s)`;
                        }
                    }, 1000);
                } else {
                    showToast('Erro ao iniciar varredura ⚠️');
                    btn.style.opacity = '1';
                    isScanning = false;
                }
            } catch(e) {
                showToast('Erro de rede ⚠️');
                btn.style.opacity = '1';
                isScanning = false;
            }
        });

        document.getElementById('btnSaveMac').addEventListener('click', async () => {
            const mac = document.getElementById('macInput').value.trim();
            try {
                const res = await fetch('/gamepad/save?mac=' + encodeURIComponent(mac));
                const data = await res.json();
                showToast(data.target_mac ? 'MAC alvo salvo ✅' : 'Filtro desativado 🔓');
                pollGamepadStatus();
            } catch(e) { showToast('Erro ao salvar MAC ⚠️'); }
        });

        document.getElementById('btnPairMac').addEventListener('click', async () => {
            const mac = document.getElementById('macInput').value.trim();
            if (mac.length !== 17) {
                showToast('Digite um MAC válido primeiro! ⚠️');
                return;
            }
            showToast('Enviando sinal de conexão ao controle... 📡');
            try {
                const res = await fetch('/gamepad/pair?mac=' + encodeURIComponent(mac));
                const data = await res.json();
                if (data.status === 'ok') {
                    showToast('Sinal de pareamento enviado! 🎮');
                } else {
                    showToast('Erro ao enviar sinal de pareamento ⚠️');
                }
            } catch(e) { showToast('Erro de rede ⚠️'); }
        });

        document.getElementById('btnClearMac').addEventListener('click', async () => {
            try {
                const res = await fetch('/gamepad/save?mac=clear');
                const data = await res.json();
                document.getElementById('macInput').value = '';
                showToast('Filtro removido (Permitir Todos) 🔓');
                pollGamepadStatus();
            } catch(e) { showToast('Erro ao limpar MAC ⚠️'); }
        });

        document.getElementById('btnSaveAlexa').addEventListener('click', async () => {
            const name = document.getElementById('alexaNameInput').value.trim();
            if (name.length === 0) {
                showToast('Digite um nome válido para a Alexa! ⚠️');
                return;
            }
            try {
                const res = await fetch('/alexa/save?name=' + encodeURIComponent(name));
                const data = await res.json();
                if (data.status === 'ok') {
                    showToast('Nome da Alexa atualizado! Rediscuta no app Alexa. 🏠');
                } else {
                    showToast('Erro ao atualizar nome ⚠️');
                }
            } catch(e) { showToast('Erro de conexão ⚠️'); }
        });

        document.getElementById('btnIdentify').addEventListener('click', async () => {
            showToast('Piscando LED do ESP32 por 5 segundos... 📍');
            try {
                await fetch('/identify');
            } catch(e) {}
        });

        setInterval(pollStatus, 2000);
        setInterval(pollGamepadStatus, 2500); // Polling rápido para capturar sinais
        pollStatus();
        pollGamepadStatus();

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

void initWebServer() {
    // --- Página principal (GET /) ---
    // Envia o HTML diretamente do flash (PROGMEM) sem gastar RAM alocando no heap!
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
        request->send(200, "text/html", HTML_PAGE);
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
        json += "\"heap\":" + String(ESP.getFreeHeap()) + ",";
        json += "\"alexa_name\":\"" + alexaDeviceName + "\",";
        json += "\"version\":\"" + String(FW_VERSION) + "\"";
        json += "}";
        request->send(200, "application/json", json);
    });

    // --- Gamepad Status (GET /gamepad/status) ---
    server.on("/gamepad/status", HTTP_GET, [](AsyncWebServerRequest *request) {
        String json = "{";
        json += "\"target_mac\":\"" + targetGamepadMac + "\",";
        json += "\"controllers\":[";

        int count = 0;
        for (int i = 0; i < BP32_MAX_GAMEPADS; i++) {
            ControllerPtr ctl = myControllers[i];
            if (ctl != nullptr && ctl->isConnected()) {
                if (count > 0) json += ",";
                json += "{";
                json += "\"index\":" + String(i) + ",";
                json += "\"model\":\"" + ctl->getModelName() + "\",";
                json += "\"mac\":\"" + getControllerMacAddress(ctl) + "\"";
                json += "}";
                count++;
            }
        }
        json += "],";

        // Adiciona a lista de MACs descobertos / interceptados via HCI (Bluetooth clássico)
        json += "\"discovered\":[";
        for (int i = 0; i < discoveredMacsCount; i++) {
            if (i > 0) json += ",";
            json += "\"" + discoveredMacs[i] + "\"";
        }
        json += "]}";

        request->send(200, "application/json", json);
    });

    // --- Gamepad Save MAC (GET /gamepad/save?mac=...) ---
    server.on("/gamepad/save", HTTP_GET, [](AsyncWebServerRequest *request) {
        String mac = "";
        if (request->hasParam("mac")) {
            mac = request->getParam("mac")->value();
        }
        saveTargetMac(mac);
        String json = "{\"status\":\"ok\",\"target_mac\":\"" + targetGamepadMac + "\"}";
        request->send(200, "application/json", json);
    });

    // --- Alexa Save Name (GET /alexa/save?name=...) ---
    server.on("/alexa/save", HTTP_GET, [](AsyncWebServerRequest *request) {
        String name = "";
        if (request->hasParam("name")) {
            name = request->getParam("name")->value();
        }
        bool success = saveAlexaName(name);
        String json = "{\"status\":\"" + String(success ? "ok" : "error") + "\",\"alexa_name\":\"" + alexaDeviceName + "\"}";
        request->send(200, "application/json", json);
    });

    // --- Identify / Locate ESP32 (GET /identify) ---
    server.on("/identify", HTTP_GET, [](AsyncWebServerRequest *request) {
        triggerIdentify();
        request->send(200, "text/plain", "Identifying...");
    });

    // --- Gamepad Force Connection/Pairing (GET /gamepad/pair?mac=...) ---
    server.on("/gamepad/pair", HTTP_GET, [](AsyncWebServerRequest *request) {
        String mac = "";
        if (request->hasParam("mac")) {
            mac = request->getParam("mac")->value();
        }
        bool success = forceGamepadConnection(mac);
        String json = "{\"status\":\"" + String(success ? "ok" : "error") + "\"}";
        request->send(200, "application/json", json);
    });

    // --- Gamepad Start BLE/BT Scan (GET /gamepad/scan) ---
    server.on("/gamepad/scan", HTTP_GET, [](AsyncWebServerRequest *request) {
        bool success = startBluetoothScan();
        String json = "{\"status\":\"" + String(success ? "scanning" : "error") + "\"}";
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
