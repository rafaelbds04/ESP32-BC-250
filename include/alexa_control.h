/**
 * @file alexa_control.h
 * @brief Integração com Amazon Alexa via FauxmoESP
 *
 * Emula um dispositivo Philips Hue local na rede (sem necessidade de cloud).
 * O dispositivo aparece automaticamente no app Alexa quando descoberto.
 *
 * COMANDOS DE VOZ:
 *   "Alexa, ligar Fonte BC-250"    → setPSUState(true)
 *   "Alexa, desligar Fonte BC-250" → setPSUState(false)
 *
 * NOTAS:
 * - Alexa e ESP32 DEVEM estar na mesma rede WiFi
 * - FauxmoESP emula o protocolo Hue 2ª geração (baseado em TCP)
 * - createServer(false) evita conflito com ESPAsyncWebServer na porta 80
 */

#ifndef ALEXA_CONTROL_H
#define ALEXA_CONTROL_H

#include <fauxmoESP.h>
#include "config.h"
#include "psu_control.h"

// Instância global do FauxmoESP
fauxmoESP fauxmo;

// =============================================================================
// INICIALIZAÇÃO
// =============================================================================

/**
 * @brief Configura e inicia o FauxmoESP
 *
 * IMPORTANTE sobre a coexistência com ESPAsyncWebServer:
 *   - createServer(false) → FauxmoESP NÃO cria seu próprio servidor TCP
 *   - setPort(80) → Usa a mesma porta do servidor web existente
 *   - O ESPAsyncWebServer e o FauxmoESP compartilham a porta 80
 *
 * O callback onSetState é chamado quando a Alexa envia um comando.
 * O parâmetro 'state' indica liga (true) ou desliga (false).
 * O parâmetro 'value' (0-255) indica brilho — não usado aqui.
 */
void initAlexa() {
    // Não criar servidor TCP próprio (ESPAsyncWebServer já serve na porta 80)
    fauxmo.createServer(false);
    fauxmo.setPort(80);

    // Habilitar descoberta de dispositivos pela Alexa
    fauxmo.enable(true);

    // Registrar o dispositivo com o nome configurado
    fauxmo.addDevice(FAUXMO_DEVICE_NAME);

    Serial.printf("[ALEXA] Dispositivo '%s' registrado\n", FAUXMO_DEVICE_NAME);

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
 *
 * DEVE ser chamado a cada iteração do loop() para que o FauxmoESP
 * possa responder à descoberta SSDP e processar comandos da Alexa.
 * Esta chamada é non-blocking.
 */
void handleAlexa() {
    fauxmo.handle();
}

#endif // ALEXA_CONTROL_H
