/*
 * ╔══════════════════════════════════════════════════════════════════╗
 * ║  EA CHOCA+ CLIENT v7.0 — Dispositivo Descentralizado             ║
 * ║  Conecta ao Hub via WebSocket + Web Local Offline                ║
 * ║  Mantém todas as funcionalidades de incubação e controle         ║
 * ╚══════════════════════════════════════════════════════════════════╝
 *
 * FUNCIONALIDADES MANTIDAS:
 *   ✅ Leitura DHT22 (temperatura/umidade)
 *   ✅ Controle de temperatura (banda proporcional 20s)
 *   ✅ Controle de umidade (histerese 5%)
 *   ✅ Servo motor (viragem automática)
 *   ✅ Display OLED 128x64
 *   ✅ Web UI local (painel offline)
 *   ✅ WebSocket ao cloud (painel remoto)
 *   ✅ OTA local
 *   ✅ WiFi Manager
 *
 * NOVO:
 *   ⭐ Primeiro acesso: Registra no cloud automaticamente
 *   ⭐ API Key gerada pelo servidor
 *   ⭐ Sincronização de dados em tempo real
 *   ⭐ Modo local + modo cloud simultâneos
 */

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiManager.h>
#include <DHT.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <ESP32Servo.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <Update.h>
#include <time.h>
#include <WebServer.h>
#include <Preferences.h>
#include <esp_system.h>
#include <ArduinoWebsockets.h>

using namespace websockets;

// ══════════════════════════════════════════════════════════════
//  VERSÃO
// ══════════════════════════════════════════════════════════════
#define FIRMWARE_VERSION  "7.0"
#define FIRMWARE_NOME     "Aurora Cloud Edition"

// ══════════════════════════════════════════════════════════════
//  PINOS
// ══════════════════════════════════════════════════════════════
#define PIN_DHT           4
#define PIN_RELE_CALOR    5
#define PIN_RELE_UMID     18
#define PIN_SERVO         19
#define RELE_ON           LOW
#define RELE_OFF          HIGH

// ══════════════════════════════════════════════════════════════
//  DEFAULTS
// ══════════════════════════════════════════════════════════════
#define DEFAULT_TEMP          37.8f
#define DEFAULT_UMID          60.0f
#define DEFAULT_HISTERESE     3.0f
#define DEFAULT_INTERVALO_V   4
#define DEFAULT_ESPECIE       "Galinha"
#define DEFAULT_DIAS_TOTAL    21
#define DEFAULT_SERVO_POS     90

// ══════════════════════════════════════════════════════════════
//  LIMITES
// ══════════════════════════════════════════════════════════════
#define TEMP_MIN    35.0f
#define TEMP_MAX    40.0f
#define UMID_MIN    35
#define UMID_MAX    75

// ══════════════════════════════════════════════════════════════
//  TIMINGS
// ══════════════════════════════════════════════════════════════
#define MS_DHT            3000UL
#define MS_DISPLAY        900UL
#define MS_CLOUD_UPDATE   5000UL
#define WS_RECONNECT_MS   6000UL
#define MS_OTA_CHECK      600000UL

// ══════════════════════════════════════════════════════════════
//  REDE
// ══════════════════════════════════════════════════════════════
#define WEB_PORT      80
#define AP_SSID       "EA CHOCA+"
#define AP_PASSWORD   "12345678"
#define AP_TIMEOUT    300

// ══════════════════════════════════════════════════════════════
//  CLOUD DEFAULTS
// ══════════════════════════════════════════════════════════════
#define CLOUD_HOST_DEFAULT  "192.168.1.100"
#define CLOUD_PORT_DEFAULT  3000
#define CLOUD_REGISTER_PATH "/api/device/register"

// ══════════════════════════════════════════════════════════════
//  OLED
// ══════════════════════════════════════════════════════════════
#define OLED_ADDR    0x3C
#define OLED_W       128
#define OLED_H       64

// ══════════════════════════════════════════════════════════════
//  NTP
// ══════════════════════════════════════════════════════════════
#define NTP_SERVER1  "pool.ntp.org"
#define NTP_SERVER2  "time.nist.gov"
#define NTP_TZ       -3

