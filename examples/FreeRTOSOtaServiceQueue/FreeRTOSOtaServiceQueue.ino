#include <Arduino.h>
#include <SuperOTA.h>

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

SuperOTA ota;

enum OtaCommandType : uint8_t {
  OTA_CMD_PORTAL_AP = 0,
  OTA_CMD_PORTAL_STOP,
  OTA_CMD_DEBUG_ON,
  OTA_CMD_DEBUG_OFF,
  OTA_CMD_DEBUG_SUMMARY,
  OTA_CMD_PRINT_STATUS
};

struct OtaCommand {
  OtaCommandType type;
};

QueueHandle_t otaQueue = nullptr;
TaskHandle_t otaServiceTaskHandle = nullptr;
TaskHandle_t consoleTaskHandle = nullptr;

String consoleBuffer;

bool enqueueOtaCommand(OtaCommandType type) {
  if (otaQueue == nullptr) {
    return false;
  }
  OtaCommand cmd{type};
  return xQueueSend(otaQueue, &cmd, pdMS_TO_TICKS(50)) == pdPASS;
}

void handleOtaCommand(const OtaCommand& cmd) {
  switch (cmd.type) {
    case OTA_CMD_PORTAL_AP:
      Serial.println("[OTA-SVC] Abrindo portal em AP...");
      ota.startConfigPortal("SuperOTA-RTOS-Setup", "12345678");
      break;

    case OTA_CMD_PORTAL_STOP:
      Serial.println("[OTA-SVC] Encerrando portal...");
      ota.stopConfigPortal(true);
      break;

    case OTA_CMD_DEBUG_ON:
      ota.enableDebugMetrics(true);
      break;

    case OTA_CMD_DEBUG_OFF:
      ota.enableDebugMetrics(false);
      break;

    case OTA_CMD_DEBUG_SUMMARY:
      ota.printDebugSummary();
      break;

    case OTA_CMD_PRINT_STATUS:
      Serial.println("[OTA-SVC] Status:");
      Serial.print("[OTA-SVC] configured: ");
      Serial.println(ota.isConfigured() ? "true" : "false");
      Serial.print("[OTA-SVC] station mode: ");
      Serial.println(ota.isStationMode() ? "true" : "false");
      Serial.print("[OTA-SVC] portal running: ");
      Serial.println(ota.configPortalRunning() ? "true" : "false");
      Serial.print("[OTA-SVC] station networks: ");
      Serial.println(ota.stationNetworkCount());
      Serial.print("[OTA-SVC] ip: ");
      Serial.println(ota.ip());
      break;
  }
}

