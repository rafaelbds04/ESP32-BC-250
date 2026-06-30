/**
 * @file psu_control.h
 * @brief Controle central da fonte ATX (PSU) — máquina de estados
 *
 * Este módulo é o coração do firmware. Contém:
 * - Estado global da PSU (volatile bool — fonte única de verdade)
 * - ISR do botão físico com debounce por millis()
 * - Funções setPSUState() e togglePSU() usadas por TODOS os módulos
 * - Leitura condicional do pino de entrada lógica (Power Good)
 *
 * ARQUITETURA:
 *   Botão, Web, Alexa e Gamepad → togglePSU() / setPSUState()
 *                                        ↓
 *                               GPIO 26 (PS-ON) + GPIO 27 (LED)
 */

#ifndef PSU_CONTROL_H
#define PSU_CONTROL_H

#include <Arduino.h>
#include "config.h"

// =============================================================================
// ESTADO GLOBAL — Fonte Única de Verdade
// =============================================================================

/** Estado atual da fonte: true = LIGADA, false = DESLIGADA */
volatile bool psuState = false;

/** Flag setada pela ISR quando o botão é pressionado */
volatile bool buttonPressed = false;

/** Timestamp do último acionamento válido do botão (debounce) */
volatile unsigned long lastButtonPress = 0;

/** Timestamp do último toggle por qualquer fonte (para log) */
unsigned long lastToggleTime = 0;

#ifdef USE_LOGIC_INPUT
/** Último valor lido do pino de entrada lógica */
bool logicInputState = false;
/** Timestamp da última leitura do logic input */
unsigned long lastLogicInputRead = 0;
#endif

// =============================================================================
// FUNÇÕES CORE
// =============================================================================

/**
 * @brief Define o estado da PSU (liga/desliga)
 *
 * Esta é a ÚNICA função que efetivamente altera os GPIOs.
 * Todos os módulos (botão, web, alexa, gamepad) DEVEM usar esta função.
 *
 * @param newState true para LIGAR, false para DESLIGAR
 * @param source   String identificando a fonte do comando (para log)
 */
void setPSUState(bool newState, const char* source = "unknown") {
    if (psuState == newState) {
        // Estado já é o desejado — não faz nada (idempotente)
        return;
    }

    psuState = newState;
    digitalWrite(PIN_PS_ON, psuState ? HIGH : LOW);
    digitalWrite(PIN_LED, psuState ? HIGH : LOW);
    lastToggleTime = millis();

    Serial.printf("[PSU] Estado alterado para %s (fonte: %s) @ %lums\n",
                  psuState ? "LIGADO" : "DESLIGADO",
                  source,
                  lastToggleTime);
}

/**
 * @brief Alterna o estado da PSU (toggle)
 * @param source String identificando a fonte do comando
 */
void togglePSU(const char* source = "unknown") {
    setPSUState(!psuState, source);
}

// =============================================================================
// ISR DO BOTÃO FÍSICO
// =============================================================================

/**
 * @brief ISR (Interrupt Service Routine) para o botão físico
 *
 * Executada em IRAM na borda de descida (FALLING) do GPIO 32.
 * Implementa debounce por software verificando o intervalo desde
 * o último acionamento. Se >= DEBOUNCE_MS, marca a flag buttonPressed.
 *
 * NOTA: Dentro da ISR, NÃO é seguro chamar Serial, WiFi, etc.
 * Apenas setamos a flag para processamento no loop().
 */
void IRAM_ATTR buttonISR() {
    unsigned long now = millis();
    if (now - lastButtonPress >= DEBOUNCE_MS) {
        lastButtonPress = now;
        buttonPressed = true;
    }
}

// =============================================================================
// LOCALIZAÇÃO DO DISPOSITIVO (IDENTIFY / BLINK)
// =============================================================================

volatile bool identifyActive = false;
unsigned long identifyStartTime = 0;
unsigned long lastIdentifyBlink = 0;
bool identifyBlinkState = false;

/**
 * @brief Ativa o modo de identificação física do ESP32 por LED piscante
 */
void triggerIdentify() {
    identifyActive = true;
    identifyStartTime = millis();
    lastIdentifyBlink = 0;
    identifyBlinkState = false;
    Serial.println("[PSU] Identificação ativada - piscando LED");
}

