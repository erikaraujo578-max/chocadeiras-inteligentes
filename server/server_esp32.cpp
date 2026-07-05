/*
 * ╔════════════════════════════════════════════════════════════╗
 * ║     EA CHOCA+ SERVER v7.0 — Hub Central na Nuvem           ║
 * ║     WebSocket Hub + Multi-Tenant + Admin Panel              ║
 * ║     1 ESP32 central gerencia N clientes remotos             ║
 * ╚════════════════════════════════════════════════════════════╝
 *
 * FUNCIONALIDADES:
 *   - WebSocket Hub para conectar múltiplos clientes
 *   - Dashboard Admin com monitoramento em tempo real
 *   - API REST para registro e controle de devices
 *   - Persistência em NVS
 *   - Distribuição de comandos aos clientes
 *
 * CONFIGURAÇÃO:
 *   - IP Público do servidor (atribuído pelo roteador/cloud)
 *   - Porta WebSocket: 3000
 *   - Porta HTTP: 80
 */

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiManager.h>
#include <WebServer.h>
#include <ArduinoWebsockets.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <time.h>
#include <map>
#include <vector>

using namespace websockets;

// ══════════════════════════════════════════════════════════
//  VERSÃO E CONFIGURAÇÃO
// ══════════════════════════════════════════════════════════
#define SERVER_VERSION    "7.0"
#define SERVER_NAME       "Aurora Cloud Hub"
#define WS_PORT           3000
#define HTTP_PORT         80
#define MAX_DEVICES       50
#define DEVICE_TIMEOUT    60000  // 60s sem status = offline

// ══════════════════════════════════════════════════════════
//  ESTRUTURA: DISPOSITIVO CONECTADO
// ══════════════════════════════════════════════════════════
struct Device {
    String deviceId;
    String apiKey;
    bool   conectado = false;
    uint32_t wsClientId = 0;
    unsigned long tUltimoStatus = 0;
    
    // Estado da Chocadeira
    float temperatura = NAN;
    float umidade = NAN;
    float setT = 37.8f;
    float setH = 60.0f;
    int   diaAtual = 0;
    int   diasTotal = 21;
    String especie = "Galinha";
    bool  incubacaoAtiva = false;
    bool  releCalor = false;
    bool  releUmid = false;
    bool  servoAtivo = false;
    int   modoFull = 0;
    String fwVersion = "7.0";
    String ip = "";
    int rssi = 0;
};

// ══════════════════════════════════════════════════════════
//  OBJETOS GLOBAIS
// ══════════════════════════════════════════════════════════
WebServer webServer(HTTP_PORT);
WebsocketsServer wsServer;
Preferences prefs;

Device devices[MAX_DEVICES];
int deviceCount = 0;
std::map<String, uint32_t> deviceConnections;  // deviceId -> wsClientId

struct {
    String adminUser = "admin";
    String adminPass = "senha123";
} serverConfig;

// ══════════════════════════════════════════════════════════
//  UTILITY
// ══════════════════════════════════════════════════════════
String gerarApiKey() {
    char buf[33];
    sprintf(buf, "%08X%08X%08X%08X",
        esp_random(), esp_random(), esp_random(), esp_random());
    return String(buf);
}

Device* encontrarDevice(const String& id) {
    for (int i = 0; i < deviceCount; i++)
        if (devices[i].deviceId == id) return &devices[i];
    return nullptr;
}

bool verificarAuth(WebServer &server) {
    if (!server.hasHeader("Authorization")) return false;
    String auth = server.header("Authorization");
    return auth == ("Bearer " + serverConfig.adminPass);
}

// ══════════════════════════════════════════════════════════
//  NVS: Persistência de Devices
// ══════════════════════════════════════════════════════════
void salvarDevicesNVS() {
    prefs.begin("choca_server", false);
    prefs.putUInt("deviceCount", deviceCount);
    for (int i = 0; i < deviceCount; i++) {
        String key = "dev_" + String(i);
        DynamicJsonDocument doc(256);
        doc["id"] = devices[i].deviceId;
        doc["key"] = devices[i].apiKey;
        String json;
        serializeJson(doc, json);
        prefs.putString(key.c_str(), json);
    }
    prefs.end();
}

