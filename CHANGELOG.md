# Changelog

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
