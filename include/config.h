/**
 * @file config.h
 * @brief Configuração centralizada do ESP32-BC-250
 *
 * Todos os parâmetros configuráveis do projeto estão aqui:
 * pinagem, constantes de timing, nomes de dispositivos e feature flags.
 *
 * NOTA: Altere apenas este arquivo para adaptar o firmware a um hardware diferente.
 */

#ifndef CONFIG_H
#define CONFIG_H

// =============================================================================
// VERSÃO DO FIRMWARE
// =============================================================================
#ifndef FW_VERSION
#define FW_VERSION "1.0.0"
#endif

// =============================================================================
// FEATURE FLAGS
// =============================================================================
// Descomente a linha abaixo para habilitar a leitura do pino de entrada lógica
// (ex: Power Good da fonte). Quando desativado, o GPIO 34 não é utilizado.
// #define USE_LOGIC_INPUT

// =============================================================================
// PINAGEM — ESP32-WROOM-32D
// =============================================================================

/**
 * GPIO 26 (OUTPUT) — Controle PS-ON da fonte ATX
 *
 * Conectado à base de um transistor NPN (C1815GR ou 2N2222A) via resistor
 * de 2.2kΩ. Coletor no PS-ON da fonte, emissor no GND.
 *   HIGH = fonte LIGADA (transistor satura, PS-ON -> GND)
 *   LOW  = fonte DESLIGADA (estado padrão no boot)
 */
#define PIN_PS_ON       26

/**
 * GPIO 27 (OUTPUT) — LED indicador de status
 *
 * LED no botão físico que reflete o estado da fonte.
 *   Aceso    = fonte LIGADA
 *   Apagado  = fonte DESLIGADA
 */
#define PIN_LED         27

/**
 * GPIO 32 (INPUT_PULLUP) — Botão físico (push-button)
 *
 * Conectado ao GND. Usa interrupção de hardware (FALLING edge)
 * com debounce por software via millis().
 */
#define PIN_BUTTON      32

/**
 * GPIO 34 (INPUT) — Entrada lógica opcional
 *
 * Lê sinal de 0 a 3.3V (ex: pino Power Good da fonte ATX).
 * Só é utilizado quando USE_LOGIC_INPUT está definido.
 * NOTA: GPIO 34 é input-only e NÃO possui pullup/pulldown interno.
 */
#define PIN_LOGIC_IN    34

// =============================================================================
// CONSTANTES DE TIMING (milissegundos)
// =============================================================================

/** Debounce e cooldown mínimo do botão físico (ms) — evita repetição por ruidos do chaveamento elétrico */
#define DEBOUNCE_MS             500

/** Cooldown entre toggles via gamepad BT (ms) — evita toggles repetidos */
#define GAMEPAD_COOLDOWN_MS     500

/** Intervalo de leitura do pino de entrada lógica (ms) */
#define LOGIC_INPUT_INTERVAL_MS 1000

/** Intervalo de heartbeat no Serial Monitor (ms) — debug */
#define HEARTBEAT_INTERVAL_MS   30000

// =============================================================================
// NOMES DE DISPOSITIVOS
// =============================================================================

/** Nome do dispositivo emulado no FauxmoESP (Alexa) */
#ifndef FAUXMO_DEVICE_NAME
#define FAUXMO_DEVICE_NAME "Fonte BC-250"
#endif

/** SSID do Access Point do WiFiManager (modo configuração) */
#ifndef WIFI_AP_NAME
#define WIFI_AP_NAME "ESP32-BC250-Setup"
#endif

// =============================================================================
// LIMIAR DO LOGIC INPUT
// =============================================================================

/**
 * Limiar de tensão (valor ADC 12-bit, 0-4095) para considerar o sinal
 * como HIGH. 3.3V / 2 ≈ 1.65V → ~2048 no ADC.
 */
#define LOGIC_INPUT_THRESHOLD   2048

// =============================================================================
// PORTA DO SERVIDOR WEB
// =============================================================================
#define WEB_SERVER_PORT 80

#include <Arduino.h>

/**
 * @brief Higieniza o nome para ser usado como hostname/MDNS de rede
 *        (Converte para minúsculas, remove espaços e acentos por hífens)
 */
inline String sanitizeHostname(String name) {
    name.toLowerCase();
    name.trim();
    String sanitized = "";
    for (size_t i = 0; i < name.length(); i++) {
        char c = name.charAt(i);
        if (isalnum(c)) {
            sanitized += c;
        } else if (c == ' ' || c == '-' || c == '_') {
            if (sanitized.length() > 0 && sanitized.charAt(sanitized.length() - 1) != '-') {
                sanitized += '-';
            }
        }
    }
    if (sanitized.length() > 0 && sanitized.charAt(sanitized.length() - 1) == '-') {
        sanitized.remove(sanitized.length() - 1);
    }
    if (sanitized.length() == 0) {
        sanitized = "esp32-bc250";
    }
    return sanitized;
}

#endif // CONFIG_H