void carregarDevicesNVS() {
    prefs.begin("choca_server", true);
    deviceCount = prefs.getUInt("deviceCount", 0);
    for (int i = 0; i < deviceCount && i < MAX_DEVICES; i++) {
        String key = "dev_" + String(i);
        String json = prefs.getString(key.c_str(), "");
        if (json.length() > 0) {
            DynamicJsonDocument doc(256);
            deserializeJson(doc, json);
            devices[i].deviceId = doc["id"].as<String>();
            devices[i].apiKey = doc["key"].as<String>();
        }
    }
    prefs.end();
}

// ══════════════════════════════════════════════════════════
//  WEBSOCKET SERVER: Receber dados dos clientes
// ══════════════════════════════════════════════════════════
void onWsMessage(uint32_t clientId, WebsocketsMessage msg) {
    DynamicJsonDocument doc(2048);
    if (deserializeJson(doc, msg.data())) {
        Serial.println("[WS] Erro ao desserializar JSON");
        return;
    }

    String tipo = doc["tipo"] | "";
    String deviceId = doc["deviceId"] | "";
    Device* dev = encontrarDevice(deviceId);

    if (tipo == "handshake") {
        String apiKey = doc["apiKey"] | "";
        if (dev && dev->apiKey == apiKey) {
            dev->conectado = true;
            dev->wsClientId = clientId;
            dev->fwVersion = doc["firmware"] | "?";            
            deviceConnections[deviceId] = clientId;
            
            Serial.println("[WS] ✅ Device " + deviceId + " autenticado! FW: " + dev->fwVersion);
            
            // Confirmação ao cliente
            DynamicJsonDocument resp(256);
            resp["tipo"] = "handshake_ok";
            resp["serverName"] = SERVER_NAME;
            resp["serverVersion"] = SERVER_VERSION;
            String s; serializeJson(resp, s);
            wsServer.send(clientId, s);
        } else {
            Serial.println("[WS] ❌ Autenticação falhou para " + deviceId);
            wsServer.close(clientId);
        }
    }
    else if (tipo == "status" && dev) {
        // Atualiza estado do device
        dev->temperatura = doc["temperatura"] | NAN;
        dev->umidade = doc["umidade"] | NAN;
        dev->setT = doc["setT"] | 37.8f;
        dev->setH = doc["setH"] | 60.0f;
        dev->diaAtual = doc["diaAtual"] | 0;
        dev->diasTotal = doc["diasTotal"] | 21;
        dev->especie = doc["especie"] | "Galinha";
        dev->incubacaoAtiva = doc["incubacaoAtiva"] | false;
        dev->releCalor = doc["releCalor"] | false;
        dev->releUmid = doc["releUmid"] | false;
        dev->servoAtivo = doc["servoAtivo"] | false;
        dev->modoFull = doc["modoFull"] | 0;
        dev->ip = doc["ip"] | "";
        dev->rssi = doc["rssi"] | 0;
        dev->tUltimoStatus = millis();
        dev->conectado = true;
        
        Serial.printf("[STATUS] %s: T=%.1f°C H=%.0f%% D%d/%d IP:%s RSSI:%d\n",
            deviceId.c_str(), dev->temperatura, dev->umidade,
            dev->diaAtual, dev->diasTotal, dev->ip.c_str(), dev->rssi);
    }
}

