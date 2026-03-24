#include "SuperOTA.h"

#include <ArduinoOTA.h>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <esp_system.h>
#if defined(CONFIG_IDF_TARGET_ESP32P4)
#include <Network.h>
#endif

namespace {
constexpr uint16_t kArduinoOtaPort = 3232;
constexpr char kPrefPreferAp[] = "preferAP";
constexpr char kPrefStaSsid[] = "staSsid";
constexpr char kPrefStaPass[] = "staPass";
constexpr char kPrefStaList[] = "staList";
constexpr char kPrefApSsid[] = "apSsid";
constexpr char kPrefApPass[] = "apPass";
constexpr char kPrefHostname[] = "hostname";
constexpr char kPrefOtaPass[] = "otaPass";
constexpr char kPrefPortalPass[] = "portalPass";
constexpr char kPrefPortalUseOta[] = "portalUseOta";
constexpr uint8_t kSafeP4ConnectAttempts = 2;
constexpr uint16_t kSafeP4RetryDelayMs = 250;
constexpr uint16_t kSafeP4LinkStabilizeDelayMs = 120;
constexpr uint16_t kConfigPortalPort = 80;
constexpr uint16_t kConfigPortalDeferredStopMs = 1200;
constexpr char kSuperOtaVersion[] = "1.3.0";
constexpr char kConfigUiRevision[] = "ui-2026-03-23-portal-flow-v1";

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

bool isCommandStartByte(char c) {
  const uint8_t u = static_cast<uint8_t>(c);
  return ((u >= '0' && u <= '9') || (u >= 'A' && u <= 'Z') || (u >= 'a' && u <= 'z') || u == '_' || u == '-');
}

bool shouldIgnoreLeadingCommandByte(const String& buffer, char c) {
  if (buffer.length() != 0) {
    return false;
  }

  const uint8_t u = static_cast<uint8_t>(c);

  // Ignore UTF-8 BOM bytes and control bytes some terminals send before commands.
  if (u == 0xEF || u == 0xBB || u == 0xBF || u < 0x20 || u == 0x7F) {
    return true;
  }

  return !isCommandStartByte(c);
}

bool prepareP4AccessPointStack(bool safeMode) {
#if defined(CONFIG_IDF_TARGET_ESP32P4)
  if (!Network.begin()) {
    Serial.println(F("[SuperOTA] ERRO: Network.begin falhou no P4."));
    return false;
  }

  if (safeMode) {
    delay(kSafeP4RetryDelayMs);
  }

  if (!WiFi.AP.begin()) {
    Serial.println(F("[SuperOTA] ERRO: WiFi.AP.begin falhou no P4."));
    return false;
  }
#else
  (void)safeMode;
#endif
  return true;
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
      _otaPassword(""),
      _portalPassword(""),
      _useOtaPasswordForPortal(true),
      _preferAccessPoint(false),
      _safeP4Mode(kBuildTargetIsP4),
      _debugMetricsEnabled(false),
      _debugSummaryIntervalMs(kDefaultDebugSummaryIntervalMs),
      _debugLastSummaryMs(0),
      _debugEventHandlerId(0),
      _debugEventHandlerRegistered(false),
      _debugStats(),
      _debugClients(),
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
      _deferredPortalStop(false),
      _deferredPortalResumeAuto(true),
      _deferredPortalStopAfterMs(0),
      _deferredPortalRestart(false),
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

void SuperOTA::enableDebugMetrics(bool enable) {
  if (enable) {
    if (_debugEventHandlerRegistered) {
      WiFi.removeEvent(_debugEventHandlerId);
      _debugEventHandlerRegistered = false;
      _debugEventHandlerId = 0;
    }

    _debugEventHandlerId = WiFi.onEvent([this](arduino_event_id_t event, arduino_event_info_t info) {
      this->handleDebugWifiEvent(event, info);
    });
    _debugEventHandlerRegistered = true;

    _debugMetricsEnabled = true;
    resetDebugMetrics();
    println(F("[SuperOTA][DEBUG] Modo debug de metricas ativado."));
    println(F("[SuperOTA][DEBUG] Comandos: debug-summary, debug-off."));
    return;
  }

  _debugMetricsEnabled = false;
  if (_debugEventHandlerRegistered) {
    WiFi.removeEvent(_debugEventHandlerId);
    _debugEventHandlerRegistered = false;
    _debugEventHandlerId = 0;
  }
  println(F("[SuperOTA][DEBUG] Modo debug de metricas desativado."));
}

bool SuperOTA::debugMetricsEnabled() const {
  return _debugMetricsEnabled;
}

void SuperOTA::setDebugSummaryIntervalMs(uint32_t intervalMs) {
  _debugSummaryIntervalMs = (intervalMs == 0) ? kDefaultDebugSummaryIntervalMs : intervalMs;
}

uint32_t SuperOTA::debugSummaryIntervalMs() const {
  return _debugSummaryIntervalMs;
}

void SuperOTA::resetDebugMetrics() {
  _debugStats = DebugStats();
  _debugStats.bootMs = millis();
  _debugStats.dhcpMinMs = 0xFFFFFFFFUL;
  _debugLastSummaryMs = _debugStats.bootMs;

  for (uint8_t i = 0; i < kDebugMaxTrackedClients; ++i) {
    _debugClients[i].used = false;
    _debugClients[i].connectMs = 0;
    memset(_debugClients[i].mac, 0, sizeof(_debugClients[i].mac));
  }
}

bool SuperOTA::debugMacEqual(const uint8_t a[6], const uint8_t b[6]) {
  return memcmp(a, b, 6) == 0;
}

String SuperOTA::debugMacToString(const uint8_t mac[6]) {
  char buffer[18];
  snprintf(buffer, sizeof(buffer), "%02X:%02X:%02X:%02X:%02X:%02X",
           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  return String(buffer);
}

int SuperOTA::findDebugClientIndex(const uint8_t mac[6]) const {
  for (uint8_t i = 0; i < kDebugMaxTrackedClients; ++i) {
    if (_debugClients[i].used && debugMacEqual(_debugClients[i].mac, mac)) {
      return static_cast<int>(i);
    }
  }
  return -1;
}

int SuperOTA::getOrCreateDebugClientIndex(const uint8_t mac[6]) {
  const int existing = findDebugClientIndex(mac);
  if (existing >= 0) {
    return existing;
  }

  for (uint8_t i = 0; i < kDebugMaxTrackedClients; ++i) {
    if (!_debugClients[i].used) {
      _debugClients[i].used = true;
      memcpy(_debugClients[i].mac, mac, 6);
      _debugClients[i].connectMs = 0;
      return static_cast<int>(i);
    }
  }
  return -1;
}

void SuperOTA::handleDebugWifiEvent(arduino_event_id_t event, arduino_event_info_t info) {
  if (!_debugMetricsEnabled) {
    return;
  }

  const uint32_t nowMs = millis();
  const uint32_t elapsedMs = nowMs - _debugStats.bootMs;

  if (event == ARDUINO_EVENT_WIFI_AP_START) {
    ++_debugStats.apStartCount;
    Serial.printf("[SuperOTA][DEBUG] +%lums AP_START count=%lu\n", elapsedMs, _debugStats.apStartCount);
    return;
  }

  if (event == ARDUINO_EVENT_WIFI_AP_STACONNECTED) {
    ++_debugStats.connectCount;
    const wifi_event_ap_staconnected_t& e = info.wifi_ap_staconnected;
    const int idx = getOrCreateDebugClientIndex(e.mac);
    if (idx >= 0) {
      _debugClients[idx].connectMs = nowMs;
    }
    Serial.printf("[SuperOTA][DEBUG] +%lums AP_STACONNECTED mac=%02X:%02X:%02X:%02X:%02X:%02X aid=%u total=%lu\n",
                  elapsedMs, e.mac[0], e.mac[1], e.mac[2], e.mac[3], e.mac[4], e.mac[5], e.aid,
                  _debugStats.connectCount);
    return;
  }

  if (event == ARDUINO_EVENT_WIFI_AP_STAIPASSIGNED) {
    ++_debugStats.ipAssignedCount;
    const ip_event_ap_staipassigned_t& e = info.wifi_ap_staipassigned;
    const IPAddress ip(e.ip.addr);

    const int idx = findDebugClientIndex(e.mac);
    if (idx >= 0 && _debugClients[idx].connectMs > 0) {
      const uint32_t dhcpMs = nowMs - _debugClients[idx].connectMs;
      if (dhcpMs < _debugStats.dhcpMinMs) {
        _debugStats.dhcpMinMs = dhcpMs;
      }
      if (dhcpMs > _debugStats.dhcpMaxMs) {
        _debugStats.dhcpMaxMs = dhcpMs;
      }
      _debugStats.dhcpSumMs += dhcpMs;
      if (dhcpMs > kDebugLateDhcpThresholdMs) {
        ++_debugStats.lateDhcpCount;
      }
      _debugClients[idx].connectMs = 0;
      Serial.printf("[SuperOTA][DEBUG] +%lums AP_STAIPASSIGNED mac=%s ip=%s dhcpMs=%lu\n",
                    elapsedMs, debugMacToString(e.mac).c_str(), ip.toString().c_str(), dhcpMs);
    } else {
      ++_debugStats.unknownIpAssignedCount;
      Serial.printf("[SuperOTA][DEBUG] +%lums AP_STAIPASSIGNED mac=%s ip=%s (sem match)\n",
                    elapsedMs, debugMacToString(e.mac).c_str(), ip.toString().c_str());
    }
    return;
  }

  if (event == ARDUINO_EVENT_WIFI_AP_STADISCONNECTED) {
    ++_debugStats.disconnectCount;
    const wifi_event_ap_stadisconnected_t& e = info.wifi_ap_stadisconnected;
    Serial.printf("[SuperOTA][DEBUG] +%lums AP_STADISCONNECTED mac=%02X:%02X:%02X:%02X:%02X:%02X aid=%u total=%lu\n",
                  elapsedMs, e.mac[0], e.mac[1], e.mac[2], e.mac[3], e.mac[4], e.mac[5], e.aid,
                  _debugStats.disconnectCount);
  }
}

void SuperOTA::printDebugSummary() {
  if (!_debugMetricsEnabled) {
    println(F("[SuperOTA][DEBUG] Modo debug desativado."));
    return;
  }

  const uint32_t now = millis();
  const uint32_t uptime = now - _debugStats.bootMs;
  const uint8_t stationsNow = (_configPortalUsesAp || WiFi.getMode() == WIFI_AP || WiFi.getMode() == WIFI_AP_STA)
                                  ? WiFi.softAPgetStationNum()
                                  : 0;

  Serial.println();
  Serial.println(F("[SuperOTA][DEBUG] ===== Resumo ====="));
  Serial.print(F("[SuperOTA][DEBUG] Uptime (ms): "));
  Serial.println(uptime);
  Serial.print(F("[SuperOTA][DEBUG] AP SSID: "));
  Serial.println(WiFi.softAPSSID());
  Serial.print(F("[SuperOTA][DEBUG] AP IP: "));
  Serial.println(WiFi.softAPIP());
  Serial.print(F("[SuperOTA][DEBUG] Estacoes conectadas agora: "));
  Serial.println(stationsNow);
  Serial.print(F("[SuperOTA][DEBUG] AP_START: "));
  Serial.println(_debugStats.apStartCount);
  Serial.print(F("[SuperOTA][DEBUG] STACONNECTED: "));
  Serial.println(_debugStats.connectCount);
  Serial.print(F("[SuperOTA][DEBUG] STADISCONNECTED: "));
  Serial.println(_debugStats.disconnectCount);
  Serial.print(F("[SuperOTA][DEBUG] STAIPASSIGNED: "));
  Serial.println(_debugStats.ipAssignedCount);
  Serial.print(F("[SuperOTA][DEBUG] STAIPASSIGNED sem match: "));
  Serial.println(_debugStats.unknownIpAssignedCount);

  if (_debugStats.ipAssignedCount > 0) {
    const uint32_t avgDhcpMs = static_cast<uint32_t>(_debugStats.dhcpSumMs / _debugStats.ipAssignedCount);
    Serial.print(F("[SuperOTA][DEBUG] DHCP min/avg/max (ms): "));
    Serial.print(_debugStats.dhcpMinMs);
    Serial.print('/');
    Serial.print(avgDhcpMs);
    Serial.print('/');
    Serial.println(_debugStats.dhcpMaxMs);
    Serial.print(F("[SuperOTA][DEBUG] DHCP > "));
    Serial.print(kDebugLateDhcpThresholdMs);
    Serial.print(F("ms: "));
    Serial.println(_debugStats.lateDhcpCount);
  } else {
    Serial.println(F("[SuperOTA][DEBUG] DHCP: sem amostras."));
  }

  Serial.println(F("[SuperOTA][DEBUG] ===================="));
}

void SuperOTA::updateDebugSummary() {
  if (!_debugMetricsEnabled) {
    return;
  }

  const uint32_t now = millis();
  if ((now - _debugLastSummaryMs) < _debugSummaryIntervalMs) {
    return;
  }

  _debugLastSummaryMs = now;
  printDebugSummary();
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

bool SuperOTA::telnetClientConnected() {
  return _telnetEnabled && _telnetClient.connected();
}

void SuperOTA::setOtaPassword(const char* password) {
  if (password == nullptr || password[0] == '\0') {
    return;
  }

  const String newPassword(password);
  if (_otaPassword == newPassword) {
    return;
  }

  _otaPassword = newPassword;
  println(F("[SuperOTA] Senha OTA configurada."));

  if (_configured) {
    configureOTAHandlers();
    ArduinoOTA.begin();
  }
}

bool SuperOTA::otaPasswordEnabled() const {
  return _otaPassword.length() > 0;
}

void SuperOTA::setPortalPassword(const char* password) {
  if (password == nullptr || password[0] == '\0') {
    return;
  }

  _portalPassword = String(password);
  println(F("[SuperOTA] Senha do portal configurada."));
}

void SuperOTA::setUseOtaPasswordForPortal(bool enable) {
  _useOtaPasswordForPortal = enable;
}

bool SuperOTA::usingOtaPasswordForPortal() const {
  return _useOtaPasswordForPortal;
}

bool SuperOTA::portalPasswordEnabled() const {
  return effectivePortalPassword().length() > 0;
}

String SuperOTA::hostname() const {
  return _hostname;
}

String SuperOTA::accessPointSsid() const {
  return _apSsid;
}

bool SuperOTA::accessPointPasswordEnabled() const {
  return _apPassword.length() > 0;
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

String SuperOTA::effectivePortalPassword() const {
  if (_useOtaPasswordForPortal && _otaPassword.length() > 0) {
    return _otaPassword;
  }
  return _portalPassword;
}

bool SuperOTA::ensurePortalAuthentication() {
  if (_configServer == nullptr) {
    return false;
  }

  const String password = effectivePortalPassword();
  if (password.isEmpty()) {
    return true;
  }

  if (_configServer->authenticate("admin", password.c_str())) {
    return true;
  }

  _configServer->requestAuthentication();
  return false;
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
  auto renderConfigPage = [this]() {
    if (_configServer == nullptr) {
      return;
    }
    if (!ensurePortalAuthentication()) {
      return;
    }
    if (_configServer->method() == HTTP_HEAD) {
      _configServer->send(200, "text/html", "");
      return;
    }
    handleConfigPage();
  };
  _configServer->on("/", HTTP_ANY, renderConfigPage);
  _configServer->on("/save", HTTP_POST, [this]() {
    if (!ensurePortalAuthentication()) {
      return;
    }
    handleConfigSave();
  });
  _configServer->on("/scan", HTTP_GET, [this]() {
    if (!ensurePortalAuthentication()) {
      return;
    }
    handleConfigScan();
  });
  _configServer->onNotFound(renderConfigPage);
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

  const bool explicitSsidProvided = (apSsid != nullptr && apSsid[0] != '\0');
  const bool explicitPasswordProvided = (apPassword != nullptr);
  const bool usingConfiguredOperationalAp = !explicitSsidProvided && !explicitPasswordProvided;

  String configSsid = explicitSsidProvided ? String(apSsid) : _apSsid;
  if (configSsid.isEmpty()) {
    configSsid = kDefaultApSsid;
  }

  String configPass = explicitPasswordProvided ? String(apPassword) : _apPassword;
  if (configPass.length() > 0 && configPass.length() < 8) {
    Serial.println(F("[SuperOTA] Senha de portal invalida (<8). Abrindo portal sem senha."));
    configPass = "";
  }

  const IPAddress apIp(192, 168, 4, 1);
  const IPAddress apGateway(192, 168, 4, 1);
  const IPAddress apSubnet(255, 255, 255, 0);
  const IPAddress apDhcpLeaseStart(192, 168, 4, 2);
  const IPAddress apDns(192, 168, 4, 1);
  IPAddress currentApIp;

  bool reuseCurrentAp = false;
  if (usingConfiguredOperationalAp && _configured && !_stationMode) {
    const String activeSsid = WiFi.softAPSSID();
    const IPAddress activeIp = WiFi.softAPIP();
    if (activeSsid.length() > 0 && hasValidIp(activeIp)) {
      reuseCurrentAp = true;
      configSsid = activeSsid;
      currentApIp = activeIp;
      Serial.print(F("[SuperOTA] Reutilizando AP atual para abrir portal: "));
      Serial.println(configSsid);
    }
  }

  if (!reuseCurrentAp) {
    _configured = false;
    _stationMode = false;
    stopNetworking();
  }

  bool apOk = false;
  if (reuseCurrentAp) {
    apOk = true;
  } else {
#if defined(CONFIG_IDF_TARGET_ESP32P4)
    const uint8_t attempts = _safeP4Mode ? kSafeP4ConnectAttempts : 1;
    for (uint8_t attempt = 1; attempt <= attempts; ++attempt) {
      if (_safeP4Mode && attempts > 1) {
        Serial.print(F("[SuperOTA] Modo seguro P4: tentativa portal AP "));
        Serial.print(attempt);
        Serial.print('/');
        Serial.println(attempts);
      }

      if (attempt > 1) {
        stopNetworking();
        if (_safeP4Mode) {
          delay(kSafeP4RetryDelayMs);
        }
      }

      if (!prepareP4AccessPointStack(_safeP4Mode)) {
        if (_safeP4Mode && attempt < attempts) {
          continue;
        }
        return false;
      }

      if (_hostname.length()) {
        WiFi.softAPsetHostname(_hostname.c_str());
      }
      if (!WiFi.AP.config(apIp, apGateway, apSubnet, apDhcpLeaseStart, apDns)) {
        Serial.println(F("[SuperOTA] Aviso: falha ao aplicar AP.config no P4."));
      }

      apOk = WiFi.AP.create(configSsid.c_str(), configPass.c_str(), 1, 0, 4);
      if (apOk) {
        break;
      }
    }
#else
    WiFi.mode(WIFI_AP);
    if (_hostname.length()) {
      WiFi.softAPsetHostname(_hostname.c_str());
    }
    if (!WiFi.softAPConfig(apIp, apGateway, apSubnet, apDhcpLeaseStart, apDns)) {
      Serial.println(F("[SuperOTA] Aviso: falha ao aplicar softAPConfig personalizada."));
    }

    const bool hasPassword = configPass.length() > 0;
    apOk = hasPassword ? WiFi.softAP(configSsid.c_str(), configPass.c_str())
                       : WiFi.softAP(configSsid.c_str());
#endif
  }

  if (!apOk) {
    Serial.println(F("[SuperOTA] Falha ao abrir AP do portal."));
    return false;
  }
  if (_safeP4Mode) {
    delay(kSafeP4LinkStabilizeDelayMs);
  }

  uint32_t waitStartMs = millis();
#if defined(CONFIG_IDF_TARGET_ESP32P4)
  if (!reuseCurrentAp) {
    while (!hasValidIp(WiFi.AP.localIP()) && (millis() - waitStartMs) < 3000U) {
      delay(25);
    }
  }
  currentApIp = WiFi.AP.localIP();
#else
  while (!hasValidIp(WiFi.softAPIP()) && (millis() - waitStartMs) < 3000U) {
    delay(25);
  }
  currentApIp = WiFi.softAPIP();
#endif
  if (!hasValidIp(currentApIp)) {
    Serial.println(F("[SuperOTA] AP sem IP valido para iniciar portal."));
    return false;
  }

#if defined(ESP_IDF_VERSION) && defined(ESP_IDF_VERSION_VAL) && ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 4, 2)
  if (WiFi.AP.enableDhcpCaptivePortal()) {
    Serial.println(F("[SuperOTA] DHCP captive portal URI habilitado."));
  } else {
    Serial.println(F("[SuperOTA] Aviso: nao foi possivel habilitar DHCP captive portal URI."));
  }
#endif

  if (_configServer != nullptr) {
    delete _configServer;
    _configServer = nullptr;
  }

  _dnsServer.stop();
  _dnsServer.setErrorReplyCode(DNSReplyCode::NoError);
  _dnsRunning = _dnsServer.start(53, "*", currentApIp);
  if (!_dnsRunning) {
    Serial.println(F("[SuperOTA] Aviso: falha ao iniciar DNS captive."));
  }

  _configServer = new WebServer(kConfigPortalPort);
  const String apIpText = currentApIp.toString();
  String mdnsHost = _hostname;
  mdnsHost += F(".local");
  mdnsHost.toLowerCase();

  auto normalizeHost = [](String host) {
    host.trim();
    host.toLowerCase();
    const int colonPos = host.indexOf(':');
    if (colonPos >= 0) {
      host = host.substring(0, colonPos);
    }
    return host;
  };

  auto isIpv4Host = [](const String& host) {
    if (host.isEmpty()) {
      return false;
    }
    uint8_t dots = 0;
    for (size_t i = 0; i < host.length(); ++i) {
      const char c = host[i];
      if (c == '.') {
        ++dots;
        continue;
      }
      if (c < '0' || c > '9') {
        return false;
      }
    }
    return dots == 3;
  };

  const String normalizedApIp = normalizeHost(apIpText);
  auto isPortalHost = [normalizeHost, isIpv4Host, normalizedApIp, mdnsHost](const String& rawHost) {
    const String host = normalizeHost(rawHost);
    if (host.isEmpty()) {
      return true;
    }
    if (host == F("localhost")) {
      return true;
    }
    if (host == normalizedApIp || host == mdnsHost) {
      return true;
    }
    return isIpv4Host(host);
  };

  auto sendCaptiveRedirect = [this, apIpText]() {
    if (_configServer == nullptr) {
      return;
    }
    const String location = String("http://") + apIpText + "/";
    _configServer->sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
    _configServer->sendHeader("Pragma", "no-cache");
    _configServer->sendHeader("Expires", "-1");
    _configServer->sendHeader("Location", location, true);
    _configServer->send(302, "text/plain", "redirect");
  };

  auto renderConfigPage = [this, isPortalHost, sendCaptiveRedirect]() {
    if (_configServer == nullptr) {
      return;
    }
    if (!isPortalHost(_configServer->hostHeader())) {
      sendCaptiveRedirect();
      return;
    }
    if (!ensurePortalAuthentication()) {
      return;
    }
    if (_configServer->method() == HTTP_HEAD) {
      _configServer->send(200, "text/html", "");
      return;
    }
    handleConfigPage();
  };
  auto captiveProbe = [this, sendCaptiveRedirect]() {
    if (_configServer == nullptr) {
      return;
    }
    if (_debugMetricsEnabled) {
      Serial.print(F("[SuperOTA] Captive probe: "));
      Serial.print(_configServer->hostHeader());
      Serial.print(F(" "));
      Serial.println(_configServer->uri());
    }
    _configServer->sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
    _configServer->sendHeader("Pragma", "no-cache");
    _configServer->sendHeader("Expires", "-1");
    sendCaptiveRedirect();
  };
  _configServer->on("/", HTTP_ANY, renderConfigPage);
  _configServer->on("/save", HTTP_POST, [this]() {
    if (!ensurePortalAuthentication()) {
      return;
    }
    handleConfigSave();
  });
  _configServer->on("/scan", HTTP_GET, [this]() {
    if (!ensurePortalAuthentication()) {
      return;
    }
    handleConfigScan();
  });
  _configServer->on("/generate_204", HTTP_ANY, captiveProbe);
  _configServer->on("/gen_204", HTTP_ANY, captiveProbe);
  _configServer->on("/hotspot-detect.html", HTTP_ANY, captiveProbe);
  _configServer->on("/connecttest.txt", HTTP_ANY, captiveProbe);
  _configServer->on("/ncsi.txt", HTTP_ANY, captiveProbe);
  _configServer->on("/fwlink", HTTP_ANY, captiveProbe);
  _configServer->on("/redirect", HTTP_ANY, captiveProbe);
  _configServer->on("/canonical.html", HTTP_ANY, captiveProbe);
  _configServer->on("/success.txt", HTTP_ANY, captiveProbe);
  _configServer->on("/success.html", HTTP_ANY, captiveProbe);
  _configServer->on("/library/test/success.html", HTTP_ANY, captiveProbe);
  _configServer->onNotFound(captiveProbe);
  _configServer->begin();
  _configPortalRunning = true;
  _configPortalUsesAp = true;
  _awaitingPortalModeChoice = false;
  startMDNS();
  if (_mdnsRunning) {
    MDNS.addService("http", "tcp", kConfigPortalPort);
  }
  startTelnetServer(_telnetPort);

  printConfigPortalEndpoints();
  return true;
}

void SuperOTA::runConfigPortalForegroundLoop() {
  if (!_configPortalRunning || _configServer == nullptr) {
    return;
  }

  println(F("[SuperOTA] Portal em foreground para melhor detecao captive."));

  while (_configPortalRunning) {
    processSerialCommands();
    processTelnet();
    updateDebugSummary();

    if (_dnsRunning) {
      _dnsServer.processNextRequest();
    }
    if (_configServer != nullptr) {
      _configServer->handleClient();
    }
    handleDeferredPortalStop();
    delay(10);
  }
}

void SuperOTA::handleDeferredPortalStop() {
  if (!_deferredPortalStop) {
    return;
  }

  if (_deferredPortalStopAfterMs != 0U) {
    const int32_t remaining = static_cast<int32_t>(_deferredPortalStopAfterMs - millis());
    if (remaining > 0) {
      return;
    }
  }

  const bool resumeAuto = _deferredPortalResumeAuto;
  const bool restartAfterSave = _deferredPortalRestart;
  _deferredPortalStop = false;
  _deferredPortalResumeAuto = true;
  _deferredPortalStopAfterMs = 0U;
  _deferredPortalRestart = false;

  if (restartAfterSave) {
    println(F("[SuperOTA] Aplicando configuracoes com reinicio seguro."));
    delay(180);
    ESP.restart();
    return;
  }

  println(F("[SuperOTA] Aplicando configuracoes reconfigurando sem reboot."));
  stopConfigPortal(resumeAuto);
}

void SuperOTA::printConfigPortalEndpoints() const {
  Serial.println(F("[SuperOTA] Portal de configuracao ativo."));
  if (_configPortalUsesAp) {
    Serial.print(F("[SuperOTA] AP SSID: "));
    Serial.println(WiFi.softAPSSID());
    Serial.print(F("[SuperOTA] AP IP: http://"));
    Serial.println(WiFi.softAPIP());
    Serial.print(F("[SuperOTA] mDNS em AP (se suportado no cliente): http://"));
    Serial.print(_hostname);
    Serial.println(F(".local"));
  } else {
    Serial.print(F("[SuperOTA] Station IP: http://"));
    Serial.println(WiFi.localIP());
    Serial.print(F("[SuperOTA] mDNS: http://"));
    Serial.print(_hostname);
    Serial.println(F(".local"));
  }
  Serial.print(F("[SuperOTA] Porta HTTP do portal: "));
  Serial.println(kConfigPortalPort);
  if (portalPasswordEnabled()) {
    Serial.println(F("[SuperOTA] Portal protegido por senha (usuario: admin)."));
  } else {
    Serial.println(F("[SuperOTA] Portal sem senha."));
  }
  if (otaPasswordEnabled()) {
    Serial.println(F("[SuperOTA] OTA protegido por senha."));
  } else {
    Serial.println(F("[SuperOTA] OTA sem senha."));
  }
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
  _deferredPortalStop = false;
  _deferredPortalResumeAuto = true;
  _deferredPortalStopAfterMs = 0U;
  _deferredPortalRestart = false;

  if (_dnsRunning) {
    _dnsServer.stop();
    _dnsRunning = false;
  }
  if (_mdnsRunning) {
    MDNS.end();
    _mdnsRunning = false;
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
    updateDebugSummary();
    if (_dnsRunning) {
      _dnsServer.processNextRequest();
    }
    _configServer->handleClient();
    handleDeferredPortalStop();
    return;
  }

  if (_enabled && _configured) {
    ArduinoOTA.handle();
  }
  updateDebugSummary();
  handleDeferredPortalStop();
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
  _otaPassword = _prefs.getString(kPrefOtaPass, _otaPassword);
  _portalPassword = _prefs.getString(kPrefPortalPass, _portalPassword);
  _useOtaPasswordForPortal = _prefs.getBool(kPrefPortalUseOta, _useOtaPasswordForPortal);

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
  _prefs.putString(kPrefOtaPass, _otaPassword);
  _prefs.putString(kPrefPortalPass, _portalPassword);
  _prefs.putBool(kPrefPortalUseOta, _useOtaPasswordForPortal);

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
    _otaPassword = "";
    _portalPassword = "";
    _useOtaPasswordForPortal = true;
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
#if defined(CONFIG_IDF_TARGET_ESP32P4)
  if (!prepareP4AccessPointStack(_safeP4Mode)) {
    return false;
  }

  if (_hostname.length()) {
    WiFi.softAPsetHostname(_hostname.c_str());
  }

  const IPAddress apIp(192, 168, 4, 1);
  const IPAddress apGateway(192, 168, 4, 1);
  const IPAddress apSubnet(255, 255, 255, 0);
  const IPAddress apDhcpLeaseStart(192, 168, 4, 2);
  const IPAddress apDns(192, 168, 4, 1);
  if (!WiFi.AP.config(apIp, apGateway, apSubnet, apDhcpLeaseStart, apDns)) {
    Serial.println(F("[SuperOTA] Aviso: falha ao aplicar AP.config no P4."));
  }

  const bool apOk = WiFi.AP.create(ssid, password, 1, 0, 4);
  if (!apOk) {
    return false;
  }

  if (_safeP4Mode) {
    delay(kSafeP4LinkStabilizeDelayMs);
  }

  uint32_t waitStartMs = millis();
  while (!hasValidIp(WiFi.AP.localIP()) && (millis() - waitStartMs) < 3000U) {
    delay(25);
  }
  return hasValidIp(WiFi.AP.localIP());
#else
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
#endif
}

void SuperOTA::configureOTAHandlers() {
  ArduinoOTA.setPort(kArduinoOtaPort);
  ArduinoOTA.setHostname(_hostname.c_str());
  if (_otaPassword.length() > 0) {
    ArduinoOTA.setPassword(_otaPassword.c_str());
  }

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
  MDNS.end();
  _mdnsRunning = false;

  if (MDNS.begin(_hostname.c_str())) {
    _mdnsRunning = true;
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
    WiFiClient incoming = _telnetServer.accept();
    if (incoming && incoming.connected()) {
      _telnetClient = incoming;
      _telnetClient.println(F("[SuperOTA] Telnet conectado."));
      _telnetClient.println(F("[SuperOTA] Comandos: configota, config-stop, config-help, debug-on, debug-off, debug-summary."));
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

    if (shouldIgnoreLeadingCommandByte(_serialBuffer, c)) {
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
      if (startConfigPortalOnStation()) {
        runConfigPortalForegroundLoop();
      }
    } else if (command == "2") {
      println(F("[SuperOTA] Escolha 2: abrir portal em AP."));
      if (startConfigPortalOnAccessPoint(nullptr, nullptr)) {
        runConfigPortalForegroundLoop();
      }
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
      if (startConfigPortalOnAccessPoint(nullptr, nullptr)) {
        runConfigPortalForegroundLoop();
      }
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
    println(F(", config-stop, config-help, 1, 2, debug-on, debug-off, debug-summary"));
    return;
  }

  if (command == "debug-on") {
    enableDebugMetrics(true);
    return;
  }

  if (command == "debug-off") {
    enableDebugMetrics(false);
    return;
  }

  if (command == "debug-summary") {
    printDebugSummary();
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
  const bool otaSecured = otaPasswordEnabled();
  const bool portalSecured = portalPasswordEnabled();
  const bool portalUsesOtaPassword = usingOtaPasswordForPortal();
  const String configUiStamp =
      String("SuperOTA v") + kSuperOtaVersion + " | " + kConfigUiRevision + " | build " + __DATE__ + " " + __TIME__;
  String accessHint;
  if (_configPortalUsesAp) {
    accessHint = String("AP: http://") + WiFi.softAPIP().toString() + " (mDNS opcional: http://" + _hostname + ".local)";
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
        "border:1px solid #32456c;border-radius:8px;background:#0f1a2e;color:#eaf0ff;box-sizing:border-box;}"
        "input:focus,textarea:focus{outline:none;border-color:#4a72ba;box-shadow:0 0 0 2px rgba(74,114,186,.22);}"
        "textarea{min-height:140px;}button{margin-top:0;padding:11px 14px;border:0;border-radius:8px;"
        "background:#315ea8;color:#fff;font-weight:700;cursor:pointer;}button:hover{background:#3d74cc;}"
        "small{color:#9eb5e8;}a{color:#7db7ff;}code{background:#0f1a2e;border:1px solid #32456c;padding:1px 6px;"
        "border-radius:6px;}.hint{padding:10px 12px;border-radius:8px;background:#0f1a2e;border:1px solid #32456c;"
        "margin:10px 0 6px 0;}.section{padding:12px;border:1px solid #2b3e63;border-radius:10px;margin:14px 0;}"
        ".section h2{margin:0 0 8px 0;font-size:18px;color:#dce8ff;}.check-row{display:flex;align-items:center;justify-content:space-between;"
        "gap:12px;margin:14px 0 6px 0;}.check-row .text{font-weight:700;color:#eaf0ff;}"
        ".check-row input[type=checkbox]{appearance:none;width:20px;height:20px;flex:0 0 auto;margin:0;"
        "border:1px solid #4f6ea8;border-radius:6px;background:#0f1a2e;cursor:pointer;position:relative;}"
        ".check-row input[type=checkbox]:checked{background:#315ea8;border-color:#5f84c8;}"
        ".check-row input[type=checkbox]:checked::after{content:'';position:absolute;left:6px;top:2px;"
        "width:5px;height:10px;border:solid #fff;border-width:0 2px 2px 0;transform:rotate(45deg);}"
        ".input-wrap{position:relative;display:block;}.input-wrap input{padding-right:40px;}"
        ".peek-btn{position:absolute;right:10px;top:50%;transform:translateY(-50%);width:20px;height:20px;padding:0;"
        "margin:0;border:0;background:transparent;display:inline-flex;align-items:center;justify-content:center;"
        "color:#b8c9ee;opacity:.82;cursor:pointer;}"
        ".peek-btn:hover{color:#dce8ff;opacity:1;}.peek-btn:focus-visible{outline:none;border-radius:999px;"
        "box-shadow:0 0 0 2px rgba(93,129,196,.38);}"
        ".peek-btn svg{position:absolute;width:16px;height:16px;fill:none;stroke:currentColor;stroke-width:1.75;"
        "stroke-linecap:round;stroke-linejoin:round;transition:opacity .16s ease,transform .16s ease;}"
        ".peek-btn .icon-open{opacity:0;transform:scale(.96);}"
        ".peek-btn .icon-off{opacity:1;transform:scale(1);}"
        ".peek-btn.is-active .icon-open{opacity:1;transform:scale(1);}"
        ".peek-btn.is-active .icon-off{opacity:0;transform:scale(.96);}"
        ".subhint{display:block;margin-top:4px;color:#9eb5e8;line-height:1.35;}"
        ".collapse{overflow:hidden;max-height:240px;opacity:1;transform:translateY(0);"
        "transition:max-height .26s ease,opacity .22s ease,transform .22s ease;}"
        ".collapse.hidden{max-height:0;opacity:0;transform:translateY(-6px);pointer-events:none;margin-top:0;}"
        ".status-ok{color:#74e6aa;}.status-warn{color:#ffd68a;}"
        ".form-footer{display:flex;align-items:center;justify-content:space-between;gap:14px;margin-top:14px;}"
        ".form-footer small{margin:0;}.build-marker{margin:12px 0 2px 0;text-align:right;font-size:12px;"
        "color:#86a0d4;letter-spacing:.01em;opacity:.92;}</style></head><body><main>");

  html += F("<h1>SuperOTA - Configuracao</h1><p>Edite os campos e clique em salvar. "
            "Formato da lista station: uma rede por linha em <code>SSID;senha</code>.</p>");
  html += F("<p class='hint'><strong>Acesso atual:</strong> ");
  html += htmlEscape(accessHint);
  html += F(" - porta 80</p>");
  html += F("<p class='hint'><strong>Seguranca:</strong> OTA ");
  html += otaSecured ? F("<span class='status-ok'>com senha</span>") : F("<span class='status-warn'>sem senha</span>");
  html += F(" | Portal ");
  html += portalSecured ? F("<span class='status-ok'>com senha</span>") : F("<span class='status-warn'>sem senha</span>");
  html += F(" (usuario: <code>admin</code>)</p>");
  html += F("<p><a href='/scan' target='_blank'>Ver redes detectadas agora</a></p>");

  html += F("<form method='post' action='/save' id='configForm'>");
  html += F("<label>Hostname</label><input name='hostname' value='");
  html += htmlEscape(_hostname);
  html += F("'/>\n");

  html += F("<label class='check-row'><span class='text'>Iniciar priorizando Access Point</span><input type='checkbox' name='preferAP' ");
  if (_preferAccessPoint) {
    html += F("checked");
  }
  html += F("/></label>");

  html += F("<label>AP SSID</label><input name='apSsid' value='");
  html += htmlEscape(_apSsid);
  html += F("'/>\n");

  html += F("<label>AP Senha (vazio = AP aberto)</label><div class='input-wrap'>"
            "<input id='apPasswordInput' type='password' name='apPassword' value='");
  html += htmlEscape(_apPassword);
  html += F("'/><button id='apPasswordPeek' class='peek-btn' type='button' aria-label='Mostrar senha' title='Pressione para revelar'>"
            "<svg class='icon-open' viewBox='0 0 24 24' aria-hidden='true'><path d='M2 12s3.6-6 10-6 10 6 10 6-3.6 6-10 6-10-6-10-6Z'/><circle cx='12' cy='12' r='2.6'/></svg>"
            "<svg class='icon-off' viewBox='0 0 24 24' aria-hidden='true'><path d='M2 12s3.6-6 10-6 10 6 10 6-3.6 6-10 6-10-6-10-6Z'/><circle cx='12' cy='12' r='2.6'/><path d='M4 4l16 16'/></svg>"
            "</button></div>\n");

  html += F("<div class='section'><h2>Seguranca</h2>");
  html += F("<label class='check-row'><span class='text'>Exigir senha para OTA</span>"
            "<input id='useOtaPasswordCheckbox' type='checkbox' name='useOtaPassword' ");
  if (otaSecured) {
    html += F("checked");
  }
  html += F("/></label>");
  html += F("<div id='otaPasswordContainer' class='collapse");
  if (!otaSecured) {
    html += F(" hidden");
  }
  html += F("'>");
  html += F("<label>Senha OTA (upload de firmware)</label><div class='input-wrap'>"
            "<input id='otaPasswordInput' type='password' name='otaPassword' placeholder='deixe vazio para manter a atual' autocomplete='new-password'/>"
            "<button id='otaPasswordPeek' class='peek-btn' type='button' aria-label='Mostrar senha OTA' title='Pressione para revelar'>"
            "<svg class='icon-open' viewBox='0 0 24 24' aria-hidden='true'><path d='M2 12s3.6-6 10-6 10 6 10 6-3.6 6-10 6-10-6-10-6Z'/><circle cx='12' cy='12' r='2.6'/></svg>"
            "<svg class='icon-off' viewBox='0 0 24 24' aria-hidden='true'><path d='M2 12s3.6-6 10-6 10 6 10 6-3.6 6-10 6-10-6-10-6Z'/><circle cx='12' cy='12' r='2.6'/><path d='M4 4l16 16'/></svg>"
            "</button></div>");
  html += F("<small class='subhint'>Porta OTA: 3232.</small>");
  html += F("</div>");
  html += F("<small id='otaDisabledNote' class='subhint'");
  if (otaSecured) {
    html += F(" style='display:none;'");
  }
  html += F(">Com essa opcao desligada, o upload OTA nao exige senha.</small>");

  html += F("<label class='check-row'><span class='text'>Proteger portal com senha</span>"
            "<input id='protectPortalCheckbox' type='checkbox' name='protectPortal' ");
  if (portalSecured) {
    html += F("checked");
  }
  html += F("/></label>");
  html += F("<div id='portalSecurityContainer' class='collapse");
  if (!portalSecured) {
    html += F(" hidden");
  }
  html += F("'>");

  html += F("<label class='check-row'><span class='text'>Usar senha OTA para proteger portal</span>"
            "<input id='portalUseOtaCheckbox' type='checkbox' name='portalUseOta' ");
  if (portalSecured && portalUsesOtaPassword) {
    html += F("checked");
  }
  html += F("/></label>");
  html += F("<small id='portalMirrorNote' class='subhint'");
  if (!(portalSecured && portalUsesOtaPassword)) {
    html += F(" style='display:none;'");
  }
  html += F(">Com essa opcao ativa, o portal usa a mesma senha OTA.</small>");

  html += F("<div id='portalPasswordContainer' class='collapse");
  if (!portalSecured || portalUsesOtaPassword) {
    html += F(" hidden");
  }
  html += F("'>");
  html += F("<label>Senha do portal (quando opcao acima estiver desmarcada)</label><div class='input-wrap'>"
            "<input id='portalPasswordInput' type='password' name='portalPassword' placeholder='deixe vazio para manter a atual' autocomplete='new-password'/>"
            "<button id='portalPasswordPeek' class='peek-btn' type='button' aria-label='Mostrar senha do portal' title='Pressione para revelar'>"
            "<svg class='icon-open' viewBox='0 0 24 24' aria-hidden='true'><path d='M2 12s3.6-6 10-6 10 6 10 6-3.6 6-10 6-10-6-10-6Z'/><circle cx='12' cy='12' r='2.6'/></svg>"
            "<svg class='icon-off' viewBox='0 0 24 24' aria-hidden='true'><path d='M2 12s3.6-6 10-6 10 6 10 6-3.6 6-10 6-10-6-10-6Z'/><circle cx='12' cy='12' r='2.6'/><path d='M4 4l16 16'/></svg>"
            "</button></div>");
  html += F("</div>");
  html += F("</div>");
  html += F("<small id='portalDisabledNote' class='subhint'");
  if (portalSecured) {
    html += F(" style='display:none;'");
  }
  html += F(">Com essa opcao desligada, o portal abre sem login.</small>");
  html += F("<small class='subhint'>Se ambos os campos de senha ficarem vazios, as senhas atuais sao mantidas.</small>");
  html += F("</div>");

  html += F("<label>Lista Station</label><textarea name='stationList'>");
  html += htmlEscape(stationText);
  html += F("</textarea>");
  html += F("<div class='form-footer'><small>Exemplo: MinhaCasa;senha123</small><button type='submit'>Salvar e aplicar</button></div></form>");
  html += F("<p class='build-marker'>");
  html += htmlEscape(configUiStamp);
  html += F("</p>");
  html += F("<script>(function(){"
            "function bindHoldReveal(btnId,inputId){"
            "var btn=document.getElementById(btnId);var input=document.getElementById(inputId);"
            "if(!btn||!input)return;"
            "var show=function(){input.type='text';btn.classList.add('is-active');};"
            "var hide=function(){input.type='password';btn.classList.remove('is-active');};"
            "btn.addEventListener('mousedown',function(e){e.preventDefault();show();});"
            "btn.addEventListener('mouseup',hide);btn.addEventListener('mouseleave',hide);"
            "btn.addEventListener('touchstart',function(e){e.preventDefault();show();},{passive:false});"
            "btn.addEventListener('touchend',hide);btn.addEventListener('touchcancel',hide);"
            "btn.addEventListener('keydown',function(e){if(e.key===' '||e.key==='Enter'){e.preventDefault();show();}});"
            "btn.addEventListener('keyup',hide);btn.addEventListener('blur',hide);"
            "document.addEventListener('mouseup',hide);document.addEventListener('touchend',hide);"
            "}"
            "function setHidden(el,hide){if(!el)return;if(hide)el.classList.add('hidden');else el.classList.remove('hidden');}"
            "function syncSecurityPanels(){"
            "var useOta=document.getElementById('useOtaPasswordCheckbox');"
            "var otaPanel=document.getElementById('otaPasswordContainer');"
            "var otaNote=document.getElementById('otaDisabledNote');"
            "var protectPortal=document.getElementById('protectPortalCheckbox');"
            "var portalSec=document.getElementById('portalSecurityContainer');"
            "var portalDisabled=document.getElementById('portalDisabledNote');"
            "var portalUseOta=document.getElementById('portalUseOtaCheckbox');"
            "var portalPanel=document.getElementById('portalPasswordContainer');"
            "var portalMirror=document.getElementById('portalMirrorNote');"
            "if(useOta&&otaPanel&&otaNote){setHidden(otaPanel,!useOta.checked);otaNote.style.display=useOta.checked?'none':'block';}"
            "if(!protectPortal||!portalSec||!portalDisabled){return;}"
            "setHidden(portalSec,!protectPortal.checked);portalDisabled.style.display=protectPortal.checked?'none':'block';"
            "if(!protectPortal.checked){if(portalMirror)portalMirror.style.display='none';setHidden(portalPanel,true);return;}"
            "if(portalUseOta&&portalUseOta.checked&&useOta&&!useOta.checked){useOta.checked=true;setHidden(otaPanel,false);if(otaNote)otaNote.style.display='none';}"
            "if(portalUseOta&&portalUseOta.checked){setHidden(portalPanel,true);if(portalMirror)portalMirror.style.display='block';}"
            "else{setHidden(portalPanel,false);if(portalMirror)portalMirror.style.display='none';}"
            "}"
            "bindHoldReveal('apPasswordPeek','apPasswordInput');"
            "bindHoldReveal('otaPasswordPeek','otaPasswordInput');"
            "bindHoldReveal('portalPasswordPeek','portalPasswordInput');"
            "var portalUseOtaCb=document.getElementById('portalUseOtaCheckbox');"
            "var protectPortalCb=document.getElementById('protectPortalCheckbox');"
            "var useOtaCb=document.getElementById('useOtaPasswordCheckbox');"
            "if(portalUseOtaCb){portalUseOtaCb.addEventListener('change',syncSecurityPanels);}"
            "if(protectPortalCb){protectPortalCb.addEventListener('change',syncSecurityPanels);}"
            "if(useOtaCb){useOtaCb.addEventListener('change',syncSecurityPanels);}"
            "syncSecurityPanels();"
            "})();</script>");
  html += F("</main></body></html>");

  _configServer->sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
  _configServer->sendHeader("Pragma", "no-cache");
  _configServer->sendHeader("Expires", "-1");
  _configServer->send(200, "text/html", html);
}

void SuperOTA::handleConfigSave() {
  if (_configServer == nullptr) {
    return;
  }

  const String previousHostname = _hostname;
  const bool previousPreferAccessPoint = _preferAccessPoint;
  const String previousApSsid = _apSsid;
  const String previousApPassword = _apPassword;
  String previousStationList;
  stationListToMultiline(previousStationList);

  const String hostname = _configServer->arg("hostname");
  const String apSsid = _configServer->arg("apSsid");
  const String apPassword = _configServer->arg("apPassword");
  const bool useOtaPassword = _configServer->hasArg("useOtaPassword");
  const bool protectPortal = _configServer->hasArg("protectPortal");
  const String otaPassword = _configServer->arg("otaPassword");
  const String portalPassword = _configServer->arg("portalPassword");
  const bool portalUseOtaRequested = _configServer->hasArg("portalUseOta");
  const String stationList = _configServer->arg("stationList");
  const String normalizedStationList = normalizeStationListInput(stationList);
  bool securityChanged = false;

  setHostname(hostname.c_str());
  setPreferAccessPoint(_configServer->hasArg("preferAP"));
  setAccessPointCredentials(apSsid.c_str(), apPassword.c_str());

  if (useOtaPassword) {
    if (otaPassword.length() > 0 && otaPassword != _otaPassword) {
      setOtaPassword(otaPassword.c_str());
      securityChanged = true;
    }
  } else if (_otaPassword.length() > 0) {
    _otaPassword = "";
    securityChanged = true;
    println(F("[SuperOTA] Senha OTA desativada."));
  }

  if (!protectPortal) {
    if (_portalPassword.length() > 0 || _useOtaPasswordForPortal) {
      securityChanged = true;
    }
    _portalPassword = "";
    _useOtaPasswordForPortal = false;
    println(F("[SuperOTA] Protecao do portal desativada."));
  } else {
    const bool effectivePortalUseOta = portalUseOtaRequested && useOtaPassword;
    if (_useOtaPasswordForPortal != effectivePortalUseOta) {
      _useOtaPasswordForPortal = effectivePortalUseOta;
      securityChanged = true;
    }
    if (!effectivePortalUseOta && portalPassword.length() > 0 && portalPassword != _portalPassword) {
      setPortalPassword(portalPassword.c_str());
      securityChanged = true;
    }
  }

  parseAndSetStationList(normalizedStationList);

  const bool topologyChanged =
      previousHostname != _hostname || previousPreferAccessPoint != _preferAccessPoint || previousApSsid != _apSsid ||
      previousApPassword != _apPassword || previousStationList != normalizedStationList;

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
  response += F("<p>Seguranca OTA: ");
  response += otaPasswordEnabled() ? F("com senha") : F("sem senha");
  response += F("<br/>Seguranca portal: ");
  response += portalPasswordEnabled() ? F("com senha") : F("sem senha");
  response += F("</p>");
  response += F("<p>Voce pode fechar esta pagina.</p></div></body></html>");

  _configServer->send(200, "text/html", response);
  _deferredPortalResumeAuto = true;
  _deferredPortalStopAfterMs = millis() + kConfigPortalDeferredStopMs;
  _deferredPortalRestart = topologyChanged && (_safeP4Mode && kBuildTargetIsP4);
  _deferredPortalStop = true;

  if (_deferredPortalRestart) {
    println(F("[SuperOTA] Mudancas de topologia detectadas. Portal sera aplicado com reinicio seguro."));
  } else if (securityChanged) {
    println(F("[SuperOTA] Mudancas de seguranca detectadas. Portal sera reaplicado sem reboot."));
  } else {
    println(F("[SuperOTA] Mudancas aplicadas sem necessidade de reboot."));
  }
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

String SuperOTA::normalizeStationListInput(const String& rawList) const {
  String tempSsids[kMaxStationNetworks];
  String tempPasswords[kMaxStationNetworks];
  uint8_t tempCount = 0;

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
      }

      ssid.trim();
      pass.trim();

      if (!ssid.isEmpty()) {
        bool updated = false;
        for (uint8_t i = 0; i < tempCount; ++i) {
          if (tempSsids[i] == ssid) {
            tempPasswords[i] = pass;
            updated = true;
            break;
          }
        }

        if (!updated && tempCount < kMaxStationNetworks) {
          tempSsids[tempCount] = ssid;
          tempPasswords[tempCount] = pass;
          ++tempCount;
        }
      }
    }

    start = end + 1;
  }

  String normalized;
  for (uint8_t i = 0; i < tempCount; ++i) {
    normalized += tempSsids[i];
    normalized += ';';
    normalized += tempPasswords[i];
    normalized += '\n';
  }

  return normalized;
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
