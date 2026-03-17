#include "SuperOTA.h"

#include <ArduinoOTA.h>
#include <cstdarg>
#include <cstdio>
#include <cstring>

namespace {
constexpr uint16_t kArduinoOtaPort = 3232;
constexpr char kPrefPreferAp[] = "preferAP";
constexpr char kPrefStaSsid[] = "staSsid";
constexpr char kPrefStaPass[] = "staPass";
constexpr char kPrefStaList[] = "staList";
constexpr char kPrefApSsid[] = "apSsid";
constexpr char kPrefApPass[] = "apPass";
constexpr char kPrefHostname[] = "hostname";
constexpr uint8_t kSafeP4ConnectAttempts = 2;
constexpr uint16_t kSafeP4RetryDelayMs = 250;
constexpr uint16_t kSafeP4LinkStabilizeDelayMs = 120;
constexpr uint16_t kConfigPortalPort = 80;

#if defined(CONFIG_IDF_TARGET_ESP32P4)
constexpr bool kBuildTargetIsP4 = true;
#else
constexpr bool kBuildTargetIsP4 = false;
#endif

String htmlEscape(const String& value) {
  String out;
  out.reserve(value.length() + 16);

  for (size_t i = 0; i < value.length(); ++i) {
    const char c = value[i];
    switch (c) {
      case '&':
        out += F("&amp;");
        break;
      case '<':
        out += F("&lt;");
        break;
      case '>':
        out += F("&gt;");
        break;
      case '\"':
        out += F("&quot;");
        break;
      case '\'':
        out += F("&#39;");
        break;
      default:
        out += c;
        break;
    }
  }

  return out;
}
}  // namespace

SuperOTA::SuperOTA()
    : _enabled(true),
      _configured(false),
      _stationMode(false),
      _mdnsRunning(false),
      _stationTimeoutMs(kDefaultStationTimeoutMs),
      _stationCount(0),
      _apSsid(kDefaultApSsid),
      _apPassword(kDefaultApPassword),
      _hostname(kDefaultHostname),
      _preferAccessPoint(false),
      _safeP4Mode(kBuildTargetIsP4),
      _telnetServer(23),
      _telnetClient(),
      _telnetEnabled(true),
      _telnetServerActive(false),
      _telnetPort(23),
      _telnetBuffer(),
      _serialConfigEnabled(true),
      _serialConfigCommand("configota"),
      _serialBuffer(),
      _configServer(nullptr),
      _configPortalRunning(false),
      _configPortalUsesAp(false),
      _awaitingPortalModeChoice(false),
      _dnsServer(),
      _dnsRunning(false),
      _prefs() {}

void SuperOTA::beginSerial(uint32_t baud) {
  Serial.begin(baud);
  println(F("[SuperOTA] Serial iniciada."));
  println(F("[SuperOTA] Digite 'configota' para abrir portal de configuracao."));
}

void SuperOTA::setStationCredentials(const char* ssid, const char* password) {
  clearStationNetworks();
  addStationNetwork(ssid, password);
}

bool SuperOTA::addStationNetwork(const char* ssid, const char* password) {
  String ssidValue = (ssid != nullptr) ? ssid : "";
  String passwordValue = (password != nullptr) ? password : "";

  ssidValue.trim();
  if (ssidValue.isEmpty()) {
    Serial.println(F("[SuperOTA] Ignorando rede station com SSID vazio."));
    return false;
  }

  for (uint8_t i = 0; i < _stationCount; ++i) {
    if (_stationSsids[i] == ssidValue) {
      _stationPasswords[i] = passwordValue;
      Serial.print(F("[SuperOTA] Rede station atualizada: "));
      Serial.println(ssidValue);
      return true;
    }
  }

  if (_stationCount >= kMaxStationNetworks) {
    Serial.println(F("[SuperOTA] Lista station cheia. Aumente kMaxStationNetworks."));
    return false;
  }

  _stationSsids[_stationCount] = ssidValue;
  _stationPasswords[_stationCount] = passwordValue;
  ++_stationCount;

  Serial.print(F("[SuperOTA] Rede station adicionada: "));
  Serial.println(ssidValue);
  return true;
}

void SuperOTA::clearStationNetworks() {
  for (uint8_t i = 0; i < kMaxStationNetworks; ++i) {
    _stationSsids[i] = "";
    _stationPasswords[i] = "";
  }
  _stationCount = 0;
}

uint8_t SuperOTA::stationNetworkCount() const {
  return _stationCount;
}

void SuperOTA::setAccessPointCredentials(const char* ssid, const char* password) {
  if (ssid == nullptr || ssid[0] == '\0') {
    _apSsid = kDefaultApSsid;
  } else {
    _apSsid = ssid;
  }

  if (password == nullptr) {
    _apPassword = kDefaultApPassword;
  } else {
    _apPassword = password;
  }
}

