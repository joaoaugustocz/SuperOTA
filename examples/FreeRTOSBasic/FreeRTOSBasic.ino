#include <Arduino.h>
#include <SuperOTA.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

SuperOTA ota;

TaskHandle_t otaTaskHandle = nullptr;
TaskHandle_t appTaskHandle = nullptr;

// Task dedicada para manter OTA/portal/telnet ativos.
// Regra deste exemplo: somente esta task chama ota.loop().
void otaTask(void* /*parameter*/) {
  for (;;) {
    ota.loop();
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

// Task de aplicacao simulada.
// Aqui voce coloca leitura de sensores, controle, telemetria, etc.
void appTask(void* /*parameter*/) {
  uint32_t tick = 0;

  for (;;) {
    Serial.print("[APP] Tick de aplicacao: ");
    Serial.println(tick++);

    // Simula trabalho periodico sem bloquear completamente o sistema.
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}

void setup() {
  ota.beginSerial(115200);
  ota.enableSerialConfigCommand(true, "configota");

  // Carrega configuracao persistida.
  const bool prefsLoaded = ota.loadPreferences();

  // Defaults apenas no primeiro boot com NVS valida.
  if (!prefsLoaded) {
    Serial.println("[APP] Aviso: NVS indisponivel. Rodando com defaults apenas em RAM.");
    ota.setHostname("superota-freertos-basic");
    ota.setPreferAccessPoint(false);
    ota.setAccessPointCredentials("SuperOTA-Recovery", "12345678");

    ota.clearStationNetworks();
    ota.addStationNetwork("MinhaRede", "MinhaSenha");
    ota.addStationNetwork("MinhaRedeBackup", "MinhaSenhaBackup");
  } else if (!ota.hasStationCredentials()) {
    ota.setHostname("superota-freertos-basic");
    ota.setPreferAccessPoint(false);
    ota.setAccessPointCredentials("SuperOTA-Recovery", "12345678");

    ota.clearStationNetworks();
    ota.addStationNetwork("MinhaRede", "MinhaSenha");
    ota.addStationNetwork("MinhaRedeBackup", "MinhaSenhaBackup");
    ota.savePreferences();

    Serial.println("[APP] Perfil inicial salvo na NVS.");
  } else {
    Serial.println("[APP] Perfil carregado da NVS.");
  }

  if (!ota.begin()) {
    Serial.println("[APP] Falha ao iniciar SuperOTA.");
  }

  BaseType_t ok;

  // Prioridade um pouco maior para a task OTA/rede.
  ok = xTaskCreate(otaTask, "OTA_Task", 6144, nullptr, 3, &otaTaskHandle);
  if (ok != pdPASS) {
    Serial.println("[APP] Erro ao criar OTA_Task.");
  }

  ok = xTaskCreate(appTask, "APP_Task", 4096, nullptr, 1, &appTaskHandle);
  if (ok != pdPASS) {
    Serial.println("[APP] Erro ao criar APP_Task.");
  }

  Serial.println("[APP] FreeRTOS Basic pronto.");
  Serial.println("[APP] Comando serial: configota");
  Serial.println("[APP] Em station, 'configota' pergunta 1=station / 2=AP.");
  Serial.println("[APP] Regra de ouro: deixe ota.loop() em task dedicada.");
}

void loop() {
  // Nao use ota.loop() aqui neste exemplo.
  // O processamento da biblioteca ja roda na OTA_Task.
  vTaskDelay(pdMS_TO_TICKS(1000));
}
