# Changelog

## Unreleased
- Portal/configuracao:
  - `startConfigPortalOnStation()` promovido para API publica.
  - `startConfigPortal()` sem argumentos passa a reutilizar o AP operacional configurado, em vez de criar SSID temporario de setup por padrao.
  - reuso do AP atual quando o dispositivo ja esta em modo AP.
- ESP32-P4:
  - salvar apenas mudancas de seguranca no portal agora reaplica sem reboot;
  - reinicio seguro fica restrito a mudancas de topologia (`hostname`, preferencia AP, credenciais AP e lista station).
  - logs mais claros no fluxo diferido de aplicacao: sem reboot vs reinicio seguro.
- Persistencia/NVS:
  - exemplos oficiais atualizados para nao gravarem defaults automaticamente quando `loadPreferences()` falhar.
- Exemplos:
  - `FreeRTOSOtaServiceQueue` refeito para espelhar o fluxo oficial do `configota` com escolha `1 = station` e `2 = AP`.
  - novo exemplo `GuidedSetupP4` com README proprio.
- Documentacao:
  - README principal reorganizado como guia de configuracao e operacao.

## 1.2.1 - 2026-03-21
- Portal de configuracao:
  - refinamento visual dos icones de senha (olho/olho riscado) com estilo mais minimalista;
  - melhor alinhamento do icone de visualizacao dentro dos campos de senha;
  - carimbo visual no rodape com versao da biblioteca + revisao de UI + data/hora de build, para facilitar validacao de cache.
- Documentacao:
  - README atualizado com orientacao para forcar refresh da biblioteca local no PlatformIO (`pkg install --force`) quando necessario.

## 1.2.0 - 2026-03-20
- Seguranca OTA + portal:
  - nova API: `setOtaPassword`, `otaPasswordEnabled`.
  - nova API: `setPortalPassword`, `setUseOtaPasswordForPortal`, `usingOtaPasswordForPortal`, `portalPasswordEnabled`.
  - `configureOTAHandlers()` agora aplica senha OTA quando configurada.
  - portal de configuracao agora pode exigir autenticacao HTTP Basic (`admin` + senha configurada).
  - protecao aplicada em `/`, `/save`, `/scan` e fluxo principal do portal.
- Portal web:
  - novos campos para definir senha OTA, definir senha do portal e escolher reutilizar a senha OTA no portal.
  - campos de senha vazios mantem os valores ja configurados.
- Persistencia NVS:
  - novas chaves `otaPass`, `portalPass`, `portalUseOta`.
- README atualizado com secao de seguranca e API nova.
- Novo exemplo `SecurityOtaAndPortalPassword` para validar senha OTA e autenticacao do portal.

## 1.1.9 - 2026-03-18
- Correcao de estabilidade no fechamento do portal:
  - `handleConfigSave()` agora agenda o fechamento do portal para o proximo ciclo de `loop()`, em vez de chamar `stopConfigPortal(true)` dentro do callback HTTP.
  - evita race/use-after-free ao destruir `WebServer` durante o proprio processamento de request.
- Ajuste importante para cenarios P4+C6 com transicao AP -> STA apos salvar configuracoes.

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