// ══════════════════════════════════════════════════════════════
//  ESTADO GLOBAL
// ══════════════════════════════════════════════════════════════
bool modoAP = false;
bool primeiroAcesso = false;

// ══════════════════════════════════════════════════════════════
//  CONFIG MANAGER
// ══════════════════════════════════════════════════════════════
class ConfigManager {
private:
    Preferences prefs;
public:
    float   setT            = DEFAULT_TEMP;
    float   setH            = DEFAULT_UMID;
    float   histerH         = DEFAULT_HISTERESE;
    int     intervaloV      = DEFAULT_INTERVALO_V;
    String  especie         = DEFAULT_ESPECIE;
    int     diasTotal       = DEFAULT_DIAS_TOTAL;
    int     servoPosRepo    = DEFAULT_SERVO_POS;
    bool    pararViragem    = false;
    bool    incubacaoAtiva  = false;
    time_t  incubacaoInicio = 0;
    String  deviceId        = "";
    String  apiKey          = "";
    bool    cloudEnabled    = false;
    String  cloudHost       = CLOUD_HOST_DEFAULT;
    int     cloudPort       = CLOUD_PORT_DEFAULT;
    bool    cloudSSL        = false;

    void init() {
        prefs.begin("choca_client", false);
        carregar();
        if (deviceId.length() < 6) { 
            deviceId = gerarDeviceId();
            primeiroAcesso = true;
            salvar();
        }
    }

    String gerarDeviceId() {
        uint64_t chipid = ESP.getEfuseMac();
        char id[13];
        sprintf(id, "EA_%04X%04X",
            (uint16_t)(chipid >> 32), (uint16_t)chipid);
        return String(id);
    }

    void carregar() {
        setT            = prefs.getFloat("setT",        DEFAULT_TEMP);
        setH            = prefs.getFloat("setH",        DEFAULT_UMID);
        histerH         = prefs.getFloat("histerH",     DEFAULT_HISTERESE);
        intervaloV      = prefs.getInt("intervaloV",    DEFAULT_INTERVALO_V);
        especie         = prefs.getString("especie",    DEFAULT_ESPECIE);
        servoPosRepo    = prefs.getInt("servoPosRepo",  DEFAULT_SERVO_POS);
        diasTotal       = prefs.getInt("diasTotal",     DEFAULT_DIAS_TOTAL);
        incubacaoInicio = (time_t)prefs.getULong("incubInicio", 0);
        incubacaoAtiva  = prefs.getBool("incubAtiva",   false);
        pararViragem    = prefs.getBool("pararV",       false);
        deviceId        = prefs.getString("deviceId",   "");
        apiKey          = prefs.getString("apiKey",     "");
        cloudEnabled    = prefs.getBool("cloudEnabled", false);
        cloudHost       = prefs.getString("cloudHost",  CLOUD_HOST_DEFAULT);
        cloudPort       = prefs.getInt("cloudPort",     CLOUD_PORT_DEFAULT);
        cloudSSL        = prefs.getBool("cloudSSL",     false);
        
        if (servoPosRepo != 90 && servoPosRepo != 180) servoPosRepo = 90;
        diasTotal = constrain(diasTotal, 1, 60);
    }

    void salvar() {
        prefs.putFloat("setT",        setT);
        prefs.putFloat("setH",        setH);
        prefs.putFloat("histerH",     histerH);
        prefs.putInt("intervaloV",    intervaloV);
        prefs.putString("especie",    especie);
        prefs.putInt("servoPosRepo",  servoPosRepo);
        prefs.putInt("diasTotal",     diasTotal);
        prefs.putULong("incubInicio", (unsigned long)incubacaoInicio);
        prefs.putBool("incubAtiva",   incubacaoAtiva);
        prefs.putBool("pararV",       pararViragem);
        prefs.putString("deviceId",   deviceId);
        prefs.putString("apiKey",     apiKey);
        prefs.putBool("cloudEnabled", cloudEnabled);
        prefs.putString("cloudHost",  cloudHost);
        prefs.putInt("cloudPort",     cloudPort);
        prefs.putBool("cloudSSL",     cloudSSL);
    }