void onWsEvent(uint32_t clientId, WebsocketsEvent event, String data) {
    if (event == WebsocketsEvent::ConnectionOpened) {
        Serial.println("[WS] Novo cliente WebSocket conectado: #" + String(clientId));
    }
    else if (event == WebsocketsEvent::ConnectionClosed) {
        Serial.println("[WS] Cliente desconectado: #" + String(clientId));
        // Marca device como offline
        for (auto it = deviceConnections.begin(); it != deviceConnections.end(); ++it) {
            if (it->second == clientId) {
                Device* dev = encontrarDevice(it->first);
                if (dev) {
                    dev->conectado = false;
                    Serial.println("[WS] Device " + it->first + " marcado como OFFLINE");
                }
                deviceConnections.erase(it);
                break;
            }
        }
    }
}

// ══════════════════════════════════════════════════════════
//  API REST: ADMIN
// ══════════════════════════════════════════════════════════

// Listar todos os devices
void apiListDevices() {
    DynamicJsonDocument doc(8192);
    JsonArray arr = doc.createNestedArray("devices");
    
    for (int i = 0; i < deviceCount; i++) {
        JsonObject o = arr.createNestedObject();
        o["deviceId"] = devices[i].deviceId;
        o["conectado"] = devices[i].conectado;
        o["temperatura"] = isnan(devices[i].temperatura) ? 0 : devices[i].temperatura;
        o["umidade"] = isnan(devices[i].umidade) ? 0 : devices[i].umidade;
        o["setT"] = devices[i].setT;
        o["setH"] = devices[i].setH;
        o["especie"] = devices[i].especie;
        o["diaAtual"] = devices[i].diaAtual;
        o["diasTotal"] = devices[i].diasTotal;
        o["incubacaoAtiva"] = devices[i].incubacaoAtiva;
        o["releCalor"] = devices[i].releCalor;
        o["releUmid"] = devices[i].releUmid;
        o["servoAtivo"] = devices[i].servoAtivo;
        o["modoFull"] = devices[i].modoFull;
        o["fwVersion"] = devices[i].fwVersion;
        o["ip"] = devices[i].ip;
        o["rssi"] = devices[i].rssi;
        o["tUltimoStatus"] = (unsigned long)(devices[i].tUltimoStatus / 1000);
    }
    
    String s; serializeJson(doc, s);
    webServer.send(200, "application/json", s);
}

// Status do servidor
void apiServerStatus() {
    DynamicJsonDocument doc(512);
    doc["serverName"] = SERVER_NAME;
    doc["version"] = SERVER_VERSION;
    doc["uptime"] = millis() / 1000;
    doc["totalDevices"] = deviceCount;
    doc["connectedDevices"] = 0;
    
    for (int i = 0; i < deviceCount; i++) {
        if (devices[i].conectado) {
            doc["connectedDevices"] = doc["connectedDevices"].as<int>() + 1;
        }
    }
    
    doc["wifiSSID"] = WiFi.SSID();
    doc["wifiIP"] = WiFi.localIP().toString();
    doc["freeMemory"] = ESP.getFreeHeap();
    doc["wsPort"] = WS_PORT;
    
    String s; serializeJson(doc, s);
    webServer.send(200, "application/json", s);
}

// Registrar novo device
void apiRegisterDevice() {
    DynamicJsonDocument doc(256);
    deserializeJson(doc, webServer.arg("plain"));
    
    String deviceId = doc["deviceId"] | "";
    if (deviceId.length() < 3) {
        webServer.send(400, "application/json", "{\"ok\":false,\"msg\":\"ID inválido\"}");
        return;
    }
    
    if (encontrarDevice(deviceId)) {
        webServer.send(409, "application/json", "{\"ok\":false,\"msg\":\"Device já registrado\"}");
        return;
    }
    
    if (deviceCount >= MAX_DEVICES) {
        webServer.send(400, "application/json", "{\"ok\":false,\"msg\":\"Limite de devices atingido\"}");
        return;
    }
    
    devices[deviceCount].deviceId = deviceId;
    devices[deviceCount].apiKey = gerarApiKey();
    deviceCount++;
    salvarDevicesNVS();
    
    DynamicJsonDocument resp(256);
    resp["ok"] = true;
    resp["apiKey"] = devices[deviceCount-1].apiKey;
    resp["msg"] = "Device registrado com sucesso";
    
    String s; serializeJson(resp, s);
    webServer.send(200, "application/json", s);
    
    Serial.println("[API] ✅ Novo device registrado: " + deviceId);
}