void SuperOTA::setHostname(const char* hostname) {
  _hostname = normalizeHostname(hostname);

  if (_configured) {
    ArduinoOTA.setHostname(_hostname.c_str());
  }

  if (_mdnsRunning) {
    startMDNS();
  }
}

void SuperOTA::setPreferAccessPoint(bool prefer) {
  _preferAccessPoint = prefer;
}

void SuperOTA::setStationConnectTimeoutMs(uint32_t timeoutMs) {
  _stationTimeoutMs = (timeoutMs == 0) ? kDefaultStationTimeoutMs : timeoutMs;
}

void SuperOTA::setSafeP4Mode(bool enable) {
  _safeP4Mode = enable;
}

bool SuperOTA::safeP4Mode() const {
  return _safeP4Mode;
}

bool SuperOTA::isP4Target() const {
  return kBuildTargetIsP4;
}

void SuperOTA::enableTelnetSerial(bool enable, uint16_t port) {
  _telnetEnabled = enable;
  if (port != 0) {
    _telnetPort = port;
  }

  if (!_telnetEnabled) {
    stopTelnetServer();
    return;
  }

  if (_configured && (_stationMode || WiFi.getMode() == WIFI_AP)) {
    startTelnetServer(_telnetPort);
  }
}

bool SuperOTA::telnetSerialEnabled() const {
  return _telnetEnabled;
}

uint16_t SuperOTA::telnetPort() const {
  return _telnetPort;
}

bool SuperOTA::telnetClientConnected() const {
  return _telnetEnabled && _telnetClient && _telnetClient.connected();
}

void SuperOTA::enableSerialConfigCommand(bool enable, const char* command) {
  _serialConfigEnabled = enable;

  if (command != nullptr && command[0] != '\0') {
    _serialConfigCommand = command;
    _serialConfigCommand.trim();
    _serialConfigCommand.toLowerCase();
  }

  if (_serialConfigCommand.isEmpty()) {
    _serialConfigCommand = "configota";
  }
}

bool SuperOTA::startConfigPortal(const char* apSsid, const char* apPassword) {
  return startConfigPortalOnAccessPoint(apSsid, apPassword);
}

bool SuperOTA::startConfigPortalOnStation() {
  if (_configPortalRunning) {
    Serial.println(F("[SuperOTA] Portal ja esta ativo."));
    return true;
  }

  if (!_configured || !_stationMode || WiFi.status() != WL_CONNECTED) {
    Serial.println(F("[SuperOTA] Nao esta em station conectado para abrir portal no station."));
    return false;
  }

  if (_configServer != nullptr) {
    delete _configServer;
    _configServer = nullptr;
  }

  if (!_mdnsRunning) {
    startMDNS();
  }

  _configServer = new WebServer(kConfigPortalPort);
  _configServer->on("/", HTTP_GET, [this]() { handleConfigPage(); });
  _configServer->on("/save", HTTP_POST, [this]() { handleConfigSave(); });
  _configServer->on("/scan", HTTP_GET, [this]() { handleConfigScan(); });
  _configServer->onNotFound([this]() { handleConfigPage(); });
  _configServer->begin();
  _configPortalRunning = true;
  _configPortalUsesAp = false;
  _awaitingPortalModeChoice = false;
  startTelnetServer(_telnetPort);

  printConfigPortalEndpoints();
  return true;
}

bool SuperOTA::startConfigPortalOnAccessPoint(const char* apSsid, const char* apPassword) {
  if (_configPortalRunning) {
    Serial.println(F("[SuperOTA] Portal ja esta ativo."));
    return true;
  }

  String configSsid = (apSsid != nullptr && apSsid[0] != '\0') ? apSsid : kDefaultConfigApSsid;
  String configPass = (apPassword != nullptr) ? apPassword : "";
  if (configPass.length() > 0 && configPass.length() < 8) {
    Serial.println(F("[SuperOTA] Senha de portal invalida (<8). Abrindo portal sem senha."));
    configPass = "";
  }

  _configured = false;
  _stationMode = false;
  stopNetworking();
  WiFi.mode(WIFI_AP);
  if (_hostname.length()) {
    WiFi.softAPsetHostname(_hostname.c_str());
  }

  const bool hasPassword = configPass.length() > 0;
  const bool apOk = hasPassword ? WiFi.softAP(configSsid.c_str(), configPass.c_str())
                                : WiFi.softAP(configSsid.c_str());
  if (!apOk) {
    Serial.println(F("[SuperOTA] Falha ao abrir AP do portal."));
    return false;
  }

  if (_configServer != nullptr) {
    delete _configServer;
    _configServer = nullptr;
  }

  _dnsServer.stop();
  _dnsServer.setErrorReplyCode(DNSReplyCode::NoError);
  _dnsServer.start(53, "*", WiFi.softAPIP());
  _dnsRunning = true;

  _configServer = new WebServer(kConfigPortalPort);
  _configServer->on("/", HTTP_GET, [this]() { handleConfigPage(); });
  _configServer->on("/save", HTTP_POST, [this]() { handleConfigSave(); });
  _configServer->on("/scan", HTTP_GET, [this]() { handleConfigScan(); });
  _configServer->on("/generate_204", HTTP_GET, [this]() { handleConfigPage(); });
  _configServer->on("/gen_204", HTTP_GET, [this]() { handleConfigPage(); });
  _configServer->on("/hotspot-detect.html", HTTP_GET, [this]() { handleConfigPage(); });
  _configServer->on("/connecttest.txt", HTTP_GET, [this]() { handleConfigPage(); });
  _configServer->on("/ncsi.txt", HTTP_GET, [this]() { handleConfigPage(); });
  _configServer->on("/fwlink", HTTP_GET, [this]() { handleConfigPage(); });
  _configServer->onNotFound([this]() { handleConfigPage(); });
  _configServer->begin();
  _configPortalRunning = true;
  _configPortalUsesAp = true;
  _awaitingPortalModeChoice = false;
  startTelnetServer(_telnetPort);

  printConfigPortalEndpoints();
  return true;
}

