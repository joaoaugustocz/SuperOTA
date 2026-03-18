# Changelog

## 1.1.8 - 2026-03-18
- Novos exemplos FreeRTOS:
  - `FreeRTOSBasic` (task dedicada para `ota.loop()` + task de aplicacao).
  - `FreeRTOSOtaServiceQueue` (servico OTA com fila de comandos entre tasks).
- README atualizado com secao dedicada de uso com SO/FreeRTOS:
  - abordagens recomendadas;
  - regras de ownership/thread-safety;
  - sugestoes de prioridade e periodicidade de `ota.loop()`.

## 1.1.7 - 2026-03-18
- Correcao de metadados de autoria para `João Augusto Carvalho Araújo`:
  - `library.properties` (`author` e `maintainer`);
  - `library.json` (`authors`);
  - `LICENSE` (copyright).

## 1.1.6 - 2026-03-18
- README expandido com secao dedicada de persistencia NVS apos reboot e update OTA.
- Exemplos `BasicStationOrAP` e `StationListWithSerialConfig` atualizados para o padrao seguro:
  - `loadPreferences()` primeiro;
  - defaults apenas quando NVS estiver vazia;
  - evitar sobrescrever configuracao salva no portal.
- Novo exemplo `PersistenceWithFactoryReset` com comandos:
  - `nvs-status` para diagnostico rapido;
  - `nvs-clear` para limpar NVS e reiniciar.

## 1.1.5 - 2026-03-18
- Novo modo de debug com metricas de AP/captive portal:
  - API publica: `enableDebugMetrics`, `setDebugSummaryIntervalMs`, `printDebugSummary`.
  - comandos serial/Telnet: `debug-on`, `debug-off`, `debug-summary`.
  - resumo periodico automatico com uptime, clientes conectados e delays DHCP.
- Logs verbosos de `Captive probe` agora so aparecem com debug ativo.
- README atualizado com secao dedicada de debug.
- Estrutura da biblioteca alinhada para distribuicao Arduino/PlatformIO (`library.json`, `keywords.txt`, `LICENSE`, workflow de CI).

## 1.1.4 - 2026-03-18
- Fluxo AP no ESP32-P4 reforcado para captive portal: uso de `Network.begin()` + `WiFi.AP.begin/config/create` nas rotas de AP da biblioteca.
- `startConfigPortalOnAccessPoint` no P4 agora segue o mesmo caminho do teste que validou abertura do portal no boot.
- Inicializacao do DNS captive passou a validar retorno de `start(...)` e registrar aviso quando falhar.
- Mantido caminho legado para outros ESP32 (softAP) para preservar compatibilidade.

## 1.1.3 - 2026-03-17
- AP de configuracao com SSID unico por sessao quando o SSID padrao e usado (`SuperOTA-Setup-XXXX`), para reduzir cache/politica de captive portal nos clientes.
- Log do portal em AP agora mostra explicitamente o SSID ativo.

## 1.1.2 - 2026-03-17
- Captive portal em AP passou a responder probes com redirecionamento `302` para `http://<ap-ip>/`.
- Roteamento de host no portal: requisicoes para host externo (por DNS catch-all) redirecionam para o host do portal em vez de servir HTML direto.
- Logs de probe agora incluem `Host + URI` para diagnostico fino (ex.: requests como `/chat`).
- Mantidas as melhorias de foreground loop e DHCP Captive Portal URI da versao 1.1.1.

## 1.1.1 - 2026-03-17
- Melhorias no portal de configuracao em AP para deteccao captive portal.
- `configota` em modo AP agora entra em loop foreground dedicado para atender DNS/HTTP sem depender do `loop()` do usuario.
- Endpoints de captive portal ampliados e tratados com `HTTP_ANY`.
- AP de configuracao agora aguarda IP valido antes de iniciar DNS/WebServer.
- Configuracao explicita de `softAPConfig` (IP/gateway/subnet/DNS) para reduzir falhas de deteccao automatica em clientes.
- Tentativa de habilitar DHCP Captive Portal URI quando o framework suporta (`IDF >= 5.4.2`).
- Ajustes de layout da pagina de configuracao (checkbox inline e rodape com exemplo a esquerda e botao a direita).
- README expandido com secao dedicada de `platformio.ini` para OTA + serial Wi-Fi.