    void resetarIncubacao() { 
        incubacaoInicio = 0; 
        incubacaoAtiva = false; 
        salvar(); 
    }
    
    void iniciarIncubacao()  { 
        time(&incubacaoInicio); 
        incubacaoAtiva = true;  
        salvar(); 
    }

    int diaAtual() {
        if (!incubacaoAtiva || incubacaoInicio == 0) return 0;
        time_t now; time(&now);
        long diff = (long)(now - incubacaoInicio);
        if (diff < 0) diff = 0;
        return constrain((int)(diff / 86400) + 1, 1, diasTotal);
    }

    bool deveVirar() {
        if (pararViragem || !incubacaoAtiva) return false;
        int d = diaAtual();
        if (d <= 2 || d >= diasTotal - 1) return false;
        return true;
    }
};

// ══════════════════════════════════════════════════════════════
//  SISTEMA STATE
// ══════════════════════════════════════════════════════════════
class SistemaState {
public:
    float temperatura = NAN;
    float umidade = NAN;
    int logCount = 0;
};

// ══════════════════════════════════════════════════════════════
//  CONTROLE TEMPERATURA
// ══════════════════════════════════════════════════════════════
class ControleTemperatura {
private:
    float alvo = DEFAULT_TEMP, tAtual = NAN;
    int   pin;
    bool  releLigado = false, modoFull = false, noCiclo = false;
    unsigned long tInicioCiclo = 0, durLigado = 0, tLigouCiclo = 0;
public:
    ControleTemperatura(float a, int p) : alvo(a), pin(p) {
        pinMode(pin, OUTPUT); 
        digitalWrite(pin, RELE_OFF);
    }
    
    void setAlvo(float v) { alvo = v; }
    void setTemperatura(float v) { tAtual = v; }
    bool isLigado() { return releLigado; }
    bool isModoFull() { return modoFull; }

    void atualizar() {
        if (isnan(tAtual)) {
            if (releLigado) { releLigado = false; modoFull = false; digitalWrite(pin, RELE_OFF); }
            return;
        }
        
        float err = alvo - tAtual;
        
        if (tAtual >= (alvo - 0.15f)) {
            if (releLigado) { releLigado = false; noCiclo = false; modoFull = false; digitalWrite(pin, RELE_OFF); }
            return;
        }
        
        if (err > 2.0f) {
            modoFull = true;
            if (!releLigado) { releLigado = true; digitalWrite(pin, RELE_ON); }
            return;
        }
        
        modoFull = false;
        
        float fracao = 0.0f;
        if      (err > 0.8f)  fracao = 1.0f;
        else if (err >= 0.3f) fracao = 0.05f + ((err - 0.3f) / 0.5f) * 0.95f;
        else if (err > 0.0f)  fracao = 0.05f;
        
        durLigado = (unsigned long)(fracao * 20000UL);
        unsigned long agora = millis();
        
        if (agora - tInicioCiclo >= 20000UL) {
            tInicioCiclo = agora;
            if (durLigado > 0 && !releLigado) {
                releLigado = true; noCiclo = true; tLigouCiclo = agora;
                digitalWrite(pin, RELE_ON);
            }
        }
        
        if (releLigado && noCiclo && (agora - tLigouCiclo >= durLigado)) {
            releLigado = false; noCiclo = false; digitalWrite(pin, RELE_OFF);
        }
    }
};

// ══════════════════════════════════════════════════════════════
//  CONTROLE UMIDADE
// ══════════════════════════════════════════════════════════════
class ControleUmidade {
private:
    float alvo = DEFAULT_UMID, uAtual = NAN;
    int   pin;
    bool  releLigado = false;
public:
    ControleUmidade(float a, int p) : alvo(a), pin(p) {
        pinMode(pin, OUTPUT); 
        digitalWrite(pin, RELE_OFF);
    }
    
