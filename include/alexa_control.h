/**
 * @file alexa_control.h
 * @brief Integração com Amazon Alexa via FauxmoESP com nome configurável por NVS
 *
 * Emula um dispositivo Philips Hue local na rede (sem necessidade de cloud).
 * O dispositivo aparece automaticamente no app Alexa quando descoberto.
 */

#ifndef ALEXA_CONTROL_H
#define ALEXA_CONTROL_H

#include <fauxmoESP.h>
#include <Preferences.h>
#include "config.h"
#include "psu_control.h"

extern Preferences preferences;

// Instância global do FauxmoESP
fauxmoESP fauxmo;

// Nome do dispositivo Alexa atual
String alexaDeviceName = "";

// =============================================================================
// CONFIGURAÇÃO PERSISTENTE (NVS)
// =============================================================================

/**
 * @brief Carrega o nome do dispositivo Alexa gravado na NVS
 */
void loadAlexaConfig() {
    preferences.begin("bc250", true); // Modo leitura
    alexaDeviceName = preferences.getString("alexa_name", "BC");
    preferences.end();

    alexaDeviceName.trim();
    if (alexaDeviceName.length() == 0) {
        alexaDeviceName = "BC";
    }
}

/**
 * @brief Salva um novo nome de dispositivo para a Alexa na NVS e atualiza o Fauxmo
 */
bool saveAlexaName(String name) {
    name.trim();
    if (name.length() == 0) return false;

    preferences.begin("bc250", false); // Modo escrita
    preferences.putString("alexa_name", name);
    preferences.end();

    String oldName = alexaDeviceName;
    alexaDeviceName = name;

    // FauxmoESP permite renomear o dispositivo de índice 0 em tempo de execução
    fauxmo.renameDevice((unsigned char)0, alexaDeviceName.c_str());

    Serial.printf("[ALEXA] Nome alterado na NVS de '%s' para '%s'\n", oldName.c_str(), alexaDeviceName.c_str());
    return true;
}

// =============================================================================
// INICIALIZAÇÃO
// =============================================================================

/**
 * @brief Configura e inicia o FauxmoESP
 */
void initAlexa() {
    loadAlexaConfig();

    // Não criar servidor TCP próprio (ESPAsyncWebServer já serve na porta 80)
    fauxmo.createServer(false);
    fauxmo.setPort(80);

    // Habilitar descoberta de dispositivos pela Alexa
    fauxmo.enable(true);

    // Registrar o dispositivo com o nome configurado na NVS (ou default "BC")
    fauxmo.addDevice(alexaDeviceName.c_str());

    Serial.printf("[ALEXA] Dispositivo '%s' registrado na pilha local\n", alexaDeviceName.c_str());

    // --- Callback de estado (chamado quando Alexa envia comando) ---
    fauxmo.onSetState([](unsigned char device_id, const char* device_name,
                         bool state, unsigned char value) {
        Serial.printf("[ALEXA] Comando recebido: device='%s', state=%s, value=%d\n",
                      device_name, state ? "ON" : "OFF", value);

        // Aplica o estado recebido da Alexa
        setPSUState(state, "Alexa");
    });
}

// =============================================================================
// HANDLER DO LOOP (NON-BLOCKING)
// =============================================================================

/**
 * @brief Processa pacotes do FauxmoESP (chamado no loop)
 */
void handleAlexa() {
    fauxmo.handle();
}

#endif // ALEXA_CONTROL_H
