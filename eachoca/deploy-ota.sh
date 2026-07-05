#!/bin/bash
# Deploy Script - EA CHOCA+ v7.0
# Compila e envia firmware via OTA para Servidor e Cliente

set -e

# Configuração
SERVER_IP="192.168.100.23"
CLIENT_IP="192.168.100.31"
PROJECT_DIR="~/chocadeira_projeto/eachoca"

echo ""
echo "╔════════════════════════════════════════════════════════════╗"
echo "║  🚀 EA CHOCA+ v7.0 - Deploy OTA Script                     ║"
echo "║  Servidor: $SERVER_IP                                   ║"
echo "║  Cliente:  $CLIENT_IP                                   ║"
echo "╚════════════════════════════════════════════════════════════╝"
echo ""

cd "$PROJECT_DIR"

# ============================================================
# COMPILAR SERVIDOR
# ============================================================
echo "[1/4] 🔨 Compilando SERVIDOR..."
echo "======================================"
pio run -e esp32-server
echo "✅ Servidor compilado!"
echo ""

# ============================================================
# COMPILAR CLIENTE
# ============================================================
echo "[2/4] 🔨 Compilando CLIENTE..."
echo "======================================"
pio run -e esp32-client
echo "✅ Cliente compilado!"
echo ""

# ============================================================
# ENVIAR SERVIDOR OTA
# ============================================================
echo "[3/4] 📤 Enviando SERVIDOR via OTA ($SERVER_IP)..."
echo "======================================"

SERVER_BIN=".pio/build/esp32-server/firmware.bin"

if [ ! -f "$SERVER_BIN" ]; then
    echo "❌ Erro: Arquivo $SERVER_BIN não encontrado!"
    exit 1
fi

curl -F "file=@$SERVER_BIN" http://$SERVER_IP/update
echo ""
echo "✅ Servidor atualizado!"
echo "   Aguarde ~10 segundos para reiniciar..."
sleep 10
echo ""

# ============================================================
# ENVIAR CLIENTE OTA
# ============================================================
echo "[4/4] 📤 Enviando CLIENTE via OTA ($CLIENT_IP)..."
echo "======================================"

CLIENT_BIN=".pio/build/esp32-client/firmware.bin"

if [ ! -f "$CLIENT_BIN" ]; then
    echo "❌ Erro: Arquivo $CLIENT_BIN não encontrado!"
    exit 1
fi

curl -F "file=@$CLIENT_BIN" http://$CLIENT_IP/update
echo ""
echo "✅ Cliente atualizado!"
echo "   Aguarde ~10 segundos para reiniciar..."
sleep 10
echo ""

# ============================================================
# VERIFICAR CONEXÃO
# ============================================================
echo "[✓] 🔍 Verificando status..."
echo "======================================"

echo -n "Testando Servidor ($SERVER_IP)... "
if curl -s http://$SERVER_IP/api/admin/status > /dev/null; then
    echo "✅ Online"
else
    echo "⚠️  Timeout (verifique manualmente)"
fi

echo -n "Testando Cliente ($CLIENT_IP)... "
if curl -s http://$CLIENT_IP/api/status > /dev/null; then
    echo "✅ Online"
else
    echo "⚠️  Timeout (verifique manualmente)"
fi

echo ""
echo "╔════════════════════════════════════════════════════════════╗"
echo "║  ✅ Deploy OTA Concluído com Sucesso!                     ║"
echo "║                                                            ║"
echo "║  🌐 Dashboard Server: http://$SERVER_IP       ║"
echo "║  🌐 Painel Cliente:   http://$CLIENT_IP       ║"
echo "╚════════════════════════════════════════════════════════════╝"
echo ""
