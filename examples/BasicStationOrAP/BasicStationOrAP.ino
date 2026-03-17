#include <SuperOTA.h>

SuperOTA ota;

void setup() {
  ota.beginSerial(115200);

  // Opcional: carrega ultimo perfil salvo na NVS.
  ota.loadPreferences();

  // Se nao houver credenciais salvas, define um perfil inicial.
  if (!ota.hasStationCredentials()) {
    ota.setStationCredentials("MinhaRede", "MinhaSenha");
    ota.setAccessPointCredentials("SuperOTA-Recovery", "12345678");
    ota.setHostname("superota-no1");
    ota.setPreferAccessPoint(false);  // tenta station antes de AP
    // Opcional: em target ESP32-P4 o modo seguro ja vem ativado por padrao.
    // ota.setSafeP4Mode(true);
    ota.savePreferences();
  }

  if (!ota.begin()) {
    Serial.println("[APP] Falha ao iniciar OTA.");
  }
}

void loop() {
  ota.loop();
}
