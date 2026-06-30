/**
 * @file gamepad_control.h
 * @brief Integração Bluetooth com gamepads via Bluepad32 + Filtro por MAC Address + Intercepção HCI
 *
 * Suporta uma ampla gama de controles Bluetooth, incluindo:
 *   - Sony DualSense (PS5) e DualShock 4 (PS4)
 *   - Xbox Wireless Controller (Series X|S, One)
 *   - Nintendo Switch Pro Controller
 *   - 8BitDo Ultimate, SN30 Pro+, etc.
 *   - Gamepads genéricos HID
 *
 * RECURSO DE FILTRO POR MAC & INTERCEPÇÃO DE CONEXÃO (WAKE FUNCTION):
 *   - Permite configurar um MAC de controle específico (ex: 8BitDo ou PS5).
 *   - Quando um MAC está configurado, o ESP32 intercepta tentativas de conexão (HCI_EVENT_CONNECTION_REQUEST).
 *   - Mesmo que o controle não conclua a conexão (falha no pareamento/handshake), apenas o sinal de tentativa
 *     de conexão vindo do MAC autorizado aciona a fonte (togglePSU). Isso resolve problemas de pareamento do 8BitDo 2.4G.
 *   - Se nenhum MAC estiver configurado (vazio), o acionamento funciona por conexão estabelecida (qualquer controle).
 *   - Inclui um sistema de escaneamento de MACs próximos para facilitar a configuração via Web UI.
 */

#ifndef GAMEPAD_CONTROL_H
#define GAMEPAD_CONTROL_H

#include <Bluepad32.h>
#include <Preferences.h>
#include <btstack.h>
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
// LISTA DE MACS DISCOBERTOS RECENTEMENTE (SCANNING)
// =============================================================================

#define MAX_DISCOVERED_MACS 15
String discoveredMacs[MAX_DISCOVERED_MACS];
int discoveredMacsCount = 0;
unsigned long lastMacClearTime = 0;

/**
 * @brief Limpa a lista de MACs descobertos periodicamente para evitar stale data
 */
void clearDiscoveredMacsIfNeeded() {
    unsigned long now = millis();
    if (now - lastMacClearTime > 60000) { // A cada 60s
        lastMacClearTime = now;
        discoveredMacsCount = 0;
    }
}

/**
 * @brief Adiciona um MAC descoberto sem duplicatas
 */
void addDiscoveredMac(String mac) {
    mac.toUpperCase();
    mac.trim();
    if (mac.length() != 17) return;

    clearDiscoveredMacsIfNeeded();

    for (int i = 0; i < discoveredMacsCount; i++) {
        if (discoveredMacs[i].equalsIgnoreCase(mac)) return;
    }

    if (discoveredMacsCount < MAX_DISCOVERED_MACS) {
        discoveredMacs[discoveredMacsCount++] = mac;
    } else {
        // Rotaciona (remove o mais antigo)
        for (int i = 1; i < MAX_DISCOVERED_MACS; i++) {
            discoveredMacs[i - 1] = discoveredMacs[i];
        }
        discoveredMacs[MAX_DISCOVERED_MACS - 1] = mac;
    }
}

// =============================================================================
// FUNÇÕES AUXILIARES DE MAC & CONFIGURAÇÃO
// =============================================================================

#include <uni_hid_device.h>

/**
 * @brief Formata o bd_addr_t do BTstack em String (AA:BB:CC:DD:EE:FF)
 */
String formatMacAddress(const uint8_t* addr) {
    char buf[18];
    snprintf(buf, sizeof(buf), "%02X:%02X:%02X:%02X:%02X:%02X",
             addr[0], addr[1], addr[2], addr[3], addr[4], addr[5]);
    return String(buf);
}

/**
 * @brief Converte uma string formatada de MAC (AA:BB:CC:DD:EE:FF) para bd_addr_t do BTstack
 */
bool parseMacAddressString(String macStr, bd_addr_t addr) {
    macStr.toUpperCase();
    macStr.trim();
    if (macStr.length() != 17) return false;
    
    unsigned int bytes[6];
    int parsed = sscanf(macStr.c_str(), "%X:%X:%X:%X:%X:%X",
                        &bytes[0], &bytes[1], &bytes[2],
                        &bytes[3], &bytes[4], &bytes[5]);
    if (parsed != 6) return false;
    
    for (int i = 0; i < 6; i++) {
        addr[i] = (uint8_t)bytes[i];
    }
    return true;
}

/**
 * @brief Força o Bluepad32/BTstack a iniciar uma conexão Bluetooth ativa para o MAC fornecido
 */
bool forceGamepadConnection(String macStr) {
    bd_addr_t addr;
    if (!parseMacAddressString(macStr, addr)) {
        Serial.println("[GAMEPAD] Erro: MAC inválido para conexão forçada");
        return false;
    }
    
    uni_hid_device_t* d = uni_hid_device_create(addr);
    if (d == nullptr) {
        Serial.println("[GAMEPAD] Erro: falha ao criar instância uni_hid_device_t");
        return false;
    }
    
    Serial.printf("[GAMEPAD] Forçando conexão ativa com o controle MAC: %s\n", macStr.c_str());
    uni_hid_device_connect(d);
    return true;
}

