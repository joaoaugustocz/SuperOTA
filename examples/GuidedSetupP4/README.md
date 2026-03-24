# GuidedSetupP4

Exemplo guiado para quem vai configurar a `SuperOTA` pela primeira vez em um ESP32-P4.

## O que este exemplo mostra

- inicializacao serial;
- `loadPreferences()` antes de aplicar defaults;
- como evitar sobrescrever configuracao salva quando a NVS falha;
- definicao do AP operacional;
- lista de redes station priorizada;
- senha OTA + senha do portal usando a mesma credencial;
- fluxo `configota` em station e em AP.

## Fluxo do sketch

1. inicia `Serial` e habilita `configota`;
2. liga Telnet serial na porta `23`;
3. liga `safeP4Mode`;
4. tenta `loadPreferences()`;
5. se a NVS falhar, aplica defaults apenas em RAM;
6. se a NVS estiver valida e vazia, grava um perfil inicial;
7. aplica senha OTA inicial somente se ainda nao houver uma salva;
8. chama `begin()` para iniciar em station ou AP;
9. mantem `ota.loop()` rodando no `loop()`.

## Comportamento esperado

### Quando existe station valida

- o P4 conecta em `MinhaRede` ou `MinhaRedeBackup`;
- `configota` pergunta:
  - `1` para abrir o portal no station;
  - `2` para abrir o portal em AP.

### Quando nao existe station valida

- o P4 entra em AP com o SSID configurado em `setAccessPointCredentials`;
- `configota` reutiliza esse AP atual;
- a URL de fallback continua sendo `http://192.168.4.1`.

## PlatformIO.ini minimo para ESP32-P4

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

; Para subir firmware via OTA depois que o station estiver funcional:
; upload_protocol = espota
; upload_port = superota-p4-guide.local
; upload_flags =
;   --port=3232
;   --auth=TroqueEssaSenha123!

; Para serial via Telnet:
; monitor_port = socket://superota-p4-guide.local:23
```

## Ajustes que normalmente voce troca

- `kHostname`
- `kAccessPointSsid`
- `kAccessPointPassword`
- `kOtaPassword`
- as redes em `applyDefaultProfile()`

## Observacoes para outros ESP32

- o exemplo foi escrito pensando primeiro no ESP32-P4;
- em ESP32, S2, S3 e C3 a logica geral continua valida;
- a diferenca principal e que o `safeP4Mode` nao altera o comportamento fora do P4.
