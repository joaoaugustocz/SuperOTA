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
- Modo debug sob demanda para metricas de captive portal/AP.

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
  library.json
  keywords.txt
  LICENSE
  .github/workflows/ci.yml
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
    PersistenceWithFactoryReset/
      PersistenceWithFactoryReset.ino
    FreeRTOSBasic/
      FreeRTOSBasic.ino
    FreeRTOSOtaServiceQueue/
      FreeRTOSOtaServiceQueue.ino
```

## Requisitos

- ESP32
- Core Arduino para ESP32
- Bibliotecas do core: `WiFi`, `ArduinoOTA`, `ESPmDNS`, `Preferences`, `WebServer`, `DNSServer`

## PlatformIO.ini (obrigatorio para OTA + serial Wi-Fi)

Use este bloco como base no seu `platformio.ini`:

```ini
[env:esp32-p4-evboard]
platform = https://github.com/pioarduino/platform-espressif32.git
board = esp32-p4-evboard
framework = arduino

build_flags =
  -D ARDUINO_USB_CDC_ON_BOOT=0
  -D CORE_DEBUG_LEVEL=0

; Upload OTA de firmware (ArduinoOTA)
upload_protocol = espota
upload_port = superota.local
upload_flags =
  --port=3232

; Monitor serial sem fio (Telnet SuperOTA)
monitor_speed = 115200
monitor_port = socket://superota.local:23
```

Importante:

- `upload_protocol`, `upload_port` e `upload_flags` **nao** podem ter espaco no inicio da linha.
- Se essas chaves ficarem indentadas, o PlatformIO pode tratar como `build_flags`, gerando erros como:
  - `unrecognized command-line option '--port=3232'`
  - erros de linker com `cannot find -l...upload_protocol` etc.
- Para usar serial USB local, troque para `monitor_port = COMx`.

## Fluxo inteligente de station

Quando existem redes station cadastradas:

1. `SuperOTA` escaneia redes visiveis com `WiFi.scanNetworks()`.
2. Procura a primeira rede encontrada respeitando sua ordem de prioridade na lista.
3. Conecta apenas em uma rede detectada no scan.
4. Se nao encontrar nenhuma, faz fallback para AP (dependendo da configuracao).

Toda a decisao e impressa via `Serial`.

## Persistencia apos reboot e update

Resumo direto:

- Sim, o que voce salva no `configota` fica na NVS.
- Reiniciar ou cortar energia nao apaga essas configuracoes.
- Update OTA de firmware normalmente tambem nao apaga NVS.

Quando pode perder dados:

- se o firmware chamar `clearPreferences()`;
- se voce fizer erase total de flash;
- se houver mudanca de tabela de particoes que remova/altere NVS.

Padrao recomendado no `setup()`:

1. `loadPreferences()`
2. se nao houver dados (`!hasStationCredentials()`), aplicar defaults
3. `savePreferences()` apenas nesse primeiro setup

Isso evita sobrescrever no boot o que foi salvo via portal.

## Uso com FreeRTOS (SO)

Abordagens possiveis:

1. `ota.loop()` no `loop()` Arduino + tasks FreeRTOS para aplicacao.
2. Task dedicada para OTA/rede chamando `ota.loop()` periodicamente.
3. Servico OTA com fila de comandos (task unica dona da biblioteca).
4. (Opcional) fixar tasks por core em chips multicore.

Recomendacao:

- Comece pela abordagem 2.
- Em projeto de producao, evolua para abordagem 3 (fila), pois reduz risco de concorrencia.

Regras praticas para evitar race condition:

- Defina uma task "dona" da `SuperOTA` em runtime.
- Evite chamar metodos da biblioteca a partir de multiplas tasks ao mesmo tempo.
- Se outra task precisar acionar OTA/portal/debug, envie comando por fila para a task dona.
- Nao tenha duas tasks lendo `Serial` em paralelo sem coordenacao.

Timing e prioridade sugeridos:

- Chamar `ota.loop()` a cada `5-20ms`.
- Task OTA com prioridade maior que tarefas de baixa criticidade (ex.: OTA = 3, app = 1-2).
- Evite blocos longos na task dona da OTA.

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

Comportamento em AP:

- URL principal recomendada: `http://192.168.4.1` (ou IP que o AP informar no serial).
- `http://hostname.local` em AP depende de suporte mDNS do cliente/rede e pode nao resolver em todos os celulares/PCs.
- A biblioteca responde endpoints de captive portal (`/generate_204`, `/hotspot-detect.html`, `/ncsi.txt`, etc.) com redirecionamento para aumentar a chance de autoabertura.