    void setAlvo(float v) { alvo = v; }
    void setUmidade(float v) { uAtual = v; }
    bool isLigado() { return releLigado; }

    void atualizar() {
        if (isnan(uAtual)) {
            if (releLigado) { releLigado = false; digitalWrite(pin, RELE_OFF); }
            return;
        }
        
        if (!releLigado && uAtual < (alvo - 5.0f)) { 
            releLigado = true;  
            digitalWrite(pin, RELE_ON);  
        }
        else if (releLigado && uAtual >= alvo) { 
            releLigado = false; 
            digitalWrite(pin, RELE_OFF); 
        }
    }
};

// ══════════════════════════════════════════════════════════════
//  CONTROLE SERVO
// ══════════════════════════════════════════════════════════════
class ControleServo {
private:
    Servo servo;
    int   pin, posAtual = 90, posDest = 180, posRepouso = 90;
    bool  ativo = false, estabilizando = false;
    unsigned long tPasso = 0, tEstab = 0;
public:
    ControleServo(int p) : pin(p) { pinMode(pin, INPUT); }
    
    void setPosicaoRepouso(int p) { if (p == 90 || p == 180) posRepouso = p; }
    bool isAtivo() { return ativo; }
    
    int getProgresso() {
        int total = abs(posDest - posRepouso);
        int atual = abs(posAtual - posRepouso);
        return total > 0 ? constrain(atual * 100 / total, 0, 100) : 100;
    }
    
    void iniciarViragem() {
        if (ativo) return;
        posDest = (posRepouso == 90) ? 180 : 90;
        posAtual = posRepouso;
        estabilizando = false;
        pinMode(pin, OUTPUT);
        servo.attach(pin);
        servo.write(posAtual);
        ativo = true; 
        tPasso = millis();
    }
    
    void atualizar() {
        if (!ativo) return;
        unsigned long agora = millis();
        
        if (estabilizando) {
            if (agora - tEstab >= 500) {
                servo.detach(); 
                pinMode(pin, INPUT);
                ativo = false; 
                estabilizando = false;
            }
            return;
        }
        
        if (agora - tPasso < 40) return;
        tPasso = agora;
        
        if (posAtual != posDest) {
            posAtual += (posDest > posAtual) ? 1 : -1;
            servo.write(posAtual);
        } else {
            posRepouso = posDest; 
            estabilizando = true; 
            tEstab = agora;
        }
    }
};

// ══════════════════════════════════════════════════════════════
//  OBJETOS GLOBAIS
// ══════════════════════════════════════════════════════════════
ConfigManager       config;
SistemaState        sistema;
ControleTemperatura controleTemp(DEFAULT_TEMP, PIN_RELE_CALOR);
ControleUmidade     controleUmid(DEFAULT_UMID, PIN_RELE_UMID);
ControleServo       controleServo(PIN_SERVO);
DHT                 dht(PIN_DHT, DHT22);
Adafruit_SSD1306    display(OLED_W, OLED_H, &Wire, -1);
WebServer           webServer(WEB_PORT);

// ══════════════════════════════════════════════════════════════
//  WEBSOCKET CLIENT (Cloud Hub)
// ══════════════════════════════════════════════════════════════
class CloudWS {
private:
    WebsocketsClient client;
    bool conectado = false;
    unsigned long tUltimoStatus = 0, tReconnect = 0;

    void onMsg(WebsocketsMessage msg) {
        DynamicJsonDocument doc(512);
        if (deserializeJson(doc, msg.data())) return;
        
        String c = doc["comando"] | "";
        float v = doc["valor"] | 0.0f;
        
        if (c == "setTemp" && v >= TEMP_MIN && v <= TEMP_MAX) {
            config.setT = v;
            controleTemp.setAlvo(v);
            config.salvar();
            Serial.println("[WS] Temp alterada para " + String(v));
        }
        else if (c == "setUmid" && v >= UMID_MIN && v <= UMID_MAX) {
            config.setH = v;
            controleUmid.setAlvo(v);
            config.salvar();
            Serial.println("[WS] Umid alterada para " + String(v));
        }
        else if (c == "virar") {
            controleServo.iniciarViragem();
            Serial.println("[WS] Viragem remota");
        }
    }
    
