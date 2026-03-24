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
  void enableDebugMetrics(bool enable = true);
  bool debugMetricsEnabled() const;
  void setDebugSummaryIntervalMs(uint32_t intervalMs);
  uint32_t debugSummaryIntervalMs() const;
  void printDebugSummary();

  void enableTelnetSerial(bool enable, uint16_t port = 23);
  bool telnetSerialEnabled() const;
  uint16_t telnetPort() const;
  bool telnetClientConnected();
  void setOtaPassword(const char* password);
  bool otaPasswordEnabled() const;
  void setPortalPassword(const char* password);
  void setUseOtaPasswordForPortal(bool enable);
  bool usingOtaPasswordForPortal() const;
  bool portalPasswordEnabled() const;
  String hostname() const;
  String accessPointSsid() const;
  bool accessPointPasswordEnabled() const;

  void enableSerialConfigCommand(bool enable = true, const char* command = "configota");
  bool startConfigPortal(const char* apSsid = nullptr, const char* apPassword = nullptr);
  bool startConfigPortalOnStation();
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
  bool startConfigPortalOnAccessPoint(const char* apSsid, const char* apPassword);
  bool ensurePortalAuthentication();
  String effectivePortalPassword() const;
  void runConfigPortalForegroundLoop();
  void handleDeferredPortalStop();
  void printConfigPortalEndpoints() const;
  void broadcastRaw(const char* message);
  void handleConfigPage();
  void handleConfigSave();
  void handleConfigScan();

  void stationListToMultiline(String& out) const;
  String normalizeStationListInput(const String& rawList) const;
  void parseAndSetStationList(const String& rawList);
  void resetDebugMetrics();
  void handleDebugWifiEvent(arduino_event_id_t event, arduino_event_info_t info);
  void updateDebugSummary();
  int findDebugClientIndex(const uint8_t mac[6]) const;
  int getOrCreateDebugClientIndex(const uint8_t mac[6]);
  static bool debugMacEqual(const uint8_t a[6], const uint8_t b[6]);
  static String debugMacToString(const uint8_t mac[6]);

  static constexpr uint32_t kDefaultDebugSummaryIntervalMs = 30000;
  static constexpr uint32_t kDebugLateDhcpThresholdMs = 1500;
  static constexpr uint8_t kDebugMaxTrackedClients = 16;

  struct DebugClient {
    bool used = false;
    uint8_t mac[6] = {0};
    uint32_t connectMs = 0;
  };

  struct DebugStats {
    uint32_t bootMs = 0;
    uint32_t apStartCount = 0;
    uint32_t connectCount = 0;
    uint32_t disconnectCount = 0;
    uint32_t ipAssignedCount = 0;
    uint32_t unknownIpAssignedCount = 0;
    uint32_t dhcpMinMs = 0xFFFFFFFFUL;
    uint32_t dhcpMaxMs = 0;
    uint64_t dhcpSumMs = 0;
    uint32_t lateDhcpCount = 0;
  };

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
  String _otaPassword;
  String _portalPassword;
  bool _useOtaPasswordForPortal;
  bool _preferAccessPoint;
  bool _safeP4Mode;
  bool _debugMetricsEnabled;
  uint32_t _debugSummaryIntervalMs;
  uint32_t _debugLastSummaryMs;
  wifi_event_id_t _debugEventHandlerId;
  bool _debugEventHandlerRegistered;
  DebugStats _debugStats;
  DebugClient _debugClients[kDebugMaxTrackedClients];

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
  bool _deferredPortalStop;
  bool _deferredPortalResumeAuto;
  uint32_t _deferredPortalStopAfterMs;
  bool _deferredPortalRestart;
  DNSServer _dnsServer;
  bool _dnsRunning;

  Preferences _prefs;
};