void SuperOTA::printConfigPortalEndpoints() const {
  Serial.println(F("[SuperOTA] Portal de configuracao ativo."));
  if (_configPortalUsesAp) {
    Serial.print(F("[SuperOTA] AP IP: http://"));
    Serial.println(WiFi.softAPIP());
  } else {
    Serial.print(F("[SuperOTA] Station IP: http://"));
    Serial.println(WiFi.localIP());
    Serial.print(F("[SuperOTA] mDNS: http://"));
    Serial.print(_hostname);
    Serial.println(F(".local"));
  }
  Serial.print(F("[SuperOTA] Porta HTTP do portal: "));
  Serial.println(kConfigPortalPort);
  if (_telnetEnabled) {
    Serial.print(F("[SuperOTA] Telnet serial: socket://"));
    Serial.print(_hostname);
    Serial.print(F(".local:"));
    Serial.println(_telnetPort);
  }
  Serial.println(F("[SuperOTA] Digite 'config-stop' para encerrar portal."));
}

void SuperOTA::stopConfigPortal(bool resumeAuto) {
  if (_configServer != nullptr) {
    _configServer->stop();
    delete _configServer;
    _configServer = nullptr;
  }
  _configPortalRunning = false;
  _awaitingPortalModeChoice = false;

  if (_dnsRunning) {
    _dnsServer.stop();
    _dnsRunning = false;
  }
  stopTelnetServer();

  if (_configPortalUsesAp) {
    WiFi.softAPdisconnect(true);
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
  }
  _configPortalUsesAp = false;

  if (resumeAuto && _enabled) {
    Serial.println(F("[SuperOTA] Encerrando portal e retomando configuracao OTA."));
    if (_configured && _stationMode && WiFi.status() == WL_CONNECTED) {
      startMDNS();
      startTelnetServer(_telnetPort);
    } else {
      configureAuto();
    }
  }
}

bool SuperOTA::configPortalRunning() const {
  return _configPortalRunning;
}

bool SuperOTA::begin() {
  if (!_enabled) {
    return false;
  }

  return configureAuto();
}

bool SuperOTA::beginStation(const char* ssid, const char* password) {
  _configured = false;
  if (!_enabled) {
    return false;
  }

  if (ssid == nullptr || ssid[0] == '\0') {
    Serial.println(F("[SuperOTA] SSID station invalido."));
    return false;
  }

  const uint8_t attempts = _safeP4Mode ? kSafeP4ConnectAttempts : 1;
  bool connected = false;

  for (uint8_t attempt = 1; attempt <= attempts; ++attempt) {
    if (_safeP4Mode && attempts > 1) {
      Serial.print(F("[SuperOTA] Modo seguro P4: tentativa STA "));
      Serial.print(attempt);
      Serial.print('/');
      Serial.println(attempts);
    }

    if (beginStationOnce(ssid, password)) {
      connected = true;
      break;
    }

    if (_safeP4Mode && attempt < attempts) {
      delay(kSafeP4RetryDelayMs);
    }
  }

  if (!connected) {
    Serial.println(F("[SuperOTA] Falha ao conectar em station."));
    return false;
  }

  _stationMode = true;
  configureOTAHandlers();
  ArduinoOTA.begin();
  startMDNS();
  startTelnetServer(_telnetPort);

  _configured = true;

  Serial.print(F("[SuperOTA] OTA ativo em station. IP: "));
  Serial.println(WiFi.localIP());
  return true;
}

