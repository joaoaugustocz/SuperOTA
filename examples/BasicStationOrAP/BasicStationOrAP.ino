#include <SuperOTA.h>

SuperOTA ota;

void setup() {
  ota.beginSerial(115200);

  // Opcional: carrega ultimo perfil salvo na NVS.
  ota.loadPreferences();

  // Se nao houver credenciais salvas, define um perfil inicial.
  if (!ota.hasStationCredentials()) {
    ota.clearStationNetworks();
    ota.addStationNetwork("MinhaRede", "MinhaSenha");
    ota.addStationNetwork("MinhaRedeBackup", "MinhaSenhaBackup");
    ota.setAccessPointCredentials("SuperOTA-Recovery", "12345678");
    ota.setHostname("superota-no1");
    ota.setPreferAccessPoint(false);  // tenta station antes de AP
    ota.enableSerialConfigCommand(true, "configota");
    ota.savePreferences();
  }

  if (!ota.begin()) {
    Serial.println("[APP] Falha ao iniciar OTA.");
  }
}

void loop() {
  ota.loop();
}
