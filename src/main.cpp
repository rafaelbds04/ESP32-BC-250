/**
 * =============================================================================
 * ESP32-BC-250 — Controlador Inteligente para Fonte ATX BC-250
 * =============================================================================
 *
 * Firmware completo que unifica QUATRO métodos de controle para a fonte ATX
 * BC-250 em uma única máquina de estados non-blocking:
 *
 *   1. BOTÃO FÍSICO  — GPIO 32 (ISR FALLING + debounce 50ms)
 *   2. INTERFACE WEB  — ESPAsyncWebServer (dark-mode, mobile-friendly)
 *   3. ALEXA          — FauxmoESP (emulação Philips Hue local)
 *   4. GAMEPAD BT     — Bluepad32 (DualSense, Xbox, 8BitDo, etc.)
 *
 * ARQUITETURA:
 *   - psuState (volatile bool) é a FONTE ÚNICA DE VERDADE
 *   - Todos os métodos convergem para setPSUState() / togglePSU()
 *   - Zero delay() no loop — tudo via millis() e callbacks
 *   - Módulos separados em headers para organização
 *
 * HARDWARE: ESP32-WROOM-32D (4MB)
 *   GPIO 26 → PS-ON (via transistor NPN + R 2.2kΩ)
 *   GPIO 27 → LED de status
 *   GPIO 32 → Botão físico (INPUT_PULLUP, ao GND)
 *   GPIO 34 → Entrada lógica opcional (Power Good)
 *
 * @author  ESP32-BC-250 Project
 * @version 1.0.0
 * @date    2026-06-28
 */

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiManager.h>  // tzapu/WiFiManager — Portal cativo para config Wi-Fi

// --- Módulos do projeto (ordem importa: config → psu → demais) ---
#include "config.h"
#include "psu_control.h"
#include "web_server.h"
#include "alexa_control.h"
#include "gamepad_control.h"

// =============================================================================
// VARIÁVEIS DO HEARTBEAT (debug)
// =============================================================================

/** Timestamp do último heartbeat no Serial Monitor */
unsigned long lastHeartbeat = 0;

// =============================================================================
// SETUP — Inicialização sequencial dos módulos
// =============================================================================

