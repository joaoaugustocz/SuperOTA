#include <SuperOTA.h>

SuperOTA ota;

void setup() {
  ota.beginSerial(115200);

  ota.setHostname("superota-smart-sta");
  ota.setPreferAccessPoint(false);
  ota.setAccessPointCredentials("SuperOTA-Recovery", "12345678");

  // Lista priorizada: a primeira rede encontrada no scan sera tentada primeiro.
  ota.clearStationNetworks();
  ota.addStationNetwork("WiFi-Casa", "senha-casa");
  ota.addStationNetwork("WiFi-Escritorio", "senha-escritorio");
  ota.addStationNetwork("WiFi-Backup", "senha-backup");

  // Comando serial para abrir portal: configota
  ota.enableSerialConfigCommand(true, "configota");
  ota.setDebugSummaryIntervalMs(30000);  // 30s quando debug estiver ativo

  if (!ota.begin()) {
    Serial.println("[APP] Falha ao iniciar OTA.");
  }

  Serial.println("[APP] Digite 'configota' no serial para abrir portal web.");
  Serial.println("[APP] Debug opcional: debug-on, debug-summary, debug-off.");
}

void loop() {
  ota.loop();
}