// Enviar comando a um device
void apiSendCommand() {
    DynamicJsonDocument doc(512);
    deserializeJson(doc, webServer.arg("plain"));
    
    String deviceId = doc["deviceId"] | "";
    String comando = doc["comando"] | "";
    Device* dev = encontrarDevice(deviceId);
    
    if (!dev) {
        webServer.send(404, "application/json", "{\"ok\":false,\"msg\":\"Device não encontrado\"}");
        return;
    }
    
    if (!dev->conectado) {
        webServer.send(503, "application/json", "{\"ok\":false,\"msg\":\"Device offline\"}");
        return;
    }
    
    auto it = deviceConnections.find(deviceId);
    if (it == deviceConnections.end()) {
        webServer.send(503, "application/json", "{\"ok\":false,\"msg\":\"Sem conexão WS\"}");
        return;
    }
    
    // Reenvia comando ao device
    DynamicJsonDocument cmdDoc(512);
    cmdDoc["comando"] = comando;
    if (doc.containsKey("valor")) cmdDoc["valor"] = doc["valor"];
    
    String s; serializeJson(cmdDoc, s);
    wsServer.send(it->second, s);
    
    Serial.println("[API] Comando enviado: " + deviceId + " -> " + comando);
    webServer.send(200, "application/json", "{\"ok\":true,\"msg\":\"Comando enviado\"}");
}

// Obter detalhes de um device
void apiGetDevice() {
    String deviceId = webServer.arg("id");
    Device* dev = encontrarDevice(deviceId);
    
    if (!dev) {
        webServer.send(404, "application/json", "{\"ok\":false,\"msg\":\"Não encontrado\"}");
        return;
    }
    
    DynamicJsonDocument doc(1024);
    doc["deviceId"] = dev->deviceId;
    doc["conectado"] = dev->conectado;
    doc["temperatura"] = isnan(dev->temperatura) ? 0 : dev->temperatura;
    doc["umidade"] = isnan(dev->umidade) ? 0 : dev->umidade;
    doc["setT"] = dev->setT;
    doc["setH"] = dev->setH;
    doc["especie"] = dev->especie;
    doc["diaAtual"] = dev->diaAtual;
    doc["diasTotal"] = dev->diasTotal;
    doc["incubacaoAtiva"] = dev->incubacaoAtiva;
    doc["fwVersion"] = dev->fwVersion;
    doc["ip"] = dev->ip;
    doc["rssi"] = dev->rssi;
    
    String s; serializeJson(doc, s);
    webServer.send(200, "application/json", s);
}

