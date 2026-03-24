#include <Arduino.h>
#include <SuperOTA.h>

SuperOTA ota;
String serialBuffer;

void processLocalCommands() {
  while (Serial.available() > 0) {
    const char c = static_cast<char>(Serial.read());

    if (c == '\n' || c == '\r') {
      if (serialBuffer.length() == 0) {
        continue;
      }

      String cmd = serialBuffer;
      serialBuffer = "";
      cmd.trim();
      cmd.toLowerCase();

      if (cmd == "nvs-clear") {
        Serial.println("[APP] Limpando preferencias da SuperOTA...");
        if (ota.clearPreferences()) {
          Serial.println("[APP] NVS limpa com sucesso. Reiniciando em 1s.");
          delay(1000);
          ESP.restart();
        } else {
          Serial.println("[APP] Falha ao limpar NVS.");
        }
      } else if (cmd == "nvs-status") {
        Serial.print("[APP] Redes station cadastradas: ");
        Serial.println(ota.stationNetworkCount());
        Serial.print("[APP] Portal em execucao: ");
        Serial.println(ota.configPortalRunning() ? "sim" : "nao");
      } else {
        Serial.print("[APP] Comando local desconhecido: ");
        Serial.println(cmd);
      }
      continue;
    }

    if (serialBuffer.length() < 80) {
      serialBuffer += c;
    }
  }
}

void setup() {
  ota.beginSerial(115200);
  ota.enableSerialConfigCommand(true, "configota");

  // Padrao recomendado:
  // 1) Carrega NVS.
  // 2) Somente se NVS estiver vazia, grava defaults.
  const bool prefsLoaded = ota.loadPreferences();

  if (!prefsLoaded) {
    Serial.println("[APP] Aviso: NVS indisponivel. Usando defaults apenas em RAM.");
    ota.setHostname("superota-persist");
    ota.setPreferAccessPoint(false);
    ota.setAccessPointCredentials("SuperOTA-Recovery", "12345678");

    ota.clearStationNetworks();
    ota.addStationNetwork("MinhaRede", "MinhaSenha");
    ota.addStationNetwork("MinhaRedeBackup", "MinhaSenhaBackup");
  } else if (!ota.hasStationCredentials()) {
    ota.setHostname("superota-persist");
    ota.setPreferAccessPoint(false);
    ota.setAccessPointCredentials("SuperOTA-Recovery", "12345678");

    ota.clearStationNetworks();
    ota.addStationNetwork("MinhaRede", "MinhaSenha");
    ota.addStationNetwork("MinhaRedeBackup", "MinhaSenhaBackup");

    ota.savePreferences();
    Serial.println("[APP] Defaults iniciais salvos na NVS.");
  } else {
    Serial.println("[APP] Configuracao carregada da NVS.");
  }

  if (!ota.begin()) {
    Serial.println("[APP] Falha ao iniciar SuperOTA.");
  }

  Serial.println("[APP] Comandos:");
  Serial.println("[APP] - configota   -> abre portal e salva configuracoes");
  Serial.println("[APP] - em station, configota pergunta 1=station / 2=AP");
  Serial.println("[APP] - nvs-status -> mostra estado basico da NVS");
  Serial.println("[APP] - nvs-clear  -> limpa NVS e reinicia");
}

void loop() {
  ota.loop();              // Necessario para OTA/portal/telnet.
  processLocalCommands();  // Comandos extras deste exemplo.
}
