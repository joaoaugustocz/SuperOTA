# SuperOTA

Subbiblioteca extraida da `Baratinha` com foco exclusivo em OTA para ESP32.

`SuperOTA` remove tudo que nao e necessario para atualizacao remota de firmware (motores, ToF, LEDs, botoes, telemetria e portal web) e mantem apenas o nucleo de conectividade/OTA.

## Objetivo

Fornecer uma biblioteca simples e reutilizavel para:

- conectar em WiFi no modo `station`;
- cair automaticamente para `access point` quando necessario;
- habilitar `ArduinoOTA`;
- anunciar via mDNS (`hostname.local`);
- salvar/carregar configuracao basica na NVS (`Preferences`).

## Compatibilidade P4 + C6 (modo seguro)

O `SuperOTA` agora detecta build para `ESP32-P4` e ativa automaticamente um tratamento de rede mais conservador:

- retries extras ao subir `station` e `access point`;
- pequeno tempo de estabilizacao apos associacao WiFi;
- fallback sem interromper OTA caso mDNS falhe.

Importante:

- em targets nao-P4, esse modo nao muda o fluxo padrao;
- voce pode ligar/desligar manualmente com `setSafeP4Mode(...)`.

## O que foi extraido da Baratinha

Componentes aproveitados do fluxo OTA original:

- estrategia de tentativa `station -> AP` (ou inversa, configuravel);
- inicializacao de `ArduinoOTA` com callbacks de log;
- suporte a hostname e mDNS;
- persistencia de SSID/senha/hostname/modo preferido.

Componentes propositalmente removidos por nao serem essenciais ao OTA:

- sensores e controle de motores;
- animacoes em LEDs;
- botao de recovery fisico;
- Telnet e telemetria;
- portal web de configuracao.

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
```

## Requisitos

- Placa ESP32
- Core Arduino para ESP32
- Bibliotecas do core:
  - `WiFi`
  - `ArduinoOTA`
  - `ESPmDNS`
  - `Preferences`

## Instalacao

1. Copie a pasta `SuperOTA` para sua pasta de bibliotecas Arduino.
2. Reinicie a IDE (Arduino IDE/PlatformIO, se aplicavel).
3. Inclua no sketch:

```cpp
#include <SuperOTA.h>
```

## Uso rapido

```cpp
#include <SuperOTA.h>

SuperOTA ota;

void setup() {
  ota.beginSerial(115200);

  ota.setStationCredentials("MinhaRede", "MinhaSenha");
  ota.setAccessPointCredentials("SuperOTA-Recovery", "12345678");
  ota.setHostname("superota-no1");
  ota.setPreferAccessPoint(false); // tenta station antes de AP
  // Opcional: no ESP32-P4 ja vem true por padrao.
  // ota.setSafeP4Mode(true);

  ota.begin();
}

void loop() {
  ota.loop(); // obrigatorio para OTA funcionar
}
```

## Exemplo com persistencia

```cpp
#include <SuperOTA.h>

SuperOTA ota;

void setup() {
  ota.beginSerial(115200);

  ota.loadPreferences();

  if (!ota.hasStationCredentials()) {
    ota.setStationCredentials("MinhaRede", "MinhaSenha");
    ota.setAccessPointCredentials("SuperOTA-Recovery", "12345678");
    ota.setHostname("superota-no1");
    ota.setPreferAccessPoint(false);
    ota.savePreferences();
  }

  ota.begin();
}

void loop() {
  ota.loop();
}
```

## API publica

### Configuracao

- `void beginSerial(uint32_t baud = 115200)`
- `void setStationCredentials(const char* ssid, const char* password = nullptr)`
- `void setAccessPointCredentials(const char* ssid, const char* password = nullptr)`
- `void setHostname(const char* hostname)`
- `void setPreferAccessPoint(bool prefer)`
- `void setStationConnectTimeoutMs(uint32_t timeoutMs)`
- `void setSafeP4Mode(bool enable)`
- `bool safeP4Mode() const`
- `bool isP4Target() const`

### Inicializacao OTA

- `bool begin()`
  - Inicializa usando o fluxo automatico com fallback.
- `bool beginStation(const char* ssid, const char* password = nullptr)`
  - Forca inicializacao direta em `WIFI_STA`.
- `bool beginAccessPoint(const char* ssid = nullptr, const char* password = nullptr)`
  - Forca inicializacao direta em `WIFI_AP`.
- `void loop()`
  - Deve ser chamado continuamente para processar `ArduinoOTA.handle()`.

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

Namespace NVS usado: `superota`

Chaves usadas:

- `preferAP`
- `staSsid`
- `staPass`
- `apSsid`
- `apPass`
- `hostname`

## Fluxo de decisao (`begin`)

- Se `preferAccessPoint == false`:
  1. Tenta `station` (se houver SSID).
  2. Se falhar, sobe em `access point`.
- Se `preferAccessPoint == true`:
  1. Sobe em `access point`.
  2. Se falhar e houver SSID de station, tenta `station`.

## Migracao a partir de Baratinha

Mapeamento principal:

- `Baratinha::setupOTAStation(...)` -> `SuperOTA::beginStation(...)`
- `Baratinha::setupOTAAccessPoint(...)` -> `SuperOTA::beginAccessPoint(...)`
- `Baratinha::enableOTA(...)` -> `SuperOTA::enable(...)`
- `Baratinha::setStationCredentials(...)` -> `SuperOTA::setStationCredentials(...)`
- `Baratinha::setAccessPointCredentials(...)` -> `SuperOTA::setAccessPointCredentials(...)`
- `Baratinha::setPreferAccessPoint(...)` -> `SuperOTA::setPreferAccessPoint(...)`
- `Baratinha::setHostname(...)` -> `SuperOTA::setHostname(...)`
- `Baratinha::processOTA()` -> `SuperOTA::loop()`

## Boas praticas

- Sempre chame `ota.loop()` no `loop()` principal.
- Use senha de AP com pelo menos 8 caracteres (ou string vazia para AP aberto).
- Em producao, prefira AP de fallback apenas para recuperacao.
- Defina `hostname` unico por dispositivo.

## Troubleshooting

### OTA nao aparece na IDE

- Confirme que `ota.begin()` retornou `true`.
- Confirme que o dispositivo e o computador estao na mesma rede.
- Valide `hostname.local` e IP no serial monitor.

### Cai sempre em AP

- Verifique SSID/senha de station.
- Aumente timeout com `setStationConnectTimeoutMs(...)`.

### mDNS nao resolve

- Tente upload OTA pelo IP direto.
- Verifique se a rede bloqueia multicast.

## Licenca

Defina a licenca do seu projeto conforme sua politica (ex.: MIT, Apache-2.0).