// ══════════════════════════════════════════════════════════
//  SETUP WEB SERVER
// ══════════════════════════════════════════════════════════
const char HTML_ADMIN[] PROGMEM = R"RAWHTML(<!DOCTYPE html>
<html lang="pt-BR">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>EA CHOCA+ Admin Hub v7.0</title>
<style>
* { margin:0; padding:0; box-sizing:border-box; }
body { 
  font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', sans-serif;
  background: linear-gradient(135deg, #0a0d14 0%, #1a1f2e 100%);
  color: #f1f5f9; 
  padding: 20px;
  min-height: 100vh;
}
.container { max-width: 1400px; margin: 0 auto; }
.header {
  display: flex;
  justify-content: space-between;
  align-items: center;
  margin-bottom: 30px;
  padding: 20px;
  background: rgba(245,158,11,.1);
  border: 1px solid rgba(245,158,11,.2);
  border-radius: 12px;
}
h1 { color: #f59e0b; font-size: 2em; }
.stats {
  display: grid;
  grid-template-columns: repeat(auto-fit, minmax(220px, 1fr));
  gap: 15px;
  margin-bottom: 30px;
}
.stat-card {
  background: rgba(17,24,39,.8);
  border: 1px solid rgba(255,255,255,.07);
  border-radius: 12px;
  padding: 20px;
  backdrop-filter: blur(10px);
}
.stat-label { font-size: 0.85em; color: #64748b; margin-bottom: 10px; text-transform: uppercase; }
.stat-value { font-size: 2.5em; font-weight: 900; color: #f59e0b; }
.stat-sub { font-size: 0.75em; color: #64748b; margin-top: 8px; }
.devices-grid {
  display: grid;
  grid-template-columns: repeat(auto-fill, minmax(320px, 1fr));
  gap: 15px;
  margin-top: 20px;
}
.device-card {
  background: rgba(17,24,39,.9);
  border: 1px solid rgba(255,255,255,.07);
  border-radius: 12px;
  padding: 18px;
  transition: all 0.3s;
  backdrop-filter: blur(10px);
}
.device-card:hover {
  border-color: rgba(245,158,11,.3);
  box-shadow: 0 0 20px rgba(245,158,11,.1);
}
.device-header {
  display: flex;
  justify-content: space-between;
  align-items: center;
  margin-bottom: 14px;
  padding-bottom: 12px;
  border-bottom: 1px solid rgba(255,255,255,.07);
}
.device-id { font-weight: 700; font-size: 0.95em; }
.device-status {
  display: inline-flex;
  align-items: center;
  gap: 5px;
  padding: 4px 12px;
  border-radius: 20px;
  font-size: 0.75em;
  font-weight: 600;
}
.status-on { background: rgba(16,185,129,.2); color: #10b981; }
.status-off { background: rgba(239,68,68,.2); color: #ef4444; }
.device-info {
  font-size: 0.82em;
  color: #cbd5e1;
  line-height: 1.8;
}
.device-info strong { color: #f1f5f9; font-weight: 600; }
.device-info .label { color: #64748b; display: inline-block; min-width: 80px; }
.status-dot {
  width: 8px;
  height: 8px;
  border-radius: 50%;
  display: inline-block;
  margin-right: 5px;
}
.dot-on { background: #10b981; box-shadow: 0 0 8px #10b981; }
.dot-off { background: #64748b; }
.incubacao-badge {
  display: inline-block;
  margin-top: 10px;
  padding: 6px 12px;
  background: rgba(245,158,11,.15);
  border: 1px solid rgba(245,158,11,.25);
  border-radius: 6px;
  font-size: 0.75em;
  color: #f59e0b;
  font-weight: 600;
}
.no-devices {
  grid-column: 1 / -1;
  text-align: center;
  padding: 60px 20px;
  color: #64748b;
}
.no-devices-icon { font-size: 3em; margin-bottom: 10px; }
</style>
</head>
<body>
<div class="container">
  <div class="header">
    <div>
      <h1>🐣 EA CHOCA+ Admin Hub</h1>
      <p style="font-size:0.85em; color:#64748b; margin-top:5px;">v7.0 Aurora Cloud</p>
    </div>
    <div style="text-align:right;">
      <p style="font-size:0.9em; margin-bottom:5px;"><span id="time"></span></p>
      <p style="font-size:0.8em; color:#64748b;" id="uptime">Uptime: --</p>
    </div>
  </div>
  
  <div class="stats">
    <div class="stat-card">
      <div class="stat-label">🌐 Servidor</div>
      <div class="stat-value" id="srvName" style="font-size:1.2em;">--</div>
      <div class="stat-sub">IP: <span id="srvIP">--</span></div>
    </div>
    <div class="stat-card">
      <div class="stat-label">📱 Total de Devices</div>
      <div class="stat-value" id="totalDevices">0</div>
    </div>
    <div class="stat-card">
      <div class="stat-label">🟢 Online Agora</div>
      <div class="stat-value" id="connectedDevices">0</div>
    </div>
    <div class="stat-card">
      <div class="stat-label">💾 Memória Livre</div>
      <div class="stat-value" style="font-size:1.5em;" id="freeMemory">--</div>
    </div>
  </div>
  
  <h2 style="margin: 30px 0 20px; font-size:1.3em;">📊 Chocadeiras Conectadas</h2>
  <div class="devices-grid" id="devicesGrid">
    <div class="no-devices">
      <div class="no-devices-icon">⏳</div>
      <p>Carregando devices...</p>
    </div>
  </div>
</div>

<script>
let refreshInterval;

async function refresh() {
  try {
    const status = await fetch('/api/admin/status').then(r => r.json()).catch(() => null);
    const devs = await fetch('/api/admin/devices').then(r => r.json()).catch(() => ({devices:[]}));
    
    if (!status) return;
    
    // Atualizar stats
    document.getElementById('srvName').textContent = status.serverName || 'Aurora';
    document.getElementById('srvIP').textContent = status.wifiIP || '--';
    document.getElementById('totalDevices').textContent = status.totalDevices || 0;
    document.getElementById('connectedDevices').textContent = status.connectedDevices || 0;
    document.getElementById('freeMemory').textContent = Math.round(status.freeMemory / 1024) + ' KB';
    
    const upH = Math.floor(status.uptime / 3600);
    const upM = Math.floor((status.uptime % 3600) / 60);
    document.getElementById('uptime').textContent = `Uptime: ${upH}h ${upM}m`;
    
    // Atualizar hora
    document.getElementById('time').textContent = new Date().toLocaleTimeString('pt-BR');
    
    // Renderizar devices
    const grid = document.getElementById('devicesGrid');
    if (!devs.devices || devs.devices.length === 0) {
      grid.innerHTML = `<div class="no-devices">
        <div class="no-devices-icon">🚫</div>
        <p>Nenhum device registrado ainda</p>
      </div>`;
      return;
    }
    
    grid.innerHTML = devs.devices.map(d => {
      const isOn = d.conectado;
      const lastUpdate = new Date(d.tUltimoStatus * 1000).toLocaleTimeString('pt-BR');
      
      return `<div class="device-card">
        <div class="device-header">
          <span class="device-id">${d.deviceId}</span>
          <span class="device-status ${isOn ? 'status-on' : 'status-off'}">
            <span class="status-dot ${isOn ? 'dot-on' : 'dot-off'}"></span>
            ${isOn ? '🟢 Online' : '🔴 Offline'}
          </span>
        </div>
        <div class="device-info">
          <div><span class="label">Espécie:</span> <strong>${d.especie}</strong></div>
          <div><span class="label">Temp:</span> <strong>${d.temperatura.toFixed(1)}°C</strong> (alvo ${d.setT}°C)</div>
          <div><span class="label">Umidade:</span> <strong>${d.umidade.toFixed(0)}%</strong> (alvo ${d.setH}%)</div>
          <div><span class="label">Dia:</span> <strong>${d.diaAtual} / ${d.diasTotal}</strong></div>
          <div><span class="label">Status:</span> <strong>${d.incubacaoAtiva ? '🔥 Incubando' : '⏸️ Parado'}</strong></div>
          <div><span class="label">Relés:</span> <strong>🔥${d.releCalor?'ON':'OFF'} 💧${d.releUmid?'ON':'OFF'} 🔄${d.servoAtivo?'MOVE':'OK'}</strong></div>
          <div><span class="label">FW:</span> <strong>${d.fwVersion}</strong> | RSSI: <strong>${d.rssi} dBm</strong></div>
          <div><span class="label">IP:</span> <strong>${d.ip || '--'}</strong></div>
          <div style="font-size:0.7em; color:#64748b; margin-top:8px;">Último status: ${lastUpdate}</div>
          ${d.incubacaoAtiva ? `<div class="incubacao-badge">🌱 Dia ${d.diaAtual} de Incubação</div>` : ''}
        </div>
      </div>`;
    }).join('');
    
  } catch(e) {
    console.error('Erro ao atualizar:', e);
  }
}

refresh();
refreshInterval = setInterval(refresh, 3000);
</script>
</body>
</html>
)RAWHTML";

void setupWebServer() {
    // Dashboard Admin (GET /)
    webServer.on("/", HTTP_GET, []() {
        webServer.send_P(200, "text/html", HTML_ADMIN);
    });
    
    // API: Listar devices
    webServer.on("/api/admin/devices", HTTP_GET, apiListDevices);
    
    // API: Status servidor
    webServer.on("/api/admin/status", HTTP_GET, apiServerStatus);
    
    // API: Registrar device
    webServer.on("/api/device/register", HTTP_POST, apiRegisterDevice);
    
    // API: Enviar comando
    webServer.on("/api/device/command", HTTP_POST, apiSendCommand);
    
    // API: Obter detalhes device
    webServer.on("/api/device/get", HTTP_GET, apiGetDevice);
    
    webServer.begin();
    Serial.println("[WEB] Servidor HTTP iniciado na porta " + String(HTTP_PORT));
}

// ══════════════════════════════════════════════════════════
//  MONITORAMENTO DE DEVICES OFFLINE
// ══════════════════════════════════════════════════════════
void verificarDevicesOffline() {
    unsigned long agora = millis();
    for (int i = 0; i < deviceCount; i++) {
        if (devices[i].conectado && (agora - devices[i].tUltimoStatus) > DEVICE_TIMEOUT) {
            devices[i].conectado = false;
            Serial.println("[MON] ⚠️ Device " + devices[i].deviceId + " marcado como TIMEOUT");
        }
    }
}

// ══════════════════════════════════════════════════════════
//  SETUP
// ══════════════════════════════════════════════════════════
void setup() {
    Serial.begin(115200);
    Serial.println("\n\n╔════════════════════════════════════════╗");
    Serial.println("║  🌐 EA CHOCA+ SERVER v7.0 Aurora      ║");
    Serial.println("║     Hub Central na Nuvem               ║");
    Serial.println("╚════════════════════════════════════════╝\n");
    
    // Carrega devices salvos
    carregarDevicesNVS();
    Serial.println("[NVS] Carregados " + String(deviceCount) + " devices da memória\n");
    
    // WiFi
    WiFi.mode(WIFI_STA);
    WiFiManager wm;
    wm.setConfigPortalTimeout(180);
    bool conectou = wm.autoConnect("EA-CHOCA-SERVER", "senha123");
    
    if (!conectou) {
        Serial.println("[WiFi] ❌ Falha na conexão!");
        while(1) delay(1000);
    }
    
    while (WiFi.status() != WL_CONNECTED) delay(500);
    Serial.println("[WiFi] ✅ Conectado: " + WiFi.localIP().toString());
    
    // NTP
    configTime(-3 * 3600, 0, "pool.ntp.org");
    
    // Setup Web
    setupWebServer();
    Serial.println("[HTTP] Dashboard admin em: http://" + WiFi.localIP().toString());
    
    // Setup WebSocket
    wsServer.onMessage(onWsMessage);
    wsServer.onEvent(onWsEvent);
    bool wsOk = wsServer.listen(WS_PORT);
    
    if (wsOk) {
        Serial.println("[WS] ✅ WebSocket escutando na porta " + String(WS_PORT));
        Serial.println("     Conectar clientes em: ws://" + WiFi.localIP().toString() + ":" + String(WS_PORT));
    } else {
        Serial.println("[WS] ❌ Erro ao iniciar WebSocket!");
    }
    
    Serial.println("\n[OK] 🌐 Servidor pronto! Aguardando clientes...\n");
}

// ══════════════════════════════════════════════════════════
//  LOOP
// ══════════════════════════════════════════════════════════
static unsigned long tMonitor = 0;

void loop() {
    webServer.handleClient();
    wsServer.poll();
    
    // Verificar devices offline
    if (millis() - tMonitor > 5000) {
        tMonitor = millis();
        verificarDevicesOffline();
    }
    
    delay(10);
}