Diagnostico rapido quando nao abre automaticamente:

- Ative debug (`debug-on`) e verifique no serial se aparecem logs `Captive probe: ...` apos conectar no AP.
- Se nao houver probe, o cliente nao iniciou teste captive (comum por politica do SO/rede salva).
- No Android, teste com `DNS Privado` em Automatico/Desligado.
- Esqueca a rede AP no celular/PC e conecte novamente.
- Acesso manual sempre funciona em `http://192.168.4.1`.

Sobre serial:

- comandos (`configota`, `1`, `2`, etc.) podem ser enviados por:
  - USB/COM no monitor serial, ou
  - Telnet em `socket://hostname.local:23`

Mapa rapido de portas:

- Portal web de configuracao: `http://hostname.local:80` (ou IP:80)
- OTA (upload de firmware): porta `3232` (ArduinoOTA / espota)
- Serial sem fio (Telnet): porta `23`
- Serial USB local: COMx

## Modo debug de metricas

Para ligar/desligar metricas detalhadas de AP/captive:

- comando serial/Telnet `debug-on` -> ativa eventos e resumo periodico
- comando serial/Telnet `debug-summary` -> imprime resumo na hora
- comando serial/Telnet `debug-off` -> desativa debug

Por codigo:

```cpp
ota.enableDebugMetrics(true);      // liga
ota.setDebugSummaryIntervalMs(30000); // resumo automatico (padrao: 30s)
ota.printDebugSummary();           // resumo manual
```

Quando debug esta desligado, os logs verbosos de captive probe nao sao impressos.

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

Esse exemplo ja segue o padrao de persistencia segura:

- carrega NVS primeiro;
- grava defaults somente quando NVS estiver vazia.

### 4) Persistencia + reset de fabrica da NVS

Use [examples/PersistenceWithFactoryReset/PersistenceWithFactoryReset.ino](examples/PersistenceWithFactoryReset/PersistenceWithFactoryReset.ino)

Comandos extras desse exemplo:

- `nvs-status` -> mostra estado basico salvo
- `nvs-clear` -> limpa NVS e reinicia (simula reset de fabrica)

### 5) FreeRTOS basico (task dedicada OTA)

Use [examples/FreeRTOSBasic/FreeRTOSBasic.ino](examples/FreeRTOSBasic/FreeRTOSBasic.ino)

Esse exemplo mostra:

- task dedicada para `ota.loop()`;
- task separada de aplicacao;
- uso de `configota` mantendo arquitetura simples com SO.

### 6) FreeRTOS com servico OTA + fila (producao)

Use [examples/FreeRTOSOtaServiceQueue/FreeRTOSOtaServiceQueue.ino](examples/FreeRTOSOtaServiceQueue/FreeRTOSOtaServiceQueue.ino)

Esse exemplo mostra:

- task OTA como servico unico (ownership da biblioteca);
- task de console enviando comandos por fila;
- comandos: `portal-ap`, `portal-stop`, `debug-on`, `debug-off`, `debug-summary`, `status`.

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
- `void enableDebugMetrics(bool enable = true)`
- `bool debugMetricsEnabled() const`
- `void setDebugSummaryIntervalMs(uint32_t intervalMs)`
- `uint32_t debugSummaryIntervalMs() const`
- `void printDebugSummary()`

### Portal / serial

- `void enableTelnetSerial(bool enable, uint16_t port = 23)`
- `bool telnetSerialEnabled() const`
- `uint16_t telnetPort() const`
- `bool telnetClientConnected()`
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

MIT. Veja o arquivo `LICENSE`.
