#pragma once

#include <Arduino.h>
#include <DNSServer.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include <WebServer.h>
#include <WiFi.h>

class SuperOTA final {
public:
  static constexpr uint32_t kDefaultStationTimeoutMs = 20000;
  static constexpr uint8_t kMaxStationNetworks = 8;

  SuperOTA();

  void beginSerial(uint32_t baud = 115200);

  // Backward compatible helper: clears list and defines only one station network.
  void setStationCredentials(const char* ssid, const char* password = nullptr);
  bool addStationNetwork(const char* ssid, const char* password = nullptr);
  void clearStationNetworks();
  uint8_t stationNetworkCount() const;

  void setAccessPointCredentials(const char* ssid, const char* password = nullptr);
  void setHostname(const char* hostname);
  void setPreferAccessPoint(bool prefer);
  void setStationConnectTimeoutMs(uint32_t timeoutMs);
  void setSafeP4Mode(bool enable);
  bool safeP4Mode() const;
  bool isP4Target() const;

  void enableTelnetSerial(bool enable, uint16_t port = 23);
  bool telnetSerialEnabled() const;
  uint16_t telnetPort() const;
  bool telnetClientConnected() const;

  void enableSerialConfigCommand(bool enable = true, const char* command = "configota");
  bool startConfigPortal(const char* apSsid = nullptr, const char* apPassword = nullptr);
  void stopConfigPortal(bool resumeAuto = true);
  bool configPortalRunning() const;

  bool begin();
  bool beginStation(const char* ssid, const char* password = nullptr);
  bool beginAccessPoint(const char* ssid = nullptr, const char* password = nullptr);

  void loop();

  void enable(bool enable);
  bool enabled() const;

  bool isConfigured() const;
  bool isStationMode() const;
  bool hasStationCredentials() const;
  IPAddress ip() const;

  bool loadPreferences();
  bool savePreferences();
  bool clearPreferences();

  template <typename T>
  void print(const T& value) {
    Serial.print(value);
    if (_telnetEnabled && _telnetClient && _telnetClient.connected()) {
      _telnetClient.print(value);
    }
  }

  template <typename T>
  void print(const T& value, int format) {
    Serial.print(value, format);
    if (_telnetEnabled && _telnetClient && _telnetClient.connected()) {
      _telnetClient.print(value, format);
    }
  }

  template <typename T>
  void println(const T& value) {
    Serial.println(value);
    if (_telnetEnabled && _telnetClient && _telnetClient.connected()) {
      _telnetClient.println(value);
    }
  }

  template <typename T>
  void println(const T& value, int format) {
    Serial.println(value, format);
    if (_telnetEnabled && _telnetClient && _telnetClient.connected()) {
      _telnetClient.println(value, format);
    }
  }

  void println();
  void printf(const char* format, ...);

private:
  static constexpr const char* kDefaultApSsid = "Baratinha-OTA";
  static constexpr const char* kDefaultApPassword = "12345678";
  static constexpr const char* kDefaultHostname = "baratinha";
  static constexpr const char* kDefaultConfigApSsid = "SuperOTA-Setup";
  static constexpr const char* kPrefsNamespace = "superota";

  bool configureAuto();
  bool beginStationFromKnownNetworks();
  bool beginStationOnce(const char* ssid, const char* password);
  bool beginAccessPointOnce(const char* ssid, const char* password);

  void configureOTAHandlers();
  void startMDNS();
  void stopNetworking();
  bool hasValidIp(const IPAddress& ip) const;
  String normalizeHostname(const char* hostname) const;

  void processSerialCommands();
  void processTelnet();
  void startTelnetServer(uint16_t port);
  void stopTelnetServer();
  void processCommandLine(const String& command);
  bool startConfigPortalOnStation();
  bool startConfigPortalOnAccessPoint(const char* apSsid, const char* apPassword);
  void printConfigPortalEndpoints() const;
  void broadcastRaw(const char* message);
  void handleConfigPage();
  void handleConfigSave();
  void handleConfigScan();

  void stationListToMultiline(String& out) const;
  void parseAndSetStationList(const String& rawList);

  bool _enabled;
  bool _configured;
  bool _stationMode;
  bool _mdnsRunning;
  uint32_t _stationTimeoutMs;

  String _stationSsids[kMaxStationNetworks];
  String _stationPasswords[kMaxStationNetworks];
  uint8_t _stationCount;

  String _apSsid;
  String _apPassword;
  String _hostname;
  bool _preferAccessPoint;
  bool _safeP4Mode;

  WiFiServer _telnetServer;
  WiFiClient _telnetClient;
  bool _telnetEnabled;
  bool _telnetServerActive;
  uint16_t _telnetPort;
  String _telnetBuffer;

  bool _serialConfigEnabled;
  String _serialConfigCommand;
  String _serialBuffer;

  WebServer* _configServer;
  bool _configPortalRunning;
  bool _configPortalUsesAp;
  bool _awaitingPortalModeChoice;
  DNSServer _dnsServer;
  bool _dnsRunning;

  Preferences _prefs;
};