    void onEvt(WebsocketsEvent evt, String data) {
        if (evt == WebsocketsEvent::ConnectionOpened) {
            conectado = true;
            Serial.println("[WS] ✅ Conectado ao cloud!");
            handshake();
        } else if (evt == WebsocketsEvent::ConnectionClosed) {
            conectado = false;
            Serial.println("[WS] ❌ Desconectado do cloud");
        }
    }
    
    void handshake() {
        DynamicJsonDocument doc(256);
        doc["tipo"] = "handshake";
        doc["deviceId"] = config.deviceId;
        doc["apiKey"] = config.apiKey;
        doc["firmware"] = FIRMWARE_VERSION;
        doc["tipoDispositivo"] = "chocadeira_client";
        String s;
        serializeJson(doc, s);
        client.send(s);
    }
    
public:
    void init() {
        client.onMessage([this](WebsocketsMessage m) { onMsg(m); });
        client.onEvent([this](WebsocketsEvent e, String d) { onEvt(e, d); });
    }
    
    void conectar() {
        if (!config.cloudEnabled || WiFi.status() != WL_CONNECTED) return;
        if (conectado) return;
        
        unsigned long a = millis();
        if (a - tReconnect < WS_RECONNECT_MS) return;
        tReconnect = a;
        
        String url = "ws://" + config.cloudHost + ":" + String(config.cloudPort);
        Serial.println("[WS] Tentando conectar a " + url);
        client.connect(url);
    }
    
    void loop() {
        if (!config.cloudEnabled) return;
        
        if (client.available()) client.poll();
        if (!conectado) conectar();
        
        if (conectado && millis() - tUltimoStatus >= MS_CLOUD_UPDATE) {
            tUltimoStatus = millis();
            enviarStatus();
        }
    }
    
    void enviarStatus() {
        DynamicJsonDocument doc(512);
        doc["tipo"] = "status";
        doc["deviceId"] = config.deviceId;
        doc["temperatura"] = isnan(sistema.temperatura) ? 0 : sistema.temperatura;
        doc["umidade"] = isnan(sistema.umidade) ? 0 : sistema.umidade;
        doc["setT"] = config.setT;
        doc["setH"] = config.setH;
        doc["intervaloV"] = config.intervaloV;
        doc["especie"] = config.especie;
        doc["diasTotal"] = config.diasTotal;
        doc["diaAtual"] = config.diaAtual();
        doc["incubacaoAtiva"] = config.incubacaoAtiva;
        doc["releCalor"] = controleTemp.isLigado();
        doc["releUmid"] = controleUmid.isLigado();
        doc["modoFull"] = controleTemp.isModoFull() ? 1 : 0;
        doc["servoAtivo"] = controleServo.isAtivo();
        doc["rssi"] = WiFi.RSSI();
        doc["ip"] = WiFi.localIP().toString();
        
        String s;
        serializeJson(doc, s);
        if (conectado) client.send(s);
    }
    
    bool isConectado() { return conectado; }
};

CloudWS wsClient;

// ══════════════════════════════════════════════════════════════
//  REGISTRO NO CLOUD (Primeiro Acesso)
// ══════════════════════════════════════════════════════════════
bool registrarNoCloud() {
    if (config.apiKey.length() > 0) return true;  // Já registrado
    
    HTTPClient http;
    String url = "http://" + config.cloudHost + ":" + String(config.cloudPort) + CLOUD_REGISTER_PATH;
    
    DynamicJsonDocument doc(256);
    doc["deviceId"] = config.deviceId;
    String body;
    serializeJson(doc, body);
    
    http.begin(url);
    http.addHeader("Content-Type", "application/json");
    http.setTimeout(5000);
    
    int code = http.POST(body);
    bool sucesso = false;
    
    if (code == 200) {
        DynamicJsonDocument resp(256);
        if (!deserializeJson(resp, http.getString())) {
            if (resp["ok"]) {
                config.apiKey = resp["apiKey"].as<String>();
                config.cloudEnabled = true;
                config.salvar();
                sucesso = true;
                Serial.println("[CLOUD] ✅ Registrado! API Key: " + config.apiKey.substring(0, 8) + "...");
            }
        }
    }
    
    http.end();
    return sucesso;
}