/**
 * @brief Processa o piscar do LED no modo de identificação (chamado no loop)
 */
void handleIdentifyBlink() {
    if (!identifyActive) return;
    
    unsigned long now = millis();
    if (now - identifyStartTime > 5000) { // Duração: 5 segundos
        identifyActive = false;
        // Restaura o estado original do LED com base no estado da PSU
        digitalWrite(PIN_LED, psuState ? HIGH : LOW);
        Serial.println("[PSU] Identificação concluída - estado do LED restaurado");
        return;
    }
    
    if (now - lastIdentifyBlink >= 100) { // Pisca rápido a cada 100ms
        lastIdentifyBlink = now;
        identifyBlinkState = !identifyBlinkState;
        digitalWrite(PIN_LED, identifyBlinkState ? HIGH : LOW);
    }
}

// =============================================================================
// INICIALIZAÇÃO
// =============================================================================

/**
 * @brief Inicializa os GPIOs do controle da PSU
 *
 * IMPORTANTE: Esta função DEVE ser chamada no início do setup(),
 * ANTES de qualquer inicialização de WiFi/BT, para garantir que
 * GPIO 26 comece em LOW (fonte DESLIGADA) durante o boot.
 */
void initPSUPins() {
    // Configura PS-ON como saída e garante LOW (fonte desligada)
    pinMode(PIN_PS_ON, OUTPUT);
    digitalWrite(PIN_PS_ON, LOW);

    // Configura LED como saída e garante apagado
    pinMode(PIN_LED, OUTPUT);
    digitalWrite(PIN_LED, LOW);

    // Configura botão com pullup interno e interrupção FALLING
    pinMode(PIN_BUTTON, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(PIN_BUTTON), buttonISR, FALLING);

    #ifdef USE_LOGIC_INPUT
    // GPIO 34 é input-only, sem pullup/pulldown interno
    pinMode(PIN_LOGIC_IN, INPUT);
    Serial.println("[PSU] Entrada lógica (GPIO 34) habilitada");
    #endif

    Serial.println("[PSU] GPIOs inicializados — PS-ON: LOW, LED: OFF");
}

// =============================================================================
// HANDLERS DO LOOP (NON-BLOCKING)
// =============================================================================

/**
 * @brief Processa a flag do botão físico (chamado no loop)
 *
 * Verifica se a ISR marcou buttonPressed e, se sim, executa o toggle.
 * O debounce já foi verificado na ISR, então aqui apenas consumimos a flag.
 */
void handleButtonDebounce() {
    if (buttonPressed) {
        buttonPressed = false;
        unsigned long now = millis();
        if (now - lastToggleTime >= DEBOUNCE_MS) {
            togglePSU("Botão Físico");
        }
    }
}

#ifdef USE_LOGIC_INPUT
/**
 * @brief Lê o pino de entrada lógica periodicamente (non-blocking)
 *
 * Faz leitura analógica do GPIO 34 a cada LOGIC_INPUT_INTERVAL_MS.
 * O valor é comparado com LOGIC_INPUT_THRESHOLD para determinar
 * se o sinal é HIGH (ex: Power Good ativo).
 *
 * Este handler apenas loga mudanças de estado. Para implementar
 * ações automáticas (ex: desligar se Power Good cair), descomente
 * a lógica no bloco condicional abaixo.
 */
void handleLogicInput() {
    unsigned long now = millis();
    if (now - lastLogicInputRead >= LOGIC_INPUT_INTERVAL_MS) {
        lastLogicInputRead = now;

        int rawValue = analogRead(PIN_LOGIC_IN);
        bool newState = (rawValue >= LOGIC_INPUT_THRESHOLD);

        if (newState != logicInputState) {
            logicInputState = newState;
            Serial.printf("[LOGIC] Entrada lógica: %s (ADC: %d)\n",
                          logicInputState ? "HIGH" : "LOW", rawValue);

            // === AÇÃO AUTOMÁTICA (descomente se necessário) ===
            // Exemplo: desligar fonte se Power Good cair
            // if (!logicInputState && psuState) {
            //     setPSUState(false, "Power Good Lost");
            // }
        }
    }
}
#endif

#endif // PSU_CONTROL_H