// Service task: unica dona da instancia ota apos setup().
// Todas as chamadas de runtime da biblioteca passam por aqui.
void otaServiceTask(void* /*parameter*/) {
  OtaCommand cmd{};

  for (;;) {
    while (xQueueReceive(otaQueue, &cmd, 0) == pdPASS) {
      handleOtaCommand(cmd);
    }

    ota.loop();
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

void printConsoleHelp() {
  Serial.println("[CONSOLE] Comandos:");
  Serial.println("[CONSOLE] portal-ap      -> abre portal em AP");
  Serial.println("[CONSOLE] portal-stop    -> fecha portal");
  Serial.println("[CONSOLE] debug-on       -> ativa metricas debug");
  Serial.println("[CONSOLE] debug-off      -> desativa metricas debug");
  Serial.println("[CONSOLE] debug-summary  -> resumo imediato de debug");
  Serial.println("[CONSOLE] status         -> status atual da SuperOTA");
  Serial.println("[CONSOLE] help           -> mostra ajuda");
}

void processConsoleCommand(const String& cmdRaw) {
  String cmd = cmdRaw;
  cmd.trim();
  cmd.toLowerCase();

  if (cmd.length() == 0) {
    return;
  }

  if (cmd == "portal-ap") {
    if (!enqueueOtaCommand(OTA_CMD_PORTAL_AP)) {
      Serial.println("[CONSOLE] Falha ao enfileirar comando.");
    }
    return;
  }

  if (cmd == "portal-stop") {
    if (!enqueueOtaCommand(OTA_CMD_PORTAL_STOP)) {
      Serial.println("[CONSOLE] Falha ao enfileirar comando.");
    }
    return;
  }

  if (cmd == "debug-on") {
    if (!enqueueOtaCommand(OTA_CMD_DEBUG_ON)) {
      Serial.println("[CONSOLE] Falha ao enfileirar comando.");
    }
    return;
  }

  if (cmd == "debug-off") {
    if (!enqueueOtaCommand(OTA_CMD_DEBUG_OFF)) {
      Serial.println("[CONSOLE] Falha ao enfileirar comando.");
    }
    return;
  }

  if (cmd == "debug-summary") {
    if (!enqueueOtaCommand(OTA_CMD_DEBUG_SUMMARY)) {
      Serial.println("[CONSOLE] Falha ao enfileirar comando.");
    }
    return;
  }

  if (cmd == "status") {
    if (!enqueueOtaCommand(OTA_CMD_PRINT_STATUS)) {
      Serial.println("[CONSOLE] Falha ao enfileirar comando.");
    }
    return;
  }

  if (cmd == "help") {
    printConsoleHelp();
    return;
  }

  Serial.print("[CONSOLE] Comando desconhecido: ");
  Serial.println(cmd);
}

// Task de console: recebe comandos via Serial e envia para a fila.
// Esta task NAO chama API de OTA diretamente.
void consoleTask(void* /*parameter*/) {
  for (;;) {
    while (Serial.available() > 0) {
      const char c = static_cast<char>(Serial.read());

      if (c == '\n' || c == '\r') {
        if (consoleBuffer.length() > 0) {
          processConsoleCommand(consoleBuffer);
          consoleBuffer = "";
        }
        continue;
      }

      if (consoleBuffer.length() < 96) {
        consoleBuffer += c;
      }
    }

    vTaskDelay(pdMS_TO_TICKS(20));
  }
}

void setup() {
  ota.beginSerial(115200);

  // Neste exemplo o console usa Serial diretamente.
  // Desabilitamos o parser serial interno para evitar duas tasks lendo Serial.
  ota.enableSerialConfigCommand(false);

  ota.loadPreferences();
  if (!ota.hasStationCredentials()) {
    ota.setHostname("superota-freertos-svc");
    ota.setPreferAccessPoint(false);
    ota.setAccessPointCredentials("SuperOTA-Recovery", "12345678");

    ota.clearStationNetworks();
    ota.addStationNetwork("MinhaRede", "MinhaSenha");
    ota.addStationNetwork("MinhaRedeBackup", "MinhaSenhaBackup");
    ota.savePreferences();
  }

  if (!ota.begin()) {
    Serial.println("[APP] Falha ao iniciar SuperOTA.");
  }

  otaQueue = xQueueCreate(12, sizeof(OtaCommand));
  if (otaQueue == nullptr) {
    Serial.println("[APP] Falha ao criar fila OTA.");
    return;
  }

  BaseType_t ok;

  ok = xTaskCreate(otaServiceTask, "OTA_Service", 7168, nullptr, 3, &otaServiceTaskHandle);
  if (ok != pdPASS) {
    Serial.println("[APP] Falha ao criar OTA_Service.");
  }

  ok = xTaskCreate(consoleTask, "Console_Task", 4096, nullptr, 1, &consoleTaskHandle);
  if (ok != pdPASS) {
    Serial.println("[APP] Falha ao criar Console_Task.");
  }

  Serial.println("[APP] FreeRTOS OTA Service com fila pronto.");
  printConsoleHelp();
}

void loop() {
  // Neste desenho, o loop Arduino fica ocioso.
  vTaskDelay(pdMS_TO_TICKS(1000));
}