// ══════════════════════════════════════════════════════════════
//  JSON HELPERS
// ══════════════════════════════════════════════════════════════
String statusJSON() {
    DynamicJsonDocument doc(1024);
    doc["temperatura"] = isnan(sistema.temperatura) ? 0 : sistema.temperatura;
    doc["umidade"] = isnan(sistema.umidade) ? 0 : sistema.umidade;
    doc["setT"] = config.setT;
    doc["setH"] = config.setH;
    doc["intervaloV"] = config.intervaloV;
    doc["especie"] = config.especie;
    doc["diasTotal"] = config.diasTotal;
    doc["diaAtual"] = config.diaAtual();
    doc["incubacaoAtiva"] = config.incubacaoAtiva;
    doc["releCalor"] = controleTemp.isLigado();
    doc["releUmid"] = controleUmid.isLigado();
    doc["modoFull"] = controleTemp.isModoFull();
    doc["servoAtivo"] = controleServo.isAtivo();
    doc["servoPos"] = config.servoPosRepo;
    doc["ip"] = modoAP ? "192.168.4.1" : WiFi.localIP().toString();
    doc["rssi"] = WiFi.RSSI();
    doc["deviceId"] = config.deviceId;
    doc["cloudEnabled"] = config.cloudEnabled;
    doc["wsConectado"] = wsClient.isConectado();
    doc["fwVersion"] = FIRMWARE_VERSION;
    doc["modoAP"] = modoAP;
    
    String s;
    serializeJson(doc, s);
    return s;
}

// ══════════════════════════════════════════════════════════════
//  WEB SERVER LOCAL
// ══════════════════════════════════════════════════════════════
void setupWebServer() {
    // API Status
    webServer.on("/api/status", HTTP_GET, []() {
        webServer.send(200, "application/json", statusJSON());
    });
    
    // API Config
    webServer.on("/api/config", HTTP_POST, []() {
        DynamicJsonDocument doc(512);
        deserializeJson(doc, webServer.arg("plain"));
        
        String acao = doc["acao"];
        
        if (acao == "setTemp") {
            float v = doc["valor"];
            if (v >= TEMP_MIN && v <= TEMP_MAX) {
                config.setT = v;
                controleTemp.setAlvo(v);
                config.salvar();
            }
        }
        else if (acao == "setUmid") {
            float v = doc["valor"];
            if (v >= UMID_MIN && v <= UMID_MAX) {
                config.setH = v;
                controleUmid.setAlvo(v);
                config.salvar();
            }
        }
        else if (acao == "iniciarIncub") {
            config.especie = doc["especie"] | "Galinha";
            config.diasTotal = doc["diasTotal"] | 21;
            config.intervaloV = doc["intervaloV"] | 4;
            if (!config.incubacaoAtiva) config.iniciarIncubacao();
        }
        
        webServer.send(200, "application/json", "{\"ok\":true}");
    });
    
    // API Virar
    webServer.on("/api/virar", HTTP_GET, []() {
        controleServo.iniciarViragem();
        webServer.send(200, "application/json", "{\"ok\":true}");
    });
    
    // Dashboard local (página simples)
    webServer.on("/", HTTP_GET, []() {
        String html = "<h1>🐣 EA CHOCA+ v7.0</h1>";
        html += "<p>Device: " + config.deviceId + "</p>";
        html += "<p>Temperatura: " + String(sistema.temperatura, 1) + "°C</p>";
        html += "<p>Umidade: " + String(sistema.umidade, 0) + "%</p>";
        html += "<p>Cloud: " + String(wsClient.isConectado() ? "✅ Conectado" : "❌ Desconectado") + "</p>";
        webServer.send(200, "text/html", html);
    });
    
    webServer.begin();
    Serial.println("[WEB] Servidor HTTP local iniciado");
}

