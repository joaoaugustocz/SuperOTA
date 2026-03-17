#include <SuperOTA.h>

SuperOTA ota;

void setup() {
  ota.beginSerial(115200);

  ota.setHostname("superota-ap-only");
  ota.setPreferAccessPoint(true);
  ota.setAccessPointCredentials("SuperOTA-OnlyAP", "12345678");

  // Sem redes station: ficara somente em AP.
  ota.clearStationNetworks();

  if (!ota.begin()) {
    Serial.println("[APP] Falha ao iniciar OTA em AP-only.");
  }
}

void loop() {
  ota.loop();
}
