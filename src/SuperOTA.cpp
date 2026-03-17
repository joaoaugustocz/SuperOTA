#include "SuperOTA.h"

#include <ArduinoOTA.h>
#include <cstring>

namespace {
constexpr uint16_t kArduinoOtaPort = 3232;
constexpr char kPrefPreferAp[] = "preferAP";
constexpr char kPrefStaSsid[] = "staSsid";
constexpr char kPrefStaPass[] = "staPass";
constexpr char kPrefApSsid[] = "apSsid";
constexpr char kPrefApPass[] = "apPass";
constexpr char kPrefHostname[] = "hostname";
constexpr uint8_t kSafeP4ConnectAttempts = 2;
constexpr uint16_t kSafeP4RetryDelayMs = 250;
constexpr uint16_t kSafeP4LinkStabilizeDelayMs = 120;

#if defined(CONFIG_IDF_TARGET_ESP32P4)
constexpr bool kBuildTargetIsP4 = true;
#else
constexpr bool kBuildTargetIsP4 = false;
#endif
}

SuperOTA::SuperOTA()
    : _enabled(true),
      _configured(false),
      _stationMode(false),
      _mdnsRunning(false),
      _stationTimeoutMs(kDefaultStationTimeoutMs),
      _staSsid(),
      _staPassword(),
      _apSsid(kDefaultApSsid),
      _apPassword(kDefaultApPassword),
      _hostname(kDefaultHostname),
      _preferAccessPoint(false),
      _safeP4Mode(kBuildTargetIsP4),
      _prefs() {}

void SuperOTA::beginSerial(uint32_t baud) {
  Serial.begin(baud);
}

void SuperOTA::setStationCredentials(const char* ssid, const char* password) {
  _staSsid = (ssid != nullptr) ? ssid : "";
  _staPassword = (password != nullptr) ? password : "";
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

  _configured = true;

  Serial.print(F("[SuperOTA] OTA ativo em AP. SSID: "));
  Serial.println(apSsid);
  Serial.print(F("[SuperOTA] IP do AP: "));
  Serial.println(WiFi.softAPIP());
  return true;
}

void SuperOTA::loop() {
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
  return !_staSsid.isEmpty();
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
  _staSsid = _prefs.getString(kPrefStaSsid, _staSsid);
  _staPassword = _prefs.getString(kPrefStaPass, _staPassword);
  _apSsid = _prefs.getString(kPrefApSsid, _apSsid);
  _apPassword = _prefs.getString(kPrefApPass, _apPassword);
  _hostname = normalizeHostname(_prefs.getString(kPrefHostname, _hostname).c_str());

  _prefs.end();

  if (_apSsid.isEmpty()) {
    _apSsid = kDefaultApSsid;
  }

  Serial.println(F("[SuperOTA] Preferencias carregadas."));
  return true;
}

bool SuperOTA::savePreferences() {
  if (!_prefs.begin(kPrefsNamespace, false)) {
    Serial.println(F("[SuperOTA] NVS indisponivel para escrita."));
    return false;
  }

  _prefs.putBool(kPrefPreferAp, _preferAccessPoint);
  _prefs.putString(kPrefStaSsid, _staSsid);
  _prefs.putString(kPrefStaPass, _staPassword);
  _prefs.putString(kPrefApSsid, _apSsid);
  _prefs.putString(kPrefApPass, _apPassword);
  _prefs.putString(kPrefHostname, _hostname);

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
  const bool hasStation = !_staSsid.isEmpty();

  if (_preferAccessPoint) {
    success = beginAccessPoint(_apSsid.c_str(), _apPassword.c_str());
    if (!success && hasStation) {
      Serial.println(F("[SuperOTA] AP falhou, tentando station."));
      success = beginStation(_staSsid.c_str(), _staPassword.c_str());
    }
  } else {
    if (hasStation) {
      success = beginStation(_staSsid.c_str(), _staPassword.c_str());
    }
    if (!success) {
      Serial.println(F("[SuperOTA] Station indisponivel, iniciando AP."));
      success = beginAccessPoint(_apSsid.c_str(), _apPassword.c_str());
    }
  }

  return success;
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

  Serial.print(F("[SuperOTA] Conectando em station"));
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

  ArduinoOTA.onStart([]() {
    Serial.println(F("[SuperOTA] OTA start."));
  });

  ArduinoOTA.onEnd([]() {
    Serial.println(F("[SuperOTA] OTA end."));
  });

  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    const unsigned int percent = (total == 0U) ? 0U : (progress * 100U) / total;
    Serial.print(F("[SuperOTA] OTA progresso: "));
    Serial.print(percent);
    Serial.println(F("%"));
  });

  ArduinoOTA.onError([](ota_error_t error) {
    Serial.print(F("[SuperOTA] Erro OTA: "));
    Serial.println(static_cast<int>(error));
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
    Serial.print(F("[SuperOTA] mDNS ativo em "));
    Serial.print(_hostname);
    Serial.println(F(".local"));
  } else {
    Serial.println(F("[SuperOTA] Falha ao iniciar mDNS."));
    if (_safeP4Mode) {
      Serial.println(F("[SuperOTA] Modo seguro P4: seguindo sem mDNS."));
    }
  }
}

void SuperOTA::stopNetworking() {
  if (_mdnsRunning) {
    MDNS.end();
    _mdnsRunning = false;
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
