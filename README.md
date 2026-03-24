# SuperOTA

Biblioteca OTA para ESP32 com fallback entre station e access point, portal web de configuracao, mDNS, serial via Telnet e persistencia em NVS.

## Visao geral

`SuperOTA` resolve quatro problemas comuns em projetos ESP32:

- subir firmware via WiFi usando `ArduinoOTA`;
- conectar automaticamente em uma lista priorizada de redes station;
- cair para AP quando nao houver station valida;
- abrir um portal web para editar a configuracao sem recompilar o firmware.

A biblioteca foi validada com foco especial em `ESP32-P4`, mas o desenho geral tambem serve para outros ESP32 suportados pelo core Arduino.

## Instalacao

### Arduino IDE

1. Instale a biblioteca `SuperOTA`.
2. Certifique-se de estar usando um core Arduino para ESP32 compativel.
3. Inclua no sketch:

```cpp
#include <SuperOTA.h>
```

### PlatformIO

Use a biblioteca pelo GitHub/release ou por caminho local.

Exemplo com caminho local:

```ini
lib_deps =
  file://C:/caminho/absoluto/para/SuperOTA
```

## Configuracao rapida

Fluxo recomendado no `setup()`:

1. `beginSerial()`
2. `enableSerialConfigCommand(true, "configota")`
3. `loadPreferences()`
4. se a NVS estiver valida e ainda nao houver perfil salvo, aplicar defaults e `savePreferences()`
5. `begin()`
6. manter `loop()` rodando continuamente

Exemplo minimo:

```cpp
#include <Arduino.h>
#include <SuperOTA.h>

SuperOTA ota;

void setup() {
  ota.beginSerial(115200);
  ota.enableSerialConfigCommand(true, "configota");

  const bool prefsLoaded = ota.loadPreferences();
  if (prefsLoaded && !ota.hasStationCredentials()) {
    ota.setHostname("meu-dispositivo");
    ota.setPreferAccessPoint(false);
    ota.setAccessPointCredentials("MeuAP-OTA", "12345678");
    ota.addStationNetwork("MinhaRede", "MinhaSenha");
    ota.addStationNetwork("MinhaRedeBackup", "MinhaSenhaBackup");
    ota.savePreferences();
  }

  ota.begin();
}

void loop() {
  ota.loop();
}
```

## Fluxo recomendado de setup

Padrao seguro para producao:

1. tente `loadPreferences()` sempre no inicio;
2. se `loadPreferences()` falhar, trate como falha de NVS e rode com defaults apenas em RAM;
3. grave defaults com `savePreferences()` somente quando a NVS estiver acessivel e vazia;
4. nao reaplique defaults em todo boot, ou voce sobrescreve o que foi salvo pelo portal;
5. use `configota` para manutencao em campo.

Padrao errado:

```cpp
ota.loadPreferences();
if (!ota.hasStationCredentials()) {
  // defaults...
  ota.savePreferences();
}
```

Esse padrao e perigoso quando a NVS falha na leitura. Nessa situacao `hasStationCredentials()` pode voltar `false` e o exemplo acaba gravando defaults por cima de uma configuracao que deveria ter sido apenas preservada.

## Como funciona o `configota`

Com o parser serial interno habilitado, o comando `configota` funciona assim:

### Quando o dispositivo esta em station conectado

A biblioteca pergunta no serial:

- `1` = abrir o portal no station atual;
- `2` = abrir o portal em AP.

Station e util quando voce ja tem IP valido e quer acessar:

- `http://hostname.local`
- `http://<ip-station>`

AP e util quando voce quer forcar um portal de configuracao isolado, com `http://192.168.4.1` como fallback.

### Quando o dispositivo ja esta em AP

`configota` reutiliza o AP atual e abre o portal nele. Nao ha troca de SSID no meio do fluxo.

### Quando o dispositivo nao esta em station conectado

`configota` abre o portal em AP usando o AP operacional configurado na biblioteca.

## Station vs AP

A biblioteca trabalha com dois modos operacionais:

### Station

- tenta conectar em uma das redes cadastradas;
- respeita a ordem de prioridade da lista station;
- usa scan para escolher apenas redes realmente visiveis;
- se conectar, ativa OTA, mDNS, Telnet serial e pode abrir portal no proprio station.

### Access Point

- e usado como fallback quando nao existe station disponivel;
- tambem pode ser escolhido explicitamente no portal/configota;
- usa o SSID definido por `setAccessPointCredentials()` como AP operacional;
- o mesmo SSID e reutilizado pelo portal quando ele e aberto em AP por padrao.

Na pratica, isso significa que `startConfigPortal()` sem argumentos agora usa o AP operacional configurado, em vez de criar um SSID aleatorio de setup quando isso nao for necessario.

## Persistencia na NVS

A biblioteca salva configuracoes no namespace `superota` via `Preferences`.