bool SuperOTA::beginAccessPoint(const char* ssid, const char* password) {
  _configured = false;
  if (!_enabled) {
    return false;
  }

  const char* apSsid = (ssid != nullptr && ssid[0] != '\0') ? ssid : _apSsid.c_str();
  const char* apPassword = (password != nullptr) ? password : _apPassword.c_str();

  const bool hasPassword = (apPassword != nullptr && apPassword[0] != '\0');
  if (hasPassword && strlen(apPassword) < 8) {
    Serial.println(F("[SuperOTA] Senha de AP precisa ter 8+ caracteres."));
    return false;
  }

  const uint8_t attempts = _safeP4Mode ? kSafeP4ConnectAttempts : 1;
  bool apStarted = false;

  for (uint8_t attempt = 1; attempt <= attempts; ++attempt) {
    if (_safeP4Mode && attempts > 1) {
      Serial.print(F("[SuperOTA] Modo seguro P4: tentativa AP "));
      Serial.print(attempt);
      Serial.print('/');
      Serial.println(attempts);
    }

    if (beginAccessPointOnce(apSsid, apPassword)) {
      apStarted = true;
      break;
    }

    if (_safeP4Mode && attempt < attempts) {
      delay(kSafeP4RetryDelayMs);
    }
  }

  if (!apStarted) {
    Serial.println(F("[SuperOTA] Falha ao iniciar access point."));
    return false;
  }

  _stationMode = false;
  configureOTAHandlers();
  ArduinoOTA.begin();
  startMDNS();
  startTelnetServer(_telnetPort);

  _configured = true;

  Serial.print(F("[SuperOTA] OTA ativo em AP. SSID: "));
  Serial.println(apSsid);
  Serial.print(F("[SuperOTA] IP do AP: "));
  Serial.println(WiFi.softAPIP());
  return true;
}

void SuperOTA::loop() {
  processSerialCommands();
  processTelnet();

  if (_configPortalRunning && _configServer != nullptr) {
    if (_dnsRunning) {
      _dnsServer.processNextRequest();
    }
    _configServer->handleClient();
    return;
  }

  if (_enabled && _configured) {
    ArduinoOTA.handle();
  }
}

void SuperOTA::enable(bool enable) {
  if (_enabled == enable) {
    return;
  }

  _enabled = enable;
  if (!_enabled) {
    _configured = false;
    stopNetworking();
  } else {
    configureAuto();
  }
}

bool SuperOTA::enabled() const {
  return _enabled;
}

bool SuperOTA::isConfigured() const {
  return _configured;
}

bool SuperOTA::isStationMode() const {
  return _configured && _stationMode;
}

bool SuperOTA::hasStationCredentials() const {
  return _stationCount > 0;
}

IPAddress SuperOTA::ip() const {
  if (!_configured) {
    return IPAddress();
  }

  return _stationMode ? WiFi.localIP() : WiFi.softAPIP();
}

bool SuperOTA::loadPreferences() {
  if (!_prefs.begin(kPrefsNamespace, true)) {
    Serial.println(F("[SuperOTA] NVS indisponivel para leitura."));
    return false;
  }

  _preferAccessPoint = _prefs.getBool(kPrefPreferAp, _preferAccessPoint);
  _apSsid = _prefs.getString(kPrefApSsid, _apSsid);
  _apPassword = _prefs.getString(kPrefApPass, _apPassword);
  _hostname = normalizeHostname(_prefs.getString(kPrefHostname, _hostname).c_str());

  clearStationNetworks();
  const String listRaw = _prefs.getString(kPrefStaList, "");
  if (listRaw.length() > 0) {
    parseAndSetStationList(listRaw);
  } else {
    // Backward compatibility with old single-station keys.
    const String staSsid = _prefs.getString(kPrefStaSsid, "");
    const String staPass = _prefs.getString(kPrefStaPass, "");
    if (staSsid.length() > 0) {
      addStationNetwork(staSsid.c_str(), staPass.c_str());
    }
  }

  _prefs.end();

  if (_apSsid.isEmpty()) {
    _apSsid = kDefaultApSsid;
  }

  Serial.print(F("[SuperOTA] Preferencias carregadas. Redes station: "));
  Serial.println(_stationCount);
  return true;
}

bool SuperOTA::savePreferences() {
  if (!_prefs.begin(kPrefsNamespace, false)) {
    Serial.println(F("[SuperOTA] NVS indisponivel para escrita."));
    return false;
  }

  _prefs.putBool(kPrefPreferAp, _preferAccessPoint);
  _prefs.putString(kPrefApSsid, _apSsid);
  _prefs.putString(kPrefApPass, _apPassword);
  _prefs.putString(kPrefHostname, _hostname);

  // Backward compatibility keys (first station network only).
  const String firstSsid = (_stationCount > 0) ? _stationSsids[0] : String();
  const String firstPass = (_stationCount > 0) ? _stationPasswords[0] : String();
  _prefs.putString(kPrefStaSsid, firstSsid);
  _prefs.putString(kPrefStaPass, firstPass);

  String stationRaw;
  stationListToMultiline(stationRaw);
  _prefs.putString(kPrefStaList, stationRaw);

  _prefs.end();

  Serial.println(F("[SuperOTA] Preferencias salvas."));
  return true;
}

