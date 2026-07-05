#!/bin/bash
# Compilar e enviar apenas CLIENTE

set -e

CLIENT_IP="192.168.100.31"
PROJECT_DIR="~/chocadeira_projeto/eachoca"

cd "$PROJECT_DIR"

echo "🔨 Compilando CLIENTE..."
pio run -e esp32-client

echo ""
echo "📤 Enviando via OTA para $CLIENT_IP..."
curl -F "file=@.pio/build/esp32-client/firmware.bin" http://$CLIENT_IP/update

echo ""
echo "✅ Cliente atualizado!"
echo "🌐 Painel: http://$CLIENT_IP"