Persistido na NVS:

- hostname;
- preferencia AP/station;
- AP SSID e AP senha;
- lista station;
- senha OTA;
- senha do portal;
- flag para reutilizar a senha OTA no portal.

O que normalmente sobrevive:

- reboot;
- power cycle;
- update OTA comum de firmware.

O que normalmente apaga a configuracao:

- `clearPreferences()`;
- erase total da flash;
- mudanca de particoes que destrua a NVS.

## Senhas OTA e portal

A biblioteca suporta dois controles de acesso independentes:

- senha OTA para upload na porta `3232`;
- senha do portal via HTTP Basic (`usuario: admin`).

Voce pode usar:

- a mesma senha para OTA e portal; ou
- uma senha separada para o portal.

API de seguranca:

- `setOtaPassword(const char* password)`
- `otaPasswordEnabled() const`
- `setPortalPassword(const char* password)`
- `setUseOtaPasswordForPortal(bool enable)`
- `usingOtaPasswordForPortal() const`
- `portalPasswordEnabled() const`

Recomendacao pratica:

1. use senha OTA forte;
2. mantenha o portal fechado e abra apenas sob comando;
3. use o mesmo segredo para OTA + portal se quiser operacao mais simples em campo;
4. use senha separada apenas quando isso fizer sentido operacionalmente.

## PlatformIO.ini

### Exemplo para ESP32-P4 com upload serial inicial

```ini
[env:esp32-p4-evboard]
platform = https://github.com/pioarduino/platform-espressif32.git
board = esp32-p4-evboard
framework = arduino

lib_deps =
  file://C:/caminho/absoluto/para/SuperOTA

build_flags =
  -D ARDUINO_USB_CDC_ON_BOOT=0
  -D CORE_DEBUG_LEVEL=0

monitor_speed = 115200
monitor_port = COM5
```

### Exemplo para OTA depois que o station estiver funcional

```ini
upload_protocol = espota
upload_port = superota.local
upload_flags =
  --port=3232
  --auth=TroqueEssaSenha123!
```

### Exemplo para serial por Telnet

```ini
monitor_port = socket://superota.local:23
```

Importante:

- `upload_protocol`, `upload_port` e `upload_flags` precisam ficar no nivel correto do `ini`;
- se voce indentar isso errado, o PlatformIO pode tentar tratar essas linhas como `build_flags`;
- se estiver usando a biblioteca local, force refresh quando necessario:

```powershell
pio pkg install -d . -e <seu-env> --library "file://C:/caminho/para/SuperOTA" --force
pio run -t clean
pio run -t upload
```

## Uso com FreeRTOS

Existem tres abordagens validas:

1. `ota.loop()` no `loop()` Arduino e tasks separadas para a aplicacao;
2. task dedicada para `ota.loop()`;
3. servico OTA com fila, onde uma task e a unica dona da biblioteca.

Recomendacao:

- projeto simples: abordagem 2;
- projeto mais robusto: abordagem 3.

Regras para nao criar race condition:

- nao deixe duas tasks chamando metodos da `SuperOTA` ao mesmo tempo;
- se outra task precisar abrir portal, ligar debug ou imprimir status, envie um comando para a task dona;
- nao tenha duas tasks consumindo `Serial` sem coordenacao.

O exemplo `FreeRTOSOtaServiceQueue` agora espelha o comportamento da biblioteca:

- `configota` em station abre escolha `1` / `2`;
- `configota` em AP abre o portal no AP atual;
- `portal-sta` abre direto no station;
- `portal-ap` abre direto em AP;
- `portal-stop` e `config-stop` fecham o portal.

## Notas especificas do ESP32-P4

O P4 usa uma pilha de rede diferente quando combinado com ESP-Hosted/C6. Por isso a biblioteca tem um modo seguro especifico.

No P4:

- retries extras em STA/AP podem ser necessarios;
- `H_API: ESP-Hosted link not yet up` pode aparecer no boot antes da estabilizacao do C6;
- mudancas de topologia feitas pelo portal podem exigir reinicio controlado;
- mudancas apenas de seguranca agora sao reaplicadas sem reboot.

Politica atual ao salvar no portal no P4:

- se mudar apenas senha OTA/portal, a biblioteca reaplica sem reboot;
- se mudar hostname, preferencia AP, AP SSID/senha ou lista station, a biblioteca agenda reinicio seguro.

## Troubleshooting

### O Windows mostrou `SuperOTA-Recovery 2`

Isso normalmente e um rotulo do cliente Windows para uma rede ja conhecida, nao o SSID real transmitido pelo dispositivo. Confirme o SSID real no log serial da placa.

### `http://hostname.local` nao abre

`hostname.local` depende de mDNS no cliente. Em muitos cenarios o acesso correto e:

- `http://hostname.local` quando o cliente suporta mDNS;
- `http://<ip-station>` em station;
- `http://192.168.4.1` em AP.