bool SuperOTA::clearPreferences() {
  if (!_prefs.begin(kPrefsNamespace, false)) {
    Serial.println(F("[SuperOTA] NVS indisponivel para limpeza."));
    return false;
  }

  const bool cleared = _prefs.clear();
  _prefs.end();

  if (cleared) {
    clearStationNetworks();
  }

  Serial.println(cleared ? F("[SuperOTA] Preferencias removidas.")
                         : F("[SuperOTA] Falha ao limpar preferencias."));
  return cleared;
}

bool SuperOTA::configureAuto() {
  if (!_enabled) {
    return false;
  }

  if (_configured) {
    return true;
  }

  bool success = false;
  const bool hasStation = (_stationCount > 0);

  if (_preferAccessPoint) {
    success = beginAccessPoint(_apSsid.c_str(), _apPassword.c_str());
    if (!success && hasStation) {
      Serial.println(F("[SuperOTA] AP falhou, tentando station da lista."));
      success = beginStationFromKnownNetworks();
    }
  } else {
    if (hasStation) {
      success = beginStationFromKnownNetworks();
    }
    if (!success) {
      Serial.println(F("[SuperOTA] Station indisponivel, iniciando AP."));
      success = beginAccessPoint(_apSsid.c_str(), _apPassword.c_str());
    }
  }

  return success;
}

bool SuperOTA::beginStationFromKnownNetworks() {
  if (_stationCount == 0) {
    Serial.println(F("[SuperOTA] Nenhuma rede station cadastrada."));
    return false;
  }

  Serial.println(F("[SuperOTA] Escaneando redes WiFi para selecionar station..."));

  stopNetworking();
  WiFi.mode(WIFI_STA);
  delay(80);

  const int found = WiFi.scanNetworks(false, true);
  if (found <= 0) {
    Serial.println(F("[SuperOTA] Nenhuma rede visivel no scan."));
    WiFi.scanDelete();
    return false;
  }

  int selectedCfg = -1;
  int selectedRssi = -1000;

  for (uint8_t cfg = 0; cfg < _stationCount; ++cfg) {
    int bestRssiForCfg = -1000;
    bool foundThisCfg = false;

    for (int i = 0; i < found; ++i) {
      if (WiFi.SSID(i) == _stationSsids[cfg]) {
        foundThisCfg = true;
        const int rssi = WiFi.RSSI(i);
        if (rssi > bestRssiForCfg) {
          bestRssiForCfg = rssi;
        }
      }
    }

    if (foundThisCfg) {
      selectedCfg = cfg;
      selectedRssi = bestRssiForCfg;
      break;  // keeps user-defined priority order
    }
  }

  WiFi.scanDelete();

  if (selectedCfg < 0) {
    Serial.println(F("[SuperOTA] Nenhuma rede cadastrada foi encontrada no scan."));
    for (uint8_t i = 0; i < _stationCount; ++i) {
      Serial.print(F("[SuperOTA] Cadastrada: "));
      Serial.println(_stationSsids[i]);
    }
    return false;
  }

  Serial.print(F("[SuperOTA] Rede selecionada: "));
  Serial.print(_stationSsids[selectedCfg]);
  Serial.print(F(" (RSSI "));
  Serial.print(selectedRssi);
  Serial.println(F(" dBm)"));

  return beginStation(_stationSsids[selectedCfg].c_str(), _stationPasswords[selectedCfg].c_str());
}

bool SuperOTA::beginStationOnce(const char* ssid, const char* password) {
  stopNetworking();
  if (_safeP4Mode) {
    delay(kSafeP4RetryDelayMs);
  }

  WiFi.mode(WIFI_STA);

  if (_hostname.length()) {
    WiFi.setHostname(_hostname.c_str());
  }

  if (password != nullptr && password[0] != '\0') {
    WiFi.begin(ssid, password);
  } else {
    WiFi.begin(ssid);
  }

  Serial.print(F("[SuperOTA] Conectando em station: "));
  Serial.println(ssid);

  const unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && (millis() - start) < _stationTimeoutMs) {
    delay(400);
    Serial.print('.');
  }
  Serial.println();

  if (WiFi.status() != WL_CONNECTED) {
    return false;
  }

  if (_safeP4Mode) {
    delay(kSafeP4LinkStabilizeDelayMs);
  }

  return hasValidIp(WiFi.localIP());
}

bool SuperOTA::beginAccessPointOnce(const char* ssid, const char* password) {
  stopNetworking();
  if (_safeP4Mode) {
    delay(kSafeP4RetryDelayMs);
  }

  WiFi.mode(WIFI_AP);

  if (_hostname.length()) {
    WiFi.softAPsetHostname(_hostname.c_str());
  }

  const bool hasPassword = (password != nullptr && password[0] != '\0');
  const bool apOk = hasPassword ? WiFi.softAP(ssid, password) : WiFi.softAP(ssid);
  if (!apOk) {
    return false;
  }

  if (_safeP4Mode) {
    delay(kSafeP4LinkStabilizeDelayMs);
  }

  return hasValidIp(WiFi.softAPIP());
}

