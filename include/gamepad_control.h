/**
 * @file gamepad_control.h
 * @brief Integração Bluetooth com gamepads via Bluepad32
 *
 * Suporta uma ampla gama de controles Bluetooth, incluindo:
 *   - Sony DualSense (PS5) e DualShock 4 (PS4)
 *   - Xbox Wireless Controller (Series X|S, One)
 *   - Nintendo Switch Pro Controller
 *   - 8BitDo Ultimate, SN30 Pro+, etc.
 *   - Gamepads genéricos HID
 *
 * LÓGICA:
 *   - Quando o botão Home/PS/Guide é pressionado, executa togglePSU()
 *   - Cooldown de GAMEPAD_COOLDOWN_MS (500ms) evita toggles repetidos
 *   - Suporta até BP32_MAX_GAMEPADS controles simultâneos
 *
 * NOTA TÉCNICA:
 *   Bluepad32 usa BTstack ao invés do Bluedroid padrão. Isso requer
 *   o framework Arduino customizado configurado no platformio.ini.
 *   A coexistência WiFi + BT funciona via time-division multiplexing
 *   no rádio 2.4GHz compartilhado do ESP32.
 *
 * API Bluepad32 v3.7+:
 *   - ControllerPtr (typedef para Controller*) — tipo correto do ponteiro
 *   - BP32_MAX_CONTROLLERS — número máximo de controles simultâneos
 *   - miscSystem() — helper que testa MISC_BUTTON_SYSTEM (Home/PS/Guide)
 */

#ifndef GAMEPAD_CONTROL_H
#define GAMEPAD_CONTROL_H

#include <Bluepad32.h>
#include "config.h"
#include "psu_control.h"

// =============================================================================
// ESTADO DO GAMEPAD
// =============================================================================

/** Array de ponteiros para controles conectados (ControllerPtr = Controller*) */
ControllerPtr myControllers[BP32_MAX_GAMEPADS];

/** Timestamp do último toggle via gamepad (cooldown) */
unsigned long lastGamepadToggle = 0;

/** Flag para log de primeiro botão detectado (debug) */
bool gamepadFirstPress = true;

// =============================================================================
// CALLBACKS DO BLUEPAD32
// =============================================================================

/**
 * @brief Callback chamado quando um controle BT é conectado
 *
 * Armazena o ponteiro do controle no array e loga informações.
 * Bluepad32 gerencia a conexão BT automaticamente.
 */
void onConnectedController(ControllerPtr ctl) {
    bool foundSlot = false;
    for (int i = 0; i < BP32_MAX_GAMEPADS; i++) {
        if (myControllers[i] == nullptr) {
            myControllers[i] = ctl;
            foundSlot = true;
            Serial.printf("[GAMEPAD] Conectado no slot %d\n", i);
            Serial.printf("[GAMEPAD]   Modelo: %s\n", ctl->getModelName().c_str());
            Serial.printf("[GAMEPAD]   Type:   %d\n", ctl->getModel());
            ControllerProperties props = ctl->getProperties();
            Serial.printf("[GAMEPAD]   VID: 0x%04x, PID: 0x%04x\n",
                          props.vendor_id, props.product_id);
            break;
        }
    }
    if (!foundSlot) {
        Serial.println("[GAMEPAD] AVISO: Todos os slots ocupados, conexão ignorada");
    }
}

/**
 * @brief Callback chamado quando um controle BT é desconectado
 *
 * Limpa o slot do controle no array.
 */
void onDisconnectedController(ControllerPtr ctl) {
    for (int i = 0; i < BP32_MAX_GAMEPADS; i++) {
        if (myControllers[i] == ctl) {
            myControllers[i] = nullptr;
            Serial.printf("[GAMEPAD] Desconectado do slot %d\n", i);
            break;
        }
    }
}

// =============================================================================
// INICIALIZAÇÃO
// =============================================================================

/**
 * @brief Inicializa o Bluepad32
 *
 * Configura os callbacks de conexão/desconexão e inicializa
 * o array de controles. O Bluepad32 começa a procurar dispositivos
 * BT automaticamente após BP32.setup().
 *
 * NOTA: Esta função DEVE ser chamada após WiFi estar conectado,
 * pois a coexistência WiFi+BT precisa que o WiFi esteja estável.
 */
void initGamepad() {
    // Inicializar array
    for (int i = 0; i < BP32_MAX_GAMEPADS; i++) {
        myControllers[i] = nullptr;
    }

    // Registrar callbacks
    BP32.setup(&onConnectedController, &onDisconnectedController);

    // Permitir reconexão e novas conexões Bluetooth agora que o Wi-Fi já conectou em STA mode
    BP32.enableNewBluetoothConnections(true);

    // Permitir reconexão automática de dispositivos já pareados
    // true = esquece conexões anteriores (forçar novo pairing)
    // Comente a linha abaixo se quiser manter os dispositivos pareados
    BP32.forgetBluetoothKeys();

    Serial.println("[GAMEPAD] Bluepad32 inicializado — aguardando conexões BT...");
    Serial.printf("[GAMEPAD]   Bluepad32 FW: %s\n", BP32.firmwareVersion());
}

// =============================================================================
// HANDLER DO LOOP (NON-BLOCKING)
// =============================================================================

/**
 * @brief Processa input dos controles conectados (chamado no loop)
 *
 * Para cada controle conectado e com dados disponíveis:
 * 1. Verifica se o botão Home/PS/Guide está pressionado via miscSystem()
 * 2. Se sim, e se o cooldown já passou, executa togglePSU()
 *
 * MAPEAMENTO DO BOTÃO HOME (miscSystem):
 *   Bluepad32 mapeia o botão Home/PS/Guide como MISC_BUTTON_SYSTEM.
 *   O helper miscSystem() testa esse bit automaticamente.
 *
 *   Referência: https://bluepad32.readthedocs.io/en/latest/
 */
void handleGamepad() {
    // Atualiza estado interno do Bluepad32 (non-blocking)
    bool dataUpdated = BP32.update();

    if (!dataUpdated) {
        return;
    }

    unsigned long now = millis();

    for (int i = 0; i < BP32_MAX_GAMEPADS; i++) {
        ControllerPtr ctl = myControllers[i];

        if (ctl == nullptr || !ctl->isConnected() || !ctl->hasData()) {
            continue;
        }

        // Verificar botão Home/PS/Guide via helper miscSystem()
        // miscSystem() testa internamente: miscButtons() & MISC_BUTTON_SYSTEM
        bool homePressed = ctl->miscSystem();

        if (homePressed && (now - lastGamepadToggle >= GAMEPAD_COOLDOWN_MS)) {
            lastGamepadToggle = now;

            // Log detalhado na primeira vez (ajuda debug)
            if (gamepadFirstPress) {
                gamepadFirstPress = false;
                Serial.printf("[GAMEPAD] Primeiro press! miscButtons=0x%04x, model=%s\n",
                              ctl->miscButtons(), ctl->getModelName().c_str());
            }

            togglePSU("Gamepad BT");
        }
    }
}

#endif // GAMEPAD_CONTROL_H
