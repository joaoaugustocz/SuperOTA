#include <Arduino.h>
#include <SuperOTA.h>

SuperOTA ota;

namespace {
constexpr const char* kHostname = "superota-p4-guide";
constexpr const char* kAccessPointSsid = "SuperOTA-Recovery";
constexpr const char* kAccessPointPassword = "12345678";
constexpr const char* kOtaPassword = "TroqueEssaSenha123!";

void applyDefaultProfile() {
  // Hostname usado em mDNS, OTA e Telnet serial.
  ota.setHostname(kHostname);

  // false = tenta station antes e cai para AP se nao encontrar rede valida.
  ota.setPreferAccessPoint(false);

  // AP operacional usado tanto no fallback normal quanto no portal em AP.
  ota.setAccessPointCredentials(kAccessPointSsid, kAccessPointPassword);

  // Lista priorizada de redes station.
  ota.clearStationNetworks();
  ota.addStationNetwork("MinhaRede", "MinhaSenha");
  ota.addStationNetwork("MinhaRedeBackup", "MinhaSenhaBackup");
}

void printGuide() {
  Serial.println();
  Serial.println("[GUIDE] ===== SuperOTA GuidedSetupP4 =====");
  Serial.println("[GUIDE] 1) No primeiro boot a biblioteca tenta carregar a NVS.");
  Serial.println("[GUIDE] 2) Se a NVS estiver vazia, este exemplo grava um perfil inicial.");
  Serial.println("[GUIDE] 3) Se a NVS falhar, os defaults rodam apenas em RAM.");
  Serial.println("[GUIDE] 4) Digite 'configota' para abrir o portal.");
  Serial.println("[GUIDE]    Em station: escolha 1=station ou 2=AP.");
  Serial.println("[GUIDE]    Em AP: o portal reutiliza o AP atual.");
  Serial.println("[GUIDE] 5) Portal em AP: http://192.168.4.1");
  Serial.println("[GUIDE] 6) OTA usa ArduinoOTA na porta 3232.");
  Serial.println("[GUIDE] ===================================");
}
}  // namespace

void setup() {
  ota.beginSerial(115200);
  ota.enableSerialConfigCommand(true, "configota");
  ota.enableTelnetSerial(true, 23);

  // No P4, o modo seguro ajuda nas transicoes com ESP-Hosted/C6.
  ota.setSafeP4Mode(true);

  const bool prefsLoaded = ota.loadPreferences();
  bool shouldPersist = false;

  if (!prefsLoaded) {
    Serial.println("[GUIDE] Aviso: NVS indisponivel. Usando defaults apenas em RAM.");
    applyDefaultProfile();
  } else if (!ota.hasStationCredentials()) {
    Serial.println("[GUIDE] NVS valida, mas ainda sem redes station. Gravando perfil inicial.");
    applyDefaultProfile();
    shouldPersist = true;
  } else {
    Serial.println("[GUIDE] Perfil carregado da NVS.");
  }

  // Exemplo de senha OTA inicial, sem sobrescrever o que o portal ja salvou.
  if (!ota.otaPasswordEnabled()) {
    ota.setOtaPassword(kOtaPassword);
    ota.setUseOtaPasswordForPortal(true);
    shouldPersist = prefsLoaded || shouldPersist;
    Serial.println("[GUIDE] Senha inicial de OTA/portal aplicada.");
  }

  if (prefsLoaded && shouldPersist) {
    ota.savePreferences();
    Serial.println("[GUIDE] Perfil persistido na NVS.");
  }

  if (!ota.begin()) {
    Serial.println("[GUIDE] Falha ao iniciar SuperOTA.");
    return;
  }

  printGuide();
}

void loop() {
  ota.loop();
}