void SuperOTA::configureOTAHandlers() {
  ArduinoOTA.setPort(kArduinoOtaPort);
  ArduinoOTA.setHostname(_hostname.c_str());

  ArduinoOTA.onStart([this]() {
    this->println(F("[SuperOTA] OTA start."));
  });

  ArduinoOTA.onEnd([this]() {
    this->println(F("[SuperOTA] OTA end."));
  });

  ArduinoOTA.onProgress([this](unsigned int progress, unsigned int total) {
    const unsigned int percent = (total == 0U) ? 0U : (progress * 100U) / total;
    this->print(F("[SuperOTA] OTA progresso: "));
    this->print(percent);
    this->println(F("%"));
  });

  ArduinoOTA.onError([this](ota_error_t error) {
    this->print(F("[SuperOTA] Erro OTA: "));
    this->println(static_cast<int>(error));
  });
}

void SuperOTA::startMDNS() {
  if (_mdnsRunning) {
    MDNS.end();
    _mdnsRunning = false;
  }

  if (MDNS.begin(_hostname.c_str())) {
    _mdnsRunning = true;
    MDNS.addService("arduino", "tcp", kArduinoOtaPort);
    print(F("[SuperOTA] mDNS ativo em "));
    print(_hostname);
    println(F(".local"));
  } else {
    println(F("[SuperOTA] Falha ao iniciar mDNS."));
    if (_safeP4Mode) {
      println(F("[SuperOTA] Modo seguro P4: seguindo sem mDNS."));
    }
  }
}

void SuperOTA::stopNetworking() {
  stopTelnetServer();

  if (_mdnsRunning) {
    MDNS.end();
    _mdnsRunning = false;
  }
  if (_dnsRunning) {
    _dnsServer.stop();
    _dnsRunning = false;
  }

  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
}

bool SuperOTA::hasValidIp(const IPAddress& ip) const {
  return ip != IPAddress(static_cast<uint32_t>(0U));
}

String SuperOTA::normalizeHostname(const char* hostname) const {
  String normalized = (hostname != nullptr && hostname[0] != '\0') ? hostname : kDefaultHostname;
  normalized.trim();
  normalized.toLowerCase();
  normalized.replace(" ", "-");

  if (normalized.isEmpty()) {
    normalized = kDefaultHostname;
  }

  return normalized;
}

void SuperOTA::println() {
  Serial.println();
  if (_telnetEnabled && _telnetClient && _telnetClient.connected()) {
    _telnetClient.println();
  }
}

void SuperOTA::printf(const char* format, ...) {
  if (format == nullptr) {
    return;
  }

  char buffer[256];
  va_list args;
  va_start(args, format);
  vsnprintf(buffer, sizeof(buffer), format, args);
  va_end(args);

  broadcastRaw(buffer);
}

void SuperOTA::broadcastRaw(const char* message) {
  if (message == nullptr) {
    return;
  }

  Serial.print(message);
  if (_telnetEnabled && _telnetClient && _telnetClient.connected()) {
    _telnetClient.print(message);
  }
}

void SuperOTA::startTelnetServer(uint16_t port) {
  if (!_telnetEnabled) {
    return;
  }

  if (_telnetClient) {
    _telnetClient.stop();
  }

  _telnetServer.stop();
  _telnetServer = WiFiServer(port);
  _telnetServer.begin();
  _telnetServer.setNoDelay(true);
  _telnetServerActive = true;
  _telnetBuffer = "";

  print(F("[SuperOTA] Telnet serial ativo em socket://"));
  print(_hostname);
  print(F(".local:"));
  println(port);
}

void SuperOTA::stopTelnetServer() {
  if (_telnetClient) {
    _telnetClient.stop();
  }
  if (_telnetServerActive) {
    _telnetServer.stop();
  }
  _telnetServerActive = false;
  _telnetBuffer = "";
}

void SuperOTA::processTelnet() {
  if (!_telnetEnabled || !_telnetServerActive) {
    return;
  }

  if (!_telnetClient || !_telnetClient.connected()) {
    if (_telnetClient) {
      _telnetClient.stop();
    }
    WiFiClient incoming = _telnetServer.available();
    if (incoming && incoming.connected()) {
      _telnetClient = incoming;
      _telnetClient.println(F("[SuperOTA] Telnet conectado."));
      _telnetClient.println(F("[SuperOTA] Comandos: configota, config-stop, config-help."));
      _telnetClient.print(F("[SuperOTA] Host: "));
      _telnetClient.println(_hostname);
    }
    return;
  }

  while (_telnetClient.available() > 0) {
    const char c = static_cast<char>(_telnetClient.read());
    if (c == '\n' || c == '\r') {
      if (_telnetBuffer.length() == 0) {
        continue;
      }
      String command = _telnetBuffer;
      _telnetBuffer = "";
      command.trim();
      command.toLowerCase();
      processCommandLine(command);
      continue;
    }

    if (_telnetBuffer.length() < 64) {
      _telnetBuffer += c;
    }
  }
}