void setup() {
    // -------------------------------------------------------------------------
    // 1. SERIAL — Primeira coisa, para que tudo possa logar
    // -------------------------------------------------------------------------
    Serial.begin(115200);
    delay(100);  // Único delay permitido: espera o Serial estabilizar
    Serial.println();
    Serial.println("=============================================");
    Serial.printf("  ESP32-BC-250 Controller v%s\n", FW_VERSION);
    Serial.println("  Fonte ATX BC-250 — Smart Controller");
    Serial.println("=============================================");
    Serial.printf("[BOOT] Free heap: %d bytes\n", ESP.getFreeHeap());
    Serial.printf("[BOOT] Chip: %s Rev %d\n", ESP.getChipModel(), ESP.getChipRevision());

    // -------------------------------------------------------------------------
    // 2. GPIOs — ANTES de tudo, garante fonte DESLIGADA durante boot
    // -------------------------------------------------------------------------
    initPSUPins();
    Serial.printf("[BOOT] Free heap após GPIOs: %d bytes\n", ESP.getFreeHeap());

    // Desativa escaneamento Bluetooth temporariamente durante a inicialização do Wi-Fi.
    // O BTstack (Bluepad32) inicia automaticamente antes do setup(); desativar a descoberta
    // evita que o rádio 2.4GHz interfira no handshake do Access Point (AP mode).
    BP32.enableNewBluetoothConnections(false);

    // -------------------------------------------------------------------------
    // 3. WIFI — WiFiManager com portal cativo (AP) se não houver rede salva
    // -------------------------------------------------------------------------
    Serial.println("[WIFI] Iniciando WiFiManager...");

    // Bloco de escopo para garantir a destruição da instância do WiFiManager
    {
        WiFiManager wm;

        // Timeout do PORTAL CATIVO: 0 = sem timeout (fica aberto até o user configurar)
        wm.setConfigPortalTimeout(0);

        // Timeout da tentativa de CONEXÃO a uma rede já salva (segundos)
        wm.setConnectTimeout(20);

        // Debug detalhado no Serial
        wm.setDebugOutput(true);

        Serial.println("[WIFI] Chamando autoConnect...");
        Serial.println("[WIFI] Se não houver rede salva, conecte-se ao AP:");
        Serial.printf("[WIFI]   SSID: %s\n", WIFI_AP_NAME);
        Serial.println("[WIFI]   Depois acesse: http://192.168.4.1");

        // autoConnect: tenta a rede salva; se falhar, levanta AP cativo
        if (!wm.autoConnect(WIFI_AP_NAME)) {
            Serial.println("[WIFI] FALHA: Portal encerrado sem conexão. Reiniciando...");
            delay(3000);
            ESP.restart();
        }

        // Garante que o webserver interno do WiFiManager libera a porta 80
        wm.stopWebPortal();
        wm.stopConfigPortal();
    }

    Serial.println("[WIFI] Conectado!");
    Serial.printf("[WIFI] SSID: %s\n", WiFi.SSID().c_str());
    Serial.printf("[WIFI] IP: %s\n", WiFi.localIP().toString().c_str());
    Serial.printf("[WIFI] RSSI: %d dBm\n", WiFi.RSSI());
    Serial.printf("[BOOT] Free heap após WiFi: %d bytes\n", ESP.getFreeHeap());

    // Pequena pausa para garantir a liberação completa do socket TCP da porta 80
    delay(500);

    // -------------------------------------------------------------------------
    // 4. ALEXA — Emulação de dispositivo Philips Hue (FauxmoESP)
    // -------------------------------------------------------------------------
    initAlexa();
    Serial.printf("[BOOT] Free heap após Alexa: %d bytes\n", ESP.getFreeHeap());

    // -------------------------------------------------------------------------
    // 5. WEB SERVER — Interface de controle via browser (compartilha porta 80 com Alexa)
    // -------------------------------------------------------------------------
    initWebServer();
    Serial.printf("[BOOT] Free heap após WebServer: %d bytes\n", ESP.getFreeHeap());

    // -------------------------------------------------------------------------
    // 6. GAMEPAD — Bluepad32 para controles Bluetooth
    // -------------------------------------------------------------------------
    initGamepad();
    Serial.printf("[BOOT] Free heap após Gamepad: %d bytes\n", ESP.getFreeHeap());

    // -------------------------------------------------------------------------
    // BOOT COMPLETO
    // -------------------------------------------------------------------------
    Serial.println();
    Serial.println("=============================================");
    Serial.println("  BOOT COMPLETO — Todos os módulos ativos");
    Serial.println("=============================================");
    Serial.printf("  Web:     http://%s/\n", WiFi.localIP().toString().c_str());
    Serial.printf("  Alexa:   \"%s\"\n", FAUXMO_DEVICE_NAME);
    Serial.println("  Gamepad: Aguardando conexão BT...");
    Serial.println("  Botão:   GPIO 32 (ISR FALLING)");
    #ifdef USE_LOGIC_INPUT
    Serial.println("  Logic:   GPIO 34 (habilitado)");
    #else
    Serial.println("  Logic:   GPIO 34 (desabilitado)");
    #endif
    Serial.printf("  Heap:    %d bytes livres\n", ESP.getFreeHeap());
    Serial.println("=============================================");
    Serial.println();
}

// =============================================================================
// LOOP — Handlers non-blocking de cada módulo
// =============================================================================

/**
 * Loop principal — ZERO delay() bloqueantes.
 *
 * Cada handler verifica internamente se é hora de agir (via millis).
 * A ordem de chamada não afeta a funcionalidade, mas prioriza
 * o botão físico (latência mais baixa) e Alexa (SSDP discovery).
 *
 * Fluxo de um toggle típico:
 *   1. Botão/Web/Alexa/Gamepad detecta ação
 *   2. Chama togglePSU() ou setPSUState()
 *   3. psu_control.h atualiza GPIO 26 + GPIO 27 + psuState
 *   4. Próximo poll da Web UI reflete o novo estado via /status
 */
void loop() {
    // 1. BOTÃO FÍSICO — Maior prioridade (processa flag da ISR)
    handleButtonDebounce();

    // 2. ALEXA — Processa SSDP discovery e comandos (FauxmoESP)
    handleAlexa();

    // 3. GAMEPAD BT — Polling dos controles conectados (Bluepad32)
    handleGamepad();

    // 4. LOGIC INPUT — Leitura periódica do GPIO 34 (se habilitado)
    #ifdef USE_LOGIC_INPUT
    handleLogicInput();
    #endif

    // 5. HEARTBEAT — Log periódico para debug (a cada 30s)
    unsigned long now = millis();
    if (now - lastHeartbeat >= HEARTBEAT_INTERVAL_MS) {
        lastHeartbeat = now;
        Serial.printf("[HEARTBEAT] PSU=%s | Heap=%d | WiFi=%ddBm | Up=%lus\n",
                      psuState ? "ON" : "OFF",
                      ESP.getFreeHeap(),
                      WiFi.RSSI(),
                      now / 1000);
    }
}