#include <SuperOTA.h>

SuperOTA ota;

void setup() {
  ota.beginSerial(115200);
  ota.enableSerialConfigCommand(true, "configota");

  // Carrega o perfil salvo no portal/configuracao anterior.
  const bool prefsLoaded = ota.loadPreferences();

  // Define defaults apenas quando a NVS esta acessivel e ainda nao possui redes station.
  if (!prefsLoaded) {
    Serial.println("[APP] Aviso: NVS indisponivel. Usando defaults apenas em RAM.");
    ota.clearStationNetworks();
    ota.addStationNetwork("MinhaRede", "MinhaSenha");
    ota.addStationNetwork("MinhaRedeBackup", "MinhaSenhaBackup");
    ota.setAccessPointCredentials("SuperOTA-Recovery", "12345678");
    ota.setHostname("superota-no1");
    ota.setPreferAccessPoint(false);
  } else if (!ota.hasStationCredentials()) {
    ota.clearStationNetworks();
    ota.addStationNetwork("MinhaRede", "MinhaSenha");
    ota.addStationNetwork("MinhaRedeBackup", "MinhaSenhaBackup");
    ota.setAccessPointCredentials("SuperOTA-Recovery", "12345678");
    ota.setHostname("superota-no1");
    ota.setPreferAccessPoint(false);  // tenta station antes de AP
    ota.savePreferences();
    Serial.println("[APP] Perfil inicial salvo na NVS.");
  } else {
    Serial.println("[APP] Perfil carregado da NVS.");
  }

  if (!ota.begin()) {
    Serial.println("[APP] Falha ao iniciar OTA.");
  }

  Serial.println("[APP] Use 'configota' para editar e salvar configuracoes.");
  Serial.println("[APP] Em station, 'configota' pergunta 1=station / 2=AP.");
  Serial.println("[APP] As configuracoes permanecem apos reboot e OTA.");
}

void loop() {
  ota.loop();
}