void SuperOTA::processSerialCommands() {
  if (!_serialConfigEnabled) {
    return;
  }

  while (Serial.available() > 0) {
    const char c = static_cast<char>(Serial.read());

    if (c == '\n' || c == '\r') {
      if (_serialBuffer.length() == 0) {
        continue;
      }

      String command = _serialBuffer;
      _serialBuffer = "";
      command.trim();
      command.toLowerCase();
      processCommandLine(command);
      continue;
    }

    if (_serialBuffer.length() < 64) {
      _serialBuffer += c;
    }
  }
}

void SuperOTA::processCommandLine(const String& command) {
  if (_awaitingPortalModeChoice) {
    if (command == "1") {
      println(F("[SuperOTA] Escolha 1: abrir portal no station."));
      startConfigPortalOnStation();
    } else if (command == "2") {
      println(F("[SuperOTA] Escolha 2: abrir portal em AP."));
      startConfigPortalOnAccessPoint(nullptr, nullptr);
    } else if (command == "config-stop") {
      _awaitingPortalModeChoice = false;
      println(F("[SuperOTA] Escolha de modo cancelada."));
    } else {
      println(F("[SuperOTA] Opcao invalida. Digite 1 (station) ou 2 (ap)."));
    }
    return;
  }

  if (command == _serialConfigCommand) {
    if (_configured && _stationMode && WiFi.status() == WL_CONNECTED) {
      _awaitingPortalModeChoice = true;
      println(F("[SuperOTA] Voce esta em station conectado."));
      println(F("[SuperOTA] Onde deseja abrir a pagina de configuracao?"));
      println(F("[SuperOTA] 1 = Station (hostname.local)"));
      println(F("[SuperOTA] 2 = Access Point (captive portal)"));
    } else {
      println(F("[SuperOTA] Abrindo portal em AP (modo padrao)."));
      startConfigPortalOnAccessPoint(nullptr, nullptr);
    }
    return;
  }

  if (command == "config-stop") {
    println(F("[SuperOTA] Comando recebido: encerrando portal."));
    stopConfigPortal(true);
    return;
  }

  if (command == "config-help") {
    print(F("[SuperOTA] Comandos: "));
    print(_serialConfigCommand);
    println(F(", config-stop, config-help, 1, 2"));
    return;
  }

  print(F("[SuperOTA] Comando desconhecido: "));
  println(command);
}

void SuperOTA::handleConfigPage() {
  if (_configServer == nullptr) {
    return;
  }

  String stationText;
  stationListToMultiline(stationText);
  String accessHint;
  if (_configPortalUsesAp) {
    accessHint = String("AP: http://") + WiFi.softAPIP().toString();
  } else {
    accessHint = String("Station: http://") + _hostname + ".local (ou http://" + WiFi.localIP().toString() + ")";
  }

  String html =
      F("<!DOCTYPE html><html><head><meta charset='utf-8'/>"
        "<meta name='viewport' content='width=device-width,initial-scale=1'/>"
        "<title>SuperOTA Config</title>"
        "<style>body{font-family:Arial,sans-serif;background:#0b1220;color:#eaf0ff;max-width:980px;"
        "margin:22px auto;padding:0 16px;}main{background:#162132;padding:22px;border-radius:12px;"
        "box-shadow:0 14px 26px rgba(0,0,0,.35);}h1{margin:0 0 10px 0;}p{color:#b8c9f0;line-height:1.45;}"
        "label{display:block;font-weight:700;margin:12px 0 4px;}input,textarea{width:100%;padding:10px;"
        "border:1px solid #32456c;border-radius:8px;background:#0f1a2e;color:#eaf0ff;}textarea{min-height:140px;}"
        "button{margin-top:14px;padding:11px 14px;border:0;border-radius:8px;background:#315ea8;"
        "color:#fff;font-weight:700;cursor:pointer;}button:hover{background:#3d74cc;}"
        "small{color:#9eb5e8;}a{color:#7db7ff;}code{background:#0f1a2e;border:1px solid #32456c;padding:1px 6px;"
        "border-radius:6px;}.hint{padding:10px 12px;border-radius:8px;background:#0f1a2e;border:1px solid #32456c;"
        "margin:10px 0 6px 0;}</style></head><body><main>");

  html += F("<h1>SuperOTA - Configuracao</h1><p>Edite os campos e clique em salvar. "
            "Formato da lista station: uma rede por linha em <code>SSID;senha</code>.</p>");
  html += F("<p class='hint'><strong>Acesso atual:</strong> ");
  html += htmlEscape(accessHint);
  html += F(" - porta 80</p>");
  html += F("<p><a href='/scan' target='_blank'>Ver redes detectadas agora</a></p>");

  html += F("<form method='post' action='/save'>");
  html += F("<label>Hostname</label><input name='hostname' value='");
  html += htmlEscape(_hostname);
  html += F("'/>\n");

  html += F("<label><input type='checkbox' name='preferAP' ");
  if (_preferAccessPoint) {
    html += F("checked");
  }
  html += F("/> Iniciar priorizando Access Point</label>");

  html += F("<label>AP SSID</label><input name='apSsid' value='");
  html += htmlEscape(_apSsid);
  html += F("'/>\n");

  html += F("<label>AP Senha (vazio = AP aberto)</label><input name='apPassword' value='");
  html += htmlEscape(_apPassword);
  html += F("'/>\n");

  html += F("<label>Lista Station</label><textarea name='stationList'>");
  html += htmlEscape(stationText);
  html += F("</textarea>");
  html += F("<small>Exemplo: MinhaCasa;senha123</small>");

  html += F("<button type='submit'>Salvar e aplicar</button></form>");
  html += F("</main></body></html>");

  _configServer->send(200, "text/html", html);
}

