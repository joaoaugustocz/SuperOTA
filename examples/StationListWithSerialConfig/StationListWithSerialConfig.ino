#include <SuperOTA.h>

SuperOTA ota;

void setup() {
  ota.beginSerial(115200);
  ota.enableSerialConfigCommand(true, "configota");

  // 1) Sempre tente carregar o perfil salvo antes de aplicar defaults.
  // Se voce configurou redes pelo portal (configota), elas estarao na NVS.
  ota.loadPreferences();

  // 2) So define valores de fabrica no primeiro boot (ou quando NVS estiver vazia).
  if (!ota.hasStationCredentials()) {
    ota.setHostname("superota-smart-sta");
    ota.setPreferAccessPoint(false);
    ota.setAccessPointCredentials("SuperOTA-Recovery", "12345678");

    // Lista priorizada: a primeira rede encontrada no scan sera tentada primeiro.
    ota.clearStationNetworks();
    ota.addStationNetwork("WiFi-Casa", "senha-casa");
    ota.addStationNetwork("WiFi-Escritorio", "senha-escritorio");
    ota.addStationNetwork("WiFi-Backup", "senha-backup");

    // Salva defaults iniciais para os proximos boots.
    ota.savePreferences();
    Serial.println("[APP] Perfil inicial salvo na NVS.");
  } else {
    Serial.println("[APP] Perfil carregado da NVS (sem sobrescrever).");
  }

  // 3) Debug opcional de captive/AP.
  ota.setDebugSummaryIntervalMs(30000);  // 30s quando debug estiver ativo

  if (!ota.begin()) {
    Serial.println("[APP] Falha ao iniciar OTA.");
  }

  Serial.println("[APP] Digite 'configota' no serial para abrir portal web.");
  Serial.println("[APP] O que salvar no portal permanece apos reboot e OTA.");
  Serial.println("[APP] Debug opcional: debug-on, debug-summary, debug-off.");
}

void loop() {
  ota.loop();
}
