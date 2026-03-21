#include <Arduino.h>
#include <SuperOTA.h>

SuperOTA ota;

namespace {
// Ajuste os valores abaixo para o seu ambiente.
constexpr const char* kHostname = "superota-security";
constexpr const char* kApSsid = "SuperOTA-Security";
constexpr const char* kApPassword = "12345678";
constexpr const char* kDefaultOtaPassword = "TroqueEssaSenha123!";

// 0 = portal usa a mesma senha do OTA.
// 1 = portal usa senha separada.
constexpr uint8_t kUseSeparatePortalPassword = 0;
constexpr const char* kDefaultPortalPassword = "PortalSenhaSeparada123!";
}  // namespace

void setup() {
  ota.beginSerial(115200);
  ota.enableSerialConfigCommand(true, "configota");
  ota.enableTelnetSerial(true, 23);

  // Carrega configuracao salva pelo portal/NVS.
  ota.loadPreferences();

  // Defaults iniciais (somente se ainda nao houver perfil salvo).
  if (!ota.hasStationCredentials()) {
    ota.setHostname(kHostname);
    ota.setPreferAccessPoint(true);
    ota.setAccessPointCredentials(kApSsid, kApPassword);

    // Adicione redes se quiser testar STA + fallback AP:
    // ota.addStationNetwork("MinhaRede", "MinhaSenha");

    ota.savePreferences();
    Serial.println("[APP] Perfil inicial salvo na NVS.");
  } else {
    Serial.println("[APP] Perfil carregado da NVS.");
  }

  // Define senha OTA somente se ainda nao existir.
  // (evita sobrescrever senha configurada pelo portal).
  if (!ota.otaPasswordEnabled()) {
    ota.setOtaPassword(kDefaultOtaPassword);
    Serial.println("[APP] Senha OTA inicial aplicada.");
  }

  // Politica de senha do portal.
  if (kUseSeparatePortalPassword) {
    ota.setUseOtaPasswordForPortal(false);
    ota.setPortalPassword(kDefaultPortalPassword);
    Serial.println("[APP] Portal com senha separada.");
  } else {
    ota.setUseOtaPasswordForPortal(true);
    Serial.println("[APP] Portal usando a mesma senha do OTA.");
  }

  // Persistencia dos ajustes de seguranca.
  ota.savePreferences();

  if (!ota.begin()) {
    Serial.println("[APP] Falha ao iniciar SuperOTA.");
    return;
  }

  Serial.println();
  Serial.println("[APP] ===== Teste de seguranca SuperOTA =====");
  Serial.println("[APP] 1) Digite 'configota' no serial para abrir portal.");
  Serial.println("[APP] 2) Login do portal: usuario 'admin'.");
  Serial.println("[APP] 3) OTA requer senha (auth) na porta 3232.");
  Serial.println("[APP] 4) URL portal em AP: http://192.168.4.1");
  Serial.println("[APP] =======================================");

  Serial.println("[APP] Exemplo PlatformIO para upload OTA com senha:");
  Serial.println("[APP]   upload_protocol = espota");
  Serial.println("[APP]   upload_port = superota-security.local");
  Serial.println("[APP]   upload_flags = --port=3232 --auth=TroqueEssaSenha123!");
}

void loop() {
  ota.loop();
}

