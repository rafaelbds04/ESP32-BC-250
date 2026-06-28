/**
 * @file gamepad_control.h
 * @brief Integração Bluetooth com gamepads via Bluepad32 + Filtro por MAC Address
 *
 * Suporta uma ampla gama de controles Bluetooth, incluindo:
 *   - Sony DualSense (PS5) e DualShock 4 (PS4)
 *   - Xbox Wireless Controller (Series X|S, One)
 *   - Nintendo Switch Pro Controller
 *   - 8BitDo Ultimate, SN30 Pro+, etc.
 *   - Gamepads genéricos HID
 *
 * RECURSO DE FILTRO POR MAC:
 *   Permite configurar um MAC de controle específico (ex: PS5 DualSense ou 8BitDo).
 *   Quando configurado, apenas o controle com aquele MAC responderá ao botão Home/PS.
 *   Se não houver MAC configurado (vazio), qualquer controle conectado pode acionar a fonte.
 *   As configurações são salvas de forma persistente na memória NVS (Preferences).
 */

#ifndef GAMEPAD_CONTROL_H
#define GAMEPAD_CONTROL_H

#include <Bluepad32.h>
#include <Preferences.h>
#include "config.h"
#include "psu_control.h"

// =============================================================================
// ESTADO DO GAMEPAD & ARMAZENAMENTO PERSISTENTE
// =============================================================================

/** Instância do NVS / Preferences */
Preferences preferences;

/** MAC Address do controle permitido (ex: "AA:BB:CC:DD:EE:FF" ou "" para todos) */
String targetGamepadMac = "";

/** Array de ponteiros para controles conectados (ControllerPtr = Controller*) */
ControllerPtr myControllers[BP32_MAX_GAMEPADS];

/** Timestamp do último toggle via gamepad (cooldown) */
unsigned long lastGamepadToggle = 0;

/** Flag para log de primeiro botão detectado (debug) */
bool gamepadFirstPress = true;

// =============================================================================
// FUNÇÕES AUXILIARES DE MAC & CONFIGURAÇÃO
// =============================================================================

/**
 * @brief Obtém o MAC Address formatado em String (AA:BB:CC:DD:EE:FF) de um controle
 */
String getControllerMacAddress(ControllerPtr ctl) {
    if (ctl == nullptr) return "";
    ControllerProperties props = ctl->getProperties();
    char buf[18];
    snprintf(buf, sizeof(buf), "%02X:%02X:%02X:%02X:%02X:%02X",
             props.btaddr[0], props.btaddr[1], props.btaddr[2],
             props.btaddr[3], props.btaddr[4], props.btaddr[5]);
    return String(buf);
}

/**
 * @brief Carrega as configurações do gamepad gravadas na NVS
 */
void loadGamepadConfig() {
    preferences.begin("bc250", true); // Modo leitura
    targetGamepadMac = preferences.getString("target_mac", "");
    preferences.end();
    targetGamepadMac.toUpperCase();
    targetGamepadMac.trim();
    if (targetGamepadMac.length() > 0) {
        Serial.printf("[GAMEPAD] Filtro de MAC ativo: '%s'\n", targetGamepadMac.c_str());
    } else {
        Serial.println("[GAMEPAD] Filtro de MAC desativado (qualquer controle pode acionar)");
    }
}

/**
 * @brief Salva o MAC Address alvo na memória NVS
 * @param mac String com o MAC ou "clear"/"all" para remover o filtro
 */
bool saveTargetMac(String mac) {
    mac.toUpperCase();
    mac.trim();
    if (mac == "CLEAR" || mac == "NONE" || mac == "ALL" || mac == "TODOS") {
        mac = "";
    }

    preferences.begin("bc250", false); // Modo escrita
    preferences.putString("target_mac", mac);
    preferences.end();

    targetGamepadMac = mac;
    Serial.printf("[GAMEPAD] Novo MAC salvo na NVS: '%s'\n",
                  targetGamepadMac.length() > 0 ? targetGamepadMac.c_str() : "TODOS (Sem Filtro)");
    return true;
}

// =============================================================================
// CALLBACKS DO BLUEPAD32
// =============================================================================

/**
 * @brief Callback chamado quando um controle BT é conectado
 */
void onConnectedController(ControllerPtr ctl) {
    bool foundSlot = false;
    for (int i = 0; i < BP32_MAX_GAMEPADS; i++) {
        if (myControllers[i] == nullptr) {
            myControllers[i] = ctl;
            foundSlot = true;
            String mac = getControllerMacAddress(ctl);
            Serial.printf("[GAMEPAD] Conectado no slot %d\n", i);
            Serial.printf("[GAMEPAD]   Modelo: %s\n", ctl->getModelName().c_str());
            Serial.printf("[GAMEPAD]   MAC:    %s\n", mac.c_str());
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
 * @brief Inicializa o Bluepad32 e carrega configurações
 */
void initGamepad() {
    // Carregar configurações de MAC salvas na NVS
    loadGamepadConfig();

    // Inicializar array
    for (int i = 0; i < BP32_MAX_GAMEPADS; i++) {
        myControllers[i] = nullptr;
    }

    // Registrar callbacks
    BP32.setup(&onConnectedController, &onDisconnectedController);

    // Permitir reconexão e novas conexões Bluetooth agora que o Wi-Fi já conectou em STA mode
    BP32.enableNewBluetoothConnections(true);

    // Permitir reconexão automática de dispositivos já pareados
    BP32.forgetBluetoothKeys();

    Serial.println("[GAMEPAD] Bluepad32 inicializado — aguardando conexões BT...");
    Serial.printf("[GAMEPAD]   Bluepad32 FW: %s\n", BP32.firmwareVersion());
}

// =============================================================================
// HANDLER DO LOOP (NON-BLOCKING)
// =============================================================================

/**
 * @brief Processa input dos controles conectados (chamado no loop)
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
        bool homePressed = ctl->miscSystem();

        if (homePressed && (now - lastGamepadToggle >= GAMEPAD_COOLDOWN_MS)) {
            lastGamepadToggle = now;

            String ctlMac = getControllerMacAddress(ctl);

            // Se houver um MAC específico configurado, ignora outros controles
            if (targetGamepadMac.length() > 0 && !ctlMac.equalsIgnoreCase(targetGamepadMac)) {
                Serial.printf("[GAMEPAD] Ignorado press do MAC %s (filtro ativo: %s)\n",
                              ctlMac.c_str(), targetGamepadMac.c_str());
                continue;
            }

            if (gamepadFirstPress) {
                gamepadFirstPress = false;
                Serial.printf("[GAMEPAD] Press aceito do MAC %s (%s)\n",
                              ctlMac.c_str(), ctl->getModelName().c_str());
            }

            togglePSU("Gamepad BT");
        }
    }
}

#endif // GAMEPAD_CONTROL_H
