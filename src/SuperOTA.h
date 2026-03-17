#pragma once

#include <Arduino.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include <WiFi.h>

class SuperOTA final {
public:
  static constexpr uint32_t kDefaultStationTimeoutMs = 20000;

  SuperOTA();

  void beginSerial(uint32_t baud = 115200);

  void setStationCredentials(const char* ssid, const char* password = nullptr);
  void setAccessPointCredentials(const char* ssid, const char* password = nullptr);
  void setHostname(const char* hostname);
  void setPreferAccessPoint(bool prefer);
  void setStationConnectTimeoutMs(uint32_t timeoutMs);
  void setSafeP4Mode(bool enable);
  bool safeP4Mode() const;
  bool isP4Target() const;

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

private:
  static constexpr const char* kDefaultApSsid = "Baratinha-OTA";
  static constexpr const char* kDefaultApPassword = "12345678";
  static constexpr const char* kDefaultHostname = "baratinha";
  static constexpr const char* kPrefsNamespace = "superota";

  bool configureAuto();
  bool beginStationOnce(const char* ssid, const char* password);
  bool beginAccessPointOnce(const char* ssid, const char* password);
  void configureOTAHandlers();
  void startMDNS();
  void stopNetworking();
  bool hasValidIp(const IPAddress& ip) const;
  String normalizeHostname(const char* hostname) const;

  bool _enabled;
  bool _configured;
  bool _stationMode;
  bool _mdnsRunning;
  uint32_t _stationTimeoutMs;

  String _staSsid;
  String _staPassword;
  String _apSsid;
  String _apPassword;
  String _hostname;
  bool _preferAccessPoint;
  bool _safeP4Mode;

  Preferences _prefs;
};
