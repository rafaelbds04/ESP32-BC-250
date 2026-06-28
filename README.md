# ESP32-BC-250 — Smart ATX Power Supply Controller

[Português](#-português) | [English](#-english)

---

## 🇵🇹 Português

### 📋 Sobre o Projeto
O **ESP32-BC-250** é um firmware completo e de alta performance escrito em C++ (Arduino Framework via PlatformIO) para transformar uma fonte ATX modelo BC-250 (ou qualquer fonte ATX padrão) em um sistema inteligente e conectado.

O projeto unifica quatro métodos independentes de controle em uma máquina de estados **non-blocking** com foco em estabilidade, baixa latência e segurança de boot:
1. **🔘 Botão Físico:** Interrupção via hardware (ISR FALLING) com trava de debounce/cooldown por software (500ms).
2. **🌐 Interface Web Responsiva:** Servidor HTTP assíncrono embarcado (`ESPAsyncWebServer`) servindo um painel Dark Mode moderno com controle por toggle e atualização de status em tempo real (via AJAX/JSON).
3. **🗣️ Comando de Voz (Alexa):** Emulação local de dispositivo smart home (`FauxmoESP`) compatível com caixas Amazon Echo (ex: *"Alexa, ligar BC-250"*).
4. **🎮 Controles Bluetooth:** Suporte nativo a controles modernos (PS5 DualSense, PS4 DualShock, Xbox Wireless, 8BitDo, Nintendo Switch Pro) via biblioteca `Bluepad32` (botão PS/Home altera o estado da fonte).

---

### 🛠️ Hardware e Pinagem (ESP32-WROOM-32D / 4MB)

| Pino GPIO | Modo | Descrição Hardware / Circuito | Estado Inicial (Boot) |
|---|---|---|---|
| **GPIO 26** | `OUTPUT` | Conectado à Base de um transistor NPN (C1815GR ou 2N2222A) via resistor de 2.2kΩ. O Coletor vai no pino **PS-ON** da fonte ATX, e o Emissor no GND. | `LOW` (Fonte Desligada) |
| **GPIO 27** | `OUTPUT` | Conectado ao LED de status do botão físico. Reflete o estado da fonte (HIGH = Aceso/Ligado, LOW = Apagado/Desligado). | `LOW` (Apagado) |
| **GPIO 32** | `INPUT_PULLUP` | Botão físico (Push-button) ligado ao GND. Aciona via interrupção de hardware na borda de descida (`FALLING`). | Pull-up Interno |
| **GPIO 34** | `INPUT` | Entrada lógica opcional (0 a 3.3V) para leitura de sinais como **Power Good** (PG) da fonte. Requer `#define USE_LOGIC_INPUT`. | Leitura ADC |

---

### 🧠 Arquitetura do Firmware

- **Single Source of Truth:** O estado da fonte é mantido estritamente na variável `psuState` em `psu_control.h`. Qualquer método de controle aciona a função central `setPSUState(bool newState, const char* source)`.
- **Zero Delays Bloqueantes:** Todo o ciclo do `loop()` é non-blocking, permitindo a execução paralela de tarefas de rede, Bluetooth e leitura de entradas.
- **Coexistência Wi-Fi + Bluetooth:** O framework customizado do Bluepad32 utiliza BTstack. A varredura Bluetooth é temporariamente pausada durante o provisionamento da rede Wi-Fi para garantir estabilidade no portal cativo (`WiFiManager`).
- **Compartilhamento de Porta HTTP:** O `ESPAsyncWebServer` e o `FauxmoESP` compartilham a porta 80 de forma limpa sem conflitos de socket.

---

### 🚀 Como Compilar e Gravar (PlatformIO)

#### Pré-requisitos
- Visual Studio Code com a extensão **PlatformIO IDE** instalada (ou PlatformIO Core CLI).

#### Passos
1. Clone este repositório:
   ```bash
   git clone https://github.com/seu-usuario/ESP32-BC-250.git
   cd ESP32-BC-250
   ```
2. Compile o projeto:
   ```bash
   pio run
   ```
3. Grave no ESP32 (conectado via USB):
   ```bash
   pio run --target upload
   ```
4. Abra o Monitor Serial para depuração (115200 baud):
   ```bash
   pio device monitor
   ```

---

### ⚙️ Configuração Inicial (Wi-Fi e Alexa)

1. **Provisionamento Wi-Fi:** No primeiro boot (ou se a rede salva não for encontrada), o ESP32 criará um Access Point com o SSID `ESP32-BC250-Setup`. Conecte-se a ele pelo celular/PC para configurar sua rede Wi-Fi local.
2. **Pareamento Alexa:** Certifique-se de que sua caixa Amazon Echo esteja na mesma rede local. Diga: *"Alexa, procurar dispositivos"*. A Alexa encontrará o dispositivo inteligente nomeado como **`BC-250`**.
3. **Conexão Gamepad BT:** Ao ligar o ESP32, coloque seu controle Bluetooth em modo de pareamento. O Bluepad32 conectará automaticamente e o botão **PS/Home** passará a alternar o estado da fonte.

---
---

## 🇬🇧 English

### 📋 About the Project
**ESP32-BC-250** is a comprehensive, high-performance firmware written in C++ (Arduino Framework via PlatformIO) designed to transform a BC-250 model ATX power supply (or any standard ATX PSU) into a modern, connected, smart controller.

The system merges four independent control methods into a single **non-blocking** state machine focused on stability, low latency, and boot safety:
1. **🔘 Physical Button:** Hardware interrupt (ISR FALLING) with software debounce and cooldown protection (500ms).
2. **🌐 Responsive Web UI:** Embedded asynchronous HTTP server (`ESPAsyncWebServer`) serving a sleek, Dark-Mode web panel with real-time AJAX/JSON status updates.
3. **🗣️ Voice Control (Alexa):** Local smart home device emulation (`FauxmoESP`) fully compatible with Amazon Echo devices (e.g., *"Alexa, turn on BC-250"*).
4. **🎮 Bluetooth Gamepads:** Native support for modern controllers (PS5 DualSense, PS4 DualShock, Xbox Wireless, 8BitDo, Nintendo Switch Pro) powered by the `Bluepad32` library (pressing PS/Home toggles PSU power).

---

### 🛠️ Hardware & Pinout (ESP32-WROOM-32D / 4MB)

| GPIO Pin | Mode | Hardware Circuit / Description | Initial State (Boot) |
|---|---|---|---|
| **GPIO 26** | `OUTPUT` | Connected to the Base of an NPN transistor (C1815GR or 2N2222A) via a 2.2kΩ resistor. Collector connects to PSU **PS-ON**, Emitter to GND. | `LOW` (PSU OFF) |
| **GPIO 27** | `OUTPUT` | Status LED on the physical button. Reflects PSU state (HIGH = ON, LOW = OFF). | `LOW` (OFF) |
| **GPIO 32** | `INPUT_PULLUP` | Physical Push-Button connected to GND. Triggers hardware interrupt on falling edge (`FALLING`). | Internal Pull-up |
| **GPIO 34** | `INPUT` | Optional logic input (0 to 3.3V) for reading signals like **Power Good** (PG). Requires `#define USE_LOGIC_INPUT`. | ADC Read |

---

### 🧠 Firmware Architecture

- **Single Source of Truth:** PSU state is centrally managed by `psuState` inside `psu_control.h`. All control sources trigger `setPSUState(bool newState, const char* source)`.
- **Non-Blocking Execution:** Zero `delay()` calls inside `loop()`. Tasks are handled asynchronously using timers and callbacks.
- **Wi-Fi & Bluetooth Coexistence:** Bluepad32 BTstack inquiry is paused during Wi-Fi onboarding to guarantee captive portal stability (`WiFiManager`).
- **HTTP Port Sharing:** `ESPAsyncWebServer` and `FauxmoESP` cleanly share TCP Port 80 without socket binding conflicts.

---

### 🚀 Build and Flash Guide (PlatformIO)

#### Prerequisites
- Visual Studio Code with **PlatformIO IDE** extension (or PlatformIO CLI).

#### Instructions
1. Clone this repository:
   ```bash
   git clone https://github.com/your-username/ESP32-BC-250.git
   cd ESP32-BC-250
   ```
2. Build project firmware:
   ```bash
   pio run
   ```
3. Flash to ESP32:
   ```bash
   pio run --target upload
   ```
4. Monitor Serial output (115200 baud):
   ```bash
   pio device monitor
   ```

---

### ⚙️ Initial Setup (Wi-Fi & Alexa)

1. **Wi-Fi Onboarding:** On first boot, the ESP32 spawns a captive AP named `ESP32-BC250-Setup`. Connect via phone or PC to enter your local Wi-Fi credentials.
2. **Alexa Pairing:** Ensure your Amazon Echo device is on the same local Wi-Fi network. Say: *"Alexa, discover devices"*. Alexa will discover a new smart device named **`BC-250`**.
3. **Bluetooth Gamepad:** Put your gamepad into pairing mode. Bluepad32 will connect automatically, and pressing the **PS/Home** button will toggle power.

---

## 📜 License & Credits

Developed with ❤️ by **ESP32-BC-250 Project**.
Based on reference architectures by [dexikdex](https://github.com/dexikdex/ESP32-BC250-LOP_PSU-PowerON-Xbox) and [PetteriLah](https://github.com/PetteriLah/BC-250-PC-Remote-Control).
