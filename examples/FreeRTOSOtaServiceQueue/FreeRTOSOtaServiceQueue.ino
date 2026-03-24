#include <Arduino.h>
#include <SuperOTA.h>

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

SuperOTA ota;

enum OtaCommandType : uint8_t {
  OTA_CMD_REQUEST_CONFIG = 0,
  OTA_CMD_PORTAL_STATION,
  OTA_CMD_PORTAL_AP,
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
volatile bool gAwaitingPortalChoice = false;

void sanitizeConsoleCommand(String& cmd) {
  while (cmd.length() > 0) {
    const uint8_t c = static_cast<uint8_t>(cmd[0]);
    if (c >= 32U && c <= 126U) {
      break;
    }
    cmd.remove(0, 1);
  }
}

bool enqueueOtaCommand(OtaCommandType type) {
  if (otaQueue == nullptr) {
    return false;
  }

  const OtaCommand cmd{type};
  return xQueueSend(otaQueue, &cmd, pdMS_TO_TICKS(50)) == pdPASS;
}

void printPortalChoicePrompt() {
  Serial.println("[OTA-SVC] Voce esta em station conectado.");
  Serial.println("[OTA-SVC] Onde deseja abrir o portal?");
  Serial.println("[OTA-SVC] 1 = station (hostname.local / IP atual)");
  Serial.println("[OTA-SVC] 2 = AP configurado");
  Serial.println("[OTA-SVC] config-stop = cancela a escolha");
}

void printConsoleHelp() {
  Serial.println("[CONSOLE] Comandos:");
  Serial.println("[CONSOLE] configota      -> em station pergunta 1/2; em AP abre o portal atual");
  Serial.println("[CONSOLE] 1              -> abre portal no station quando houver pergunta pendente");
  Serial.println("[CONSOLE] 2              -> abre portal em AP quando houver pergunta pendente");
  Serial.println("[CONSOLE] portal-sta     -> abre portal direto no station");
  Serial.println("[CONSOLE] portal-ap      -> abre portal em AP");
  Serial.println("[CONSOLE] portal-stop    -> fecha portal");
  Serial.println("[CONSOLE] config-stop    -> alias para fechar portal ou cancelar a escolha 1/2");
  Serial.println("[CONSOLE] debug-on       -> ativa metricas debug");
  Serial.println("[CONSOLE] debug-off      -> desativa metricas debug");
  Serial.println("[CONSOLE] debug-summary  -> resumo imediato de debug");
  Serial.println("[CONSOLE] status         -> status atual da SuperOTA");
  Serial.println("[CONSOLE] help           -> mostra ajuda");
}

void printStatus() {
  Serial.println("[OTA-SVC] Status:");
  Serial.print("[OTA-SVC] configured: ");
  Serial.println(ota.isConfigured() ? "true" : "false");
  Serial.print("[OTA-SVC] station mode: ");
  Serial.println(ota.isStationMode() ? "true" : "false");
  Serial.print("[OTA-SVC] portal running: ");
  Serial.println(ota.configPortalRunning() ? "true" : "false");
  Serial.print("[OTA-SVC] hostname: ");
  Serial.println(ota.hostname());
  Serial.print("[OTA-SVC] AP configurado: ");
  Serial.println(ota.accessPointSsid());
  Serial.print("[OTA-SVC] AP com senha: ");
  Serial.println(ota.accessPointPasswordEnabled() ? "sim" : "nao");
  Serial.print("[OTA-SVC] OTA com senha: ");
  Serial.println(ota.otaPasswordEnabled() ? "sim" : "nao");
  Serial.print("[OTA-SVC] Portal com senha: ");
  Serial.println(ota.portalPasswordEnabled() ? "sim" : "nao");
  Serial.print("[OTA-SVC] station networks: ");
  Serial.println(ota.stationNetworkCount());
  Serial.print("[OTA-SVC] ip: ");
  Serial.println(ota.ip());
}

void handleOtaCommand(const OtaCommand& cmd) {
  switch (cmd.type) {
    case OTA_CMD_REQUEST_CONFIG:
      if (ota.configPortalRunning()) {
        Serial.println("[OTA-SVC] Portal ja esta ativo.");
        gAwaitingPortalChoice = false;
      } else if (ota.isConfigured() && ota.isStationMode()) {
        gAwaitingPortalChoice = true;
        printPortalChoicePrompt();
      } else {
        gAwaitingPortalChoice = false;
        Serial.println("[OTA-SVC] Abrindo portal no AP atual/configurado...");
        if (ota.startConfigPortal()) {
          Serial.println("[OTA-SVC] Portal AP solicitado com sucesso.");
        } else {
          Serial.println("[OTA-SVC] Falha ao abrir portal AP.");
        }
      }
      break;

    case OTA_CMD_PORTAL_STATION:
      gAwaitingPortalChoice = false;
      Serial.println("[OTA-SVC] Abrindo portal em station...");
      if (ota.startConfigPortalOnStation()) {
        Serial.println("[OTA-SVC] Portal station solicitado com sucesso.");
      } else {
        Serial.println("[OTA-SVC] Falha ao abrir portal station.");
      }
      break;

    case OTA_CMD_PORTAL_AP:
      gAwaitingPortalChoice = false;
      Serial.println("[OTA-SVC] Abrindo portal em AP...");
      if (ota.startConfigPortal()) {
        Serial.println("[OTA-SVC] Portal AP solicitado com sucesso.");
      } else {
        Serial.println("[OTA-SVC] Falha ao abrir portal AP.");
      }
      break;

    case OTA_CMD_PORTAL_STOP:
      gAwaitingPortalChoice = false;
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
      printStatus();
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

void processConsoleCommand(const String& cmdRaw) {
  String cmd = cmdRaw;
  cmd.trim();
  sanitizeConsoleCommand(cmd);
  cmd.toLowerCase();

  if (cmd.length() == 0) {
    return;
  }

  if (gAwaitingPortalChoice) {
    if (cmd == "1") {
      if (!enqueueOtaCommand(OTA_CMD_PORTAL_STATION)) {
        Serial.println("[CONSOLE] Falha ao enfileirar comando.");
      }
      return;
    }

    if (cmd == "2") {
      if (!enqueueOtaCommand(OTA_CMD_PORTAL_AP)) {
        Serial.println("[CONSOLE] Falha ao enfileirar comando.");
      }
      return;
    }

    if (cmd == "config-stop" || cmd == "portal-stop") {
      gAwaitingPortalChoice = false;
      Serial.println("[CONSOLE] Escolha de modo cancelada.");
      return;
    }

    Serial.println("[CONSOLE] Opcao invalida. Digite 1, 2 ou config-stop.");
    return;
  }

  if (cmd == "configota") {
    if (!enqueueOtaCommand(OTA_CMD_REQUEST_CONFIG)) {
      Serial.println("[CONSOLE] Falha ao enfileirar comando.");
    }
    return;
  }

  if (cmd == "portal-sta") {
    if (!enqueueOtaCommand(OTA_CMD_PORTAL_STATION)) {
      Serial.println("[CONSOLE] Falha ao enfileirar comando.");
    }
    return;
  }

  if (cmd == "portal-ap") {
    if (!enqueueOtaCommand(OTA_CMD_PORTAL_AP)) {
      Serial.println("[CONSOLE] Falha ao enfileirar comando.");
    }
    return;
  }

  if (cmd == "portal-stop" || cmd == "config-stop") {
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

void configureVolatileDefaults() {
  ota.setHostname("superota-freertos-svc");
  ota.setPreferAccessPoint(false);
  ota.setAccessPointCredentials("SuperOTA-Recovery", "12345678");

  ota.clearStationNetworks();
  ota.addStationNetwork("MinhaRede", "MinhaSenha");
  ota.addStationNetwork("MinhaRedeBackup", "MinhaSenhaBackup");
}

void setup() {
  ota.beginSerial(115200);

  // Neste exemplo o console usa Serial diretamente.
  // Desabilitamos o parser serial interno para evitar duas tasks lendo Serial.
  ota.enableSerialConfigCommand(false);

  const bool prefsLoaded = ota.loadPreferences();
  if (!prefsLoaded) {
    Serial.println("[APP] Aviso: NVS indisponivel. Usando defaults apenas em RAM.");
    configureVolatileDefaults();
  } else if (!ota.hasStationCredentials()) {
    configureVolatileDefaults();
    ota.savePreferences();
    Serial.println("[APP] Perfil inicial salvo na NVS.");
  } else {
    Serial.println("[APP] Perfil carregado da NVS.");
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

  // Margem extra de stack para portal + reconexao + callbacks de rede em alvo P4.
  ok = xTaskCreate(otaServiceTask, "OTA_Service", 10240, nullptr, 3, &otaServiceTaskHandle);
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