/**
 * @brief Obtém o MAC Address formatado de um controle conectado
 */
String getControllerMacAddress(ControllerPtr ctl) {
    if (ctl == nullptr) return "";
    ControllerProperties props = ctl->getProperties();
    return formatMacAddress(props.btaddr);
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
// INTERCEPÇÃO DE EVENTOS HCI (BTSTACK)
// =============================================================================

btstack_packet_callback_registration_t hci_event_callback_registration;

/**
 * @brief Intercepta pacotes HCI de baixo nível do BTstack
 *
 * Captura tentativas de conexão e varreduras de varredura ativa.
 * Executa o acionamento imediato da fonte se a tentativa vier do MAC cadastrado.
 */
void my_hci_packet_handler(uint8_t packet_type, uint16_t channel, uint8_t *packet, uint16_t size) {
    if (packet_type != HCI_EVENT_PACKET) return;

    uint8_t event = hci_event_packet_get_type(packet);
    bd_addr_t addr;
    bool got_addr = false;

    if (event == HCI_EVENT_CONNECTION_REQUEST) {
        hci_event_connection_request_get_bd_addr(packet, addr);
        got_addr = true;

        String reqMac = formatMacAddress(addr);
        Serial.printf("[GAMEPAD] Tentativa de conexão recebida do MAC: %s\n", reqMac.c_str());

        // Se houver filtro de MAC cadastrado, aciona imediatamente na recepção da tentativa
        // Isso ignora se a negociação L2CAP ou pareamento vai ter sucesso
        if (targetGamepadMac.length() > 0 && reqMac.equalsIgnoreCase(targetGamepadMac)) {
            unsigned long now = millis();
            if (now - lastGamepadToggle >= GAMEPAD_COOLDOWN_MS) {
                lastGamepadToggle = now;
                Serial.printf("[GAMEPAD] WAKE-UP acionado por conexão pendente do MAC alvo: %s!\n", reqMac.c_str());
                togglePSU("Conexão Gamepad BT");
            }
        }
    } 
    else if (event == HCI_EVENT_INQUIRY_RESULT) {
        hci_event_inquiry_result_get_bd_addr(packet, addr);
        got_addr = true;
    } 
    else if (event == HCI_EVENT_INQUIRY_RESULT_WITH_RSSI) {
        hci_event_inquiry_result_with_rssi_get_bd_addr(packet, addr);
        got_addr = true;
    } 
    else if (event == HCI_EVENT_EXTENDED_INQUIRY_RESPONSE) {
        hci_event_extended_inquiry_response_get_bd_addr(packet, addr);
        got_addr = true;
    }

    if (got_addr) {
        String macStr = formatMacAddress(addr);
        addDiscoveredMac(macStr);
    }
}

// =============================================================================
// CALLBACKS DO BLUEPAD32
// =============================================================================

/**
 * @brief Callback chamado quando um controle BT é conectado com sucesso
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
 * @brief Inicializa o Bluepad32, carrega configurações e registra intercepção HCI
 */
void initGamepad() {
    loadGamepadConfig();

    for (int i = 0; i < BP32_MAX_GAMEPADS; i++) {
        myControllers[i] = nullptr;
    }

    // Configurar o Bluepad32
    BP32.setup(&onConnectedController, &onDisconnectedController);
    BP32.enableNewBluetoothConnections(true);
    BP32.forgetBluetoothKeys();

    // Registrar o manipulador de pacotes HCI para capturar conexões pendentes e scans
    hci_event_callback_registration.callback = &my_hci_packet_handler;
    hci_add_event_handler(&hci_event_callback_registration);

    Serial.println("[GAMEPAD] Bluepad32 + Interceptador HCI ativo — aguardando sinais BT...");
}

// =============================================================================
// HANDLER DO LOOP (NON-BLOCKING)
// =============================================================================

/**
 * @brief Processa input dos controles conectados com sucesso
 */
void handleGamepad() {
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

        bool homePressed = ctl->miscSystem();

        if (homePressed && (now - lastGamepadToggle >= GAMEPAD_COOLDOWN_MS)) {
            lastGamepadToggle = now;
            String ctlMac = getControllerMacAddress(ctl);

            if (targetGamepadMac.length() > 0 && !ctlMac.equalsIgnoreCase(targetGamepadMac)) {
                continue;
            }

            if (gamepadFirstPress) {
                gamepadFirstPress = false;
                Serial.printf("[GAMEPAD] Botão Home pressionado por controle conectado: %s\n", ctlMac.c_str());
            }

            togglePSU("Gamepad BT");
        }
    }
}

#endif // GAMEPAD_CONTROL_H