void SuperOTA::handleConfigSave() {
  if (_configServer == nullptr) {
    return;
  }

  const String hostname = _configServer->arg("hostname");
  const String apSsid = _configServer->arg("apSsid");
  const String apPassword = _configServer->arg("apPassword");
  const String stationList = _configServer->arg("stationList");

  setHostname(hostname.c_str());
  setPreferAccessPoint(_configServer->hasArg("preferAP"));
  setAccessPointCredentials(apSsid.c_str(), apPassword.c_str());
  parseAndSetStationList(stationList);

  savePreferences();

  String response = F("<!DOCTYPE html><html><head><meta charset='utf-8'/>"
                      "<meta name='viewport' content='width=device-width,initial-scale=1'/>"
                      "<style>body{font-family:Arial,sans-serif;background:#0b1220;color:#eaf0ff;"
                      "display:flex;justify-content:center;align-items:center;height:100vh;margin:0;}"
                      "div{background:#162132;padding:22px;border-radius:12px;max-width:420px;}"
                      "a{color:#7db7ff;}</style></head><body><div>"
                      "<h2>Configuracoes salvas</h2>");
  response += F("<p>Redes station cadastradas: ");
  response += String(_stationCount);
  response += F("</p><p>O portal sera fechado e o OTA sera reconfigurado automaticamente.</p>");
  response += F("<p>Voce pode fechar esta pagina.</p></div></body></html>");

  _configServer->send(200, "text/html", response);
  delay(120);
  stopConfigPortal(true);
}

void SuperOTA::handleConfigScan() {
  if (_configServer == nullptr) {
    return;
  }

  if (_configPortalUsesAp) {
    WiFi.mode(WIFI_AP_STA);
  } else {
    WiFi.mode(WIFI_STA);
  }
  delay(80);

  int found = WiFi.scanNetworks(false, true);
  String out = F("SSID\tRSSI\tCanal\n");

  if (found <= 0) {
    out += F("(nenhuma rede encontrada)\n");
  } else {
    for (int i = 0; i < found; ++i) {
      out += WiFi.SSID(i);
      out += '\t';
      out += String(WiFi.RSSI(i));
      out += '\t';
      out += String(WiFi.channel(i));
      out += '\n';
    }
  }

  WiFi.scanDelete();
  if (_configPortalUsesAp) {
    WiFi.mode(WIFI_AP);
  }
  _configServer->send(200, "text/plain", out);
}

void SuperOTA::stationListToMultiline(String& out) const {
  out = "";
  for (uint8_t i = 0; i < _stationCount; ++i) {
    out += _stationSsids[i];
    out += ';';
    out += _stationPasswords[i];
    out += '\n';
  }
}

void SuperOTA::parseAndSetStationList(const String& rawList) {
  clearStationNetworks();

  int start = 0;
  while (start <= rawList.length()) {
    int end = rawList.indexOf('\n', start);
    if (end < 0) {
      end = rawList.length();
    }

    String line = rawList.substring(start, end);
    line.trim();

    if (!line.isEmpty()) {
      int sep = line.indexOf(';');
      if (sep < 0) {
        sep = line.indexOf('\t');
      }

      String ssid;
      String pass;

      if (sep >= 0) {
        ssid = line.substring(0, sep);
        pass = line.substring(sep + 1);
      } else {
        ssid = line;
        pass = "";
      }

      ssid.trim();
      pass.trim();
      addStationNetwork(ssid.c_str(), pass.c_str());
    }

    start = end + 1;
  }
}