### O portal nao abriu automaticamente

Autoabertura de captive portal depende do sistema operacional. Acesse manualmente:

- `http://192.168.4.1` quando estiver em AP;
- `http://hostname.local` ou `http://<ip-station>` quando estiver em station.

### O portal abriu em AP com SSID diferente do esperado

Se voce chamou `startConfigPortal()` sem argumentos, o comportamento atual e:

- reutilizar o AP atual quando ja estiver em AP;
- usar o AP operacional configurado (`setAccessPointCredentials`) quando abrir em AP a partir de station.

Se aparecer outro SSID, o firmware em execucao provavelmente nao e o mais recente ou o projeto esta usando cache antigo da biblioteca.

### Falha de NVS

Se o log mostrar `NVS indisponivel para leitura`, trate isso como falha de persistencia. Os exemplos oficiais atualizados passaram a rodar com defaults apenas em RAM nessa situacao, sem gravar por cima da configuracao salva.

## Exemplos

### `BasicAccessPointOnly`

AP fixo com OTA e sem uso de station.

### `BasicStationOrAP`

Fluxo basico de station com fallback para AP e persistencia segura.

### `StationListWithSerialConfig`

Lista de redes station priorizada + `configota` + debug opcional.

### `PersistenceWithFactoryReset`

Mostra persistencia em NVS e inclui `nvs-clear` para reset de fabrica.

### `SecurityOtaAndPortalPassword`

Exemplo focado em senha OTA e protecao do portal.

### `FreeRTOSBasic`

Task dedicada para `ota.loop()` e task separada para aplicacao.

### `FreeRTOSOtaServiceQueue`

Servico OTA com fila, ownership unico da biblioteca e console espelhando o fluxo oficial do `configota`.

### `GuidedSetupP4`

Exemplo novo, mais detalhado, pensado primeiro para ESP32-P4.

Arquivos relevantes:

- `examples/GuidedSetupP4/GuidedSetupP4.ino`
- `examples/GuidedSetupP4/README.md`

## API publica

### Configuracao

- `beginSerial(uint32_t baud = 115200)`
- `setStationCredentials(const char* ssid, const char* password = nullptr)`
- `addStationNetwork(const char* ssid, const char* password = nullptr)`
- `clearStationNetworks()`
- `stationNetworkCount() const`
- `setAccessPointCredentials(const char* ssid, const char* password = nullptr)`
- `setHostname(const char* hostname)`
- `hostname() const`
- `accessPointSsid() const`
- `accessPointPasswordEnabled() const`
- `setPreferAccessPoint(bool prefer)`
- `setStationConnectTimeoutMs(uint32_t timeoutMs)`
- `setSafeP4Mode(bool enable)`
- `safeP4Mode() const`
- `isP4Target() const`

### OTA, portal e debug

- `enableTelnetSerial(bool enable, uint16_t port = 23)`
- `telnetSerialEnabled() const`
- `telnetPort() const`
- `telnetClientConnected()`
- `setOtaPassword(const char* password)`
- `otaPasswordEnabled() const`
- `setPortalPassword(const char* password)`
- `setUseOtaPasswordForPortal(bool enable)`
- `usingOtaPasswordForPortal() const`
- `portalPasswordEnabled() const`
- `enableSerialConfigCommand(bool enable = true, const char* command = "configota")`
- `startConfigPortal(const char* apSsid = nullptr, const char* apPassword = nullptr)`
- `startConfigPortalOnStation()`
- `stopConfigPortal(bool resumeAuto = true)`
- `configPortalRunning() const`
- `enableDebugMetrics(bool enable = true)`
- `debugMetricsEnabled() const`
- `setDebugSummaryIntervalMs(uint32_t intervalMs)`
- `debugSummaryIntervalMs() const`
- `printDebugSummary()`

### Inicializacao e estado

- `begin()`
- `beginStation(const char* ssid, const char* password = nullptr)`
- `beginAccessPoint(const char* ssid = nullptr, const char* password = nullptr)`
- `loop()`
- `enable(bool enable)`
- `enabled() const`
- `isConfigured() const`
- `isStationMode() const`
- `hasStationCredentials() const`
- `ip() const`

### Persistencia

- `loadPreferences()`
- `savePreferences()`
- `clearPreferences()`

## Boas praticas

- chame `loadPreferences()` antes de aplicar defaults;
- grave defaults apenas quando a NVS estiver acessivel e vazia;
- mantenha `ota.loop()` sendo executado continuamente;
- trate `configota` como fluxo operacional principal, nao como ajuste de excecao;
- em FreeRTOS, defina ownership claro da biblioteca;
- em P4, prefira manter `safeP4Mode` ligado;
- use senha OTA em qualquer uso fora de bancada.

## Licenca

MIT. Veja `LICENSE`.