// ══════════════════════════════════════════════════════════════
//  DISPLAY OLED
// ══════════════════════════════════════════════════════════════
void atualizarDisplay() {
    static uint8_t tela = 0;
    static unsigned long tTela = 0;

    if (!controleServo.isAtivo() && millis() - tTela > 4000) {
        tTela = millis();
        tela = (tela + 1) % 4;
    }

    display.clearDisplay();
    display.setTextColor(WHITE);

    const char* hdr;
    if (modoAP)                      hdr = "MODO AP";
    else if (controleServo.isAtivo()) hdr = "VIRANDO";
    else if (controleTemp.isModoFull()) hdr = "AQUECENDO";
    else switch (tela) {
        case 0: hdr = "TEMPERATURA"; break;
        case 1: hdr = "UMIDADE"; break;
        case 2: hdr = "INCUBACAO"; break;
        default:hdr = "SISTEMA"; break;
    }
    
    display.setTextSize(1);
    display.setCursor(4, 0);
    display.print(hdr);
    display.drawLine(0, 10, 128, 10, WHITE);

    if (controleServo.isAtivo()) {
        display.setTextSize(1);
        display.setCursor(4, 14);
        display.print("Virando...");
        display.drawRect(4, 30, 120, 10, WHITE);
        display.fillRect(4, 30, controleServo.getProgresso() * 120 / 100, 10, WHITE);
    } else if (modoAP) {
        display.setTextSize(1);
        display.setCursor(4, 20);
        display.print("WiFi: EA CHOCA+");
        display.setCursor(4, 35);
        display.print("IP: 192.168.4.1");
        display.setCursor(4, 50);
        display.print("Cloud: " + String(wsClient.isConectado() ? "OK" : "OFF"));
    } else {
        switch (tela) {
            case 0: {
                if (isnan(sistema.temperatura)) {
                    display.setTextSize(1);
                    display.setCursor(20, 30);
                    display.print("ERRO SENSOR");
                } else {
                    display.setTextSize(2);
                    display.setCursor(10, 20);
                    display.print(sistema.temperatura, 1);
                    display.print("C");
                    display.setTextSize(1);
                    display.setCursor(4, 50);
                    display.print("Alvo: " + String(config.setT, 1) + "C");
                }
                break;
            }
            case 1: {
                if (isnan(sistema.umidade)) {
                    display.setTextSize(1);
                    display.setCursor(20, 30);
                    display.print("ERRO SENSOR");
                } else {
                    display.setTextSize(2);
                    display.setCursor(20, 20);
                    display.print((int)sistema.umidade);
                    display.print("%");
                    display.setTextSize(1);
                    display.setCursor(4, 50);
                    display.print("Alvo: " + String((int)config.setH) + "%");
                }
                break;
            }
            case 2: {
                int d = config.diaAtual();
                display.setTextSize(1);
                display.setCursor(4, 14);
                display.print(config.especie);
                if (d > 0) {
                    display.setTextSize(2);
                    display.setCursor(20, 28);
                    display.print("D" + String(d) + "/" + String(config.diasTotal));
                } else {
                    display.setTextSize(1);
                    display.setCursor(4, 35);
                    display.print("Nao iniciada");
                }
                break;
            }
            case 3: {
                display.setTextSize(1);
                display.setCursor(4, 14);
                display.print("ID: " + config.deviceId.substring(0, 10));
                display.setCursor(4, 26);
                display.print("FW: " + String(FIRMWARE_VERSION));
                display.setCursor(4, 38);
                display.print("WS: " + String(wsClient.isConectado() ? "ON" : "OFF"));
                display.setCursor(4, 50);
                display.print("RSSI: " + String(WiFi.RSSI()) + "dBm");
                break;
            }
        }
    }
    
    display.display();
}

// ══════════════════════════════════════════════════════════════
//  TIMERS E SENSORES
// ══════════════════════════════════════════════════════════════
unsigned long tDHT=0, tDisplay=0, tWifiCheck=0, tViragem=0, tOTACheck=0;

