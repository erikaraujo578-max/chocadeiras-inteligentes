#!/bin/bash
# Compilar e enviar apenas SERVIDOR

set -e

SERVER_IP="192.168.100.23"
PROJECT_DIR="~/chocadeira_projeto/eachoca"

cd "$PROJECT_DIR"

echo "🔨 Compilando SERVIDOR..."
pio run -e esp32-server

echo ""
echo "📤 Enviando via OTA para $SERVER_IP..."
curl -F "file=@.pio/build/esp32-server/firmware.bin" http://$SERVER_IP/update

echo ""
echo "✅ Servidor atualizado!"
echo "🌐 Dashboard: http://$SERVER_IP"
