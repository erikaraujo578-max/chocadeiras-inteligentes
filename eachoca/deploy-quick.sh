#!/bin/bash
# Quick Deploy - Apenas compila e envia

set -e

SERVER_IP="192.168.100.23"
CLIENT_IP="192.168.100.31"
PROJECT_DIR="~/chocadeira_projeto/eachoca"

cd "$PROJECT_DIR"

echo "🔨 Compilando Servidor..."
pio run -e esp32-server -q

echo "📤 Enviando Servidor OTA..."
curl -s -F "file=@.pio/build/esp32-server/firmware.bin" http://$SERVER_IP/update
echo " ✅"

echo "🔨 Compilando Cliente..."
pio run -e esp32-client -q

echo "📤 Enviando Cliente OTA..."
curl -s -F "file=@.pio/build/esp32-client/firmware.bin" http://$CLIENT_IP/update
echo " ✅"

echo "✅ Deploy concluído!"
