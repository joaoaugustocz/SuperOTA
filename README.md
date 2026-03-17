# SuperOTA

Subbiblioteca extraida da `Baratinha` com foco exclusivo em OTA para ESP32.

## O que essa versao adiciona

- Exemplo e fluxo de inicializacao AP-only.
- Lista de redes station (ate `kMaxStationNetworks`) com scan inteligente.
- Portal web de configuracao para editar:
  - hostname;
  - preferencia de inicializacao (STA/AP);
  - credenciais AP;
  - lista de redes station.
- Entrada no portal por comando serial (default: `configota`).

## Visao geral

`SuperOTA` oferece:

- OTA via `ArduinoOTA`;
- fallback STA/AP;
- mDNS (`hostname.local`);
- persistencia NVS (`Preferences`);
- modo seguro automatico para target ESP32-P4.

## Estrutura

```text
SuperOTA/
  library.properties
  README.md
  src/
    SuperOTA.h
    SuperOTA.cpp
  examples/
    BasicStationOrAP/
      BasicStationOrAP.ino
    BasicAccessPointOnly/
      BasicAccessPointOnly.ino
    StationListWithSerialConfig/
      StationListWithSerialConfig.ino
```

## Requisitos

- ESP32
- Core Arduino para ESP32
- Bibliotecas do core: `WiFi`, `ArduinoOTA`, `ESPmDNS`, `Preferences`, `WebServer`, `DNSServer`

## Fluxo inteligente de station

Quando existem redes station cadastradas:

1. `SuperOTA` escaneia redes visiveis com `WiFi.scanNetworks()`.
2. Procura a primeira rede encontrada respeitando sua ordem de prioridade na lista.
3. Conecta apenas em uma rede detectada no scan.
4. Se nao encontrar nenhuma, faz fallback para AP (dependendo da configuracao).

Toda a decisao e impressa via `Serial`.

## Portal de configuracao

Com serial habilitado, digite no monitor serial:

- `configota` -> inicia fluxo de abertura do portal
- se estiver em station conectado, a biblioteca pergunta:
  - `1` = abrir portal em station (`http://hostname.local`)
  - `2` = abrir portal em AP (captive portal)
- `config-stop` -> encerra portal e retoma configuracao OTA
- `config-help` -> mostra comandos

Quando aberto em station, a pagina fica disponivel em:

- `http://hostname.local`
- `http://<ip_station>`
- porta HTTP: `80`

Quando aberto em AP, a biblioteca sobe um AP de setup com captive portal (DNS catch-all) para abrir a pagina automaticamente apos conectar.

Sobre serial:

- comandos (`configota`, `1`, `2`, etc.) podem ser enviados por:
  - USB/COM no monitor serial, ou
  - Telnet em `socket://hostname.local:23`

Mapa rapido de portas:

- Portal web de configuracao: `http://hostname.local:80` (ou IP:80)
- OTA (upload de firmware): porta `3232` (ArduinoOTA / espota)
- Serial sem fio (Telnet): porta `23`
- Serial USB local: COMx

No portal voce consegue editar:

- hostname
- preferencia de inicializacao (priorizar AP)
- AP SSID/senha
- lista station (uma por linha em `SSID;senha`)

Tambem existe endpoint de scan:

- `GET /scan` -> lista redes detectadas (SSID, RSSI, canal)

## Exemplos

### 1) AP-only

Use [examples/BasicAccessPointOnly/BasicAccessPointOnly.ino](examples/BasicAccessPointOnly/BasicAccessPointOnly.ino)

### 2) Station + AP fallback

Use [examples/BasicStationOrAP/BasicStationOrAP.ino](examples/BasicStationOrAP/BasicStationOrAP.ino)

### 3) Lista station + portal serial

Use [examples/StationListWithSerialConfig/StationListWithSerialConfig.ino](examples/StationListWithSerialConfig/StationListWithSerialConfig.ino)

## API publica

### Configuracao

- `void beginSerial(uint32_t baud = 115200)`
- `void setStationCredentials(const char* ssid, const char* password = nullptr)`
- `bool addStationNetwork(const char* ssid, const char* password = nullptr)`
- `void clearStationNetworks()`
- `uint8_t stationNetworkCount() const`
- `void setAccessPointCredentials(const char* ssid, const char* password = nullptr)`
- `void setHostname(const char* hostname)`
- `void setPreferAccessPoint(bool prefer)`
- `void setStationConnectTimeoutMs(uint32_t timeoutMs)`
- `void setSafeP4Mode(bool enable)`
- `bool safeP4Mode() const`
- `bool isP4Target() const`

### Portal / serial

- `void enableTelnetSerial(bool enable, uint16_t port = 23)`
- `bool telnetSerialEnabled() const`
- `uint16_t telnetPort() const`
- `bool telnetClientConnected() const`
- `void enableSerialConfigCommand(bool enable = true, const char* command = "configota")`
- `bool startConfigPortal(const char* apSsid = nullptr, const char* apPassword = nullptr)`
- `void stopConfigPortal(bool resumeAuto = true)`
- `bool configPortalRunning() const`

### Inicializacao OTA

- `bool begin()`
- `bool beginStation(const char* ssid, const char* password = nullptr)`
- `bool beginAccessPoint(const char* ssid = nullptr, const char* password = nullptr)`
- `void loop()`

### Estado

- `void enable(bool enable)`
- `bool enabled() const`
- `bool isConfigured() const`
- `bool isStationMode() const`
- `bool hasStationCredentials() const`
- `IPAddress ip() const`

### Persistencia (NVS)

- `bool loadPreferences()`
- `bool savePreferences()`
- `bool clearPreferences()`

Namespace NVS: `superota`

Chaves:

- `preferAP`
- `staSsid` / `staPass` (compatibilidade)
- `staList` (lista station)
- `apSsid`
- `apPass`
- `hostname`

## Modo seguro P4 + C6

No build ESP32-P4, o modo seguro e ligado por padrao:

- retries extras em STA/AP;
- atraso curto para estabilizacao de link;
- mDNS nao bloqueia o OTA em caso de falha.

Em outros targets, esse modo nao altera o comportamento padrao.

## Boas praticas

- Sempre chame `ota.loop()` no loop principal.
- Mantenha AP senha com 8+ caracteres (ou vazio para aberto).
- Defina prioridade das redes station na ordem de insercao.
- Use `savePreferences()` apos alterar configuracoes no codigo.

## Licenca

Defina a licenca do seu projeto conforme sua politica (ex.: MIT, Apache-2.0).