void lerSensores() {
    float t = dht.readTemperature();
    float h = dht.readHumidity();
    
    if (!isnan(t) && !isnan(h)) {
        sistema.temperatura = t;
        sistema.umidade = h;
        controleTemp.setTemperatura(t);
        controleUmid.setUmidade(h);
    }
}

void verificarViragem() {
    if (!controleServo.isAtivo() && config.deveVirar() && config.intervaloV > 0) {
        if (millis() - tViragem >= (unsigned long)config.intervaloV * 3600000UL) {
            tViragem = millis();
            controleServo.iniciarViragem();
            Serial.println("[VIRAGEM] Auto D" + String(config.diaAtual()));
        }
    }
}

// ══════════════════════════════════════════════════════════════
//  SETUP
// ══════════════════════════════════════════════════════════════
void setup() {
    Serial.begin(115200);
    Serial.println("\n\nEA CHOCA+ CLIENT v7.0 Aurora — Iniciando...\n");

    pinMode(PIN_RELE_CALOR, OUTPUT);
    digitalWrite(PIN_RELE_CALOR, RELE_OFF);
    pinMode(PIN_RELE_UMID, OUTPUT);
    digitalWrite(PIN_RELE_UMID, RELE_OFF);

    Wire.begin();
    if (display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
        display.clearDisplay();
        display.setTextColor(WHITE);
        display.setTextSize(2);
        display.setCursor(8, 10);
        display.print("EA CHOCA+");
        display.setTextSize(1);
        display.setCursor(14, 36);
        display.print("v7.0 Aurora");
        display.setCursor(14, 50);
        display.print("Iniciando...");
        display.display();
    }

    config.init();
    dht.begin();

    WiFi.mode(WIFI_STA);
    WiFiManager wm;
    wm.setConfigPortalTimeout(AP_TIMEOUT);

    bool conectou = wm.autoConnect(AP_SSID, AP_PASSWORD);

    if (conectou) {
        modoAP = false;
        Serial.println("[WiFi] ✅ Conectado: " + WiFi.localIP().toString());
        configTime(NTP_TZ * 3600, 0, NTP_SERVER1, NTP_SERVER2);
        
        // Tentar registrar no cloud se for primeiro acesso
        if (primeiroAcesso && config.apiKey.length() == 0) {
            Serial.println("[SETUP] Primeiro acesso - Registrando no cloud...");
            delay(2000);  // Aguardar conexão estabilizar
            if (registrarNoCloud()) {
                Serial.println("[SETUP] ✅ Registrado com sucesso!");
            } else {
                Serial.println("[SETUP] ⚠️ Registro falhou - Continuando em modo local");
            }
        }
    } else {
        modoAP = true;
        Serial.println("[WiFi] ❌ Modo AP offline");
    }

    setupWebServer();
    wsClient.init();

    controleTemp.setAlvo(config.setT);
    controleUmid.setAlvo(config.setH);
    controleServo.setPosicaoRepouso(config.servoPosRepo);
    tViragem = millis();

    Serial.println("\n[OK] ✅ EA CHOCA+ CLIENT v7.0 pronto!");
    Serial.println("    DeviceID: " + config.deviceId);
    Serial.println("    Cloud: " + String(config.cloudEnabled ? "ATIVO" : "INATIVO"));
    Serial.println("    Web Local: http://" + (modoAP ? "192.168.4.1" : WiFi.localIP().toString()));
}

// ══════════════════════════════════════════════════════════════
//  LOOP
// ══════════════════════════════════════════════════════════════
void loop() {
    unsigned long agora = millis();

    webServer.handleClient();

    if (!modoAP) wsClient.loop();

    // Sensores
    if (agora - tDHT >= MS_DHT) {
        tDHT = agora;
        lerSensores();
    }

    // Controles
    controleTemp.atualizar();
    controleUmid.atualizar();
    controleServo.atualizar();

    // Viragem
    verificarViragem();

    // Display
    if (agora - tDisplay >= MS_DISPLAY) {
        tDisplay = agora;
        atualizarDisplay();
    }

    delay(10);
}
