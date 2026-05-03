#include "NetworkManager.h"
#include <ArduinoJson.h>
#include "Config.h"

WebSocketsClient webSocket;
TelemetryPacket* offlineBuffer = nullptr;
int bufferHead = 0;
int bufferTail = 0;
int bufferCount = 0;
portMUX_TYPE bufferMux = portMUX_INITIALIZER_UNLOCKED;

void drainOfflineBuffer()
{
    if(bufferCount == 0)
    {
       webSocket.sendTXT("42[\"log_status\", \"Buffer boş\"]");
        return; 
    }
    int totalToSend = bufferCount;
    Serial.printf("[BBOX] %d paket gönderiliyor...\n", totalToSend);

    while (bufferCount > 0) {
        TelemetryPacket oldData;
        portENTER_CRITICAL(&bufferMux);
        oldData = offlineBuffer[bufferTail];
        bufferTail = (bufferTail + 1) % MAX_OFFLINE_PACKETS;
        bufferCount--;
        portEXIT_CRITICAL(&bufferMux);
        

        JsonDocument doc;
        doc["Time"]     = oldData.timestamp;
        doc["Depth"]    = oldData.depth;
        doc["Ax"]       = oldData.ax;
        doc["Ay"]       = oldData.ay;
        doc["Az"]       = oldData.az;
        doc["pitch"]    = oldData.pitch;
        doc["roll"]     = oldData.roll;
        doc["yaw"]      = oldData.yaw;
        doc["velocity"] = oldData.velocity;
        doc["distance"] = oldData.distance;

        String payload;
        serializeJson(doc, payload);
        webSocket.sendTXT("42[\"log_data\"," + payload + "]");

        vTaskDelay(pdMS_TO_TICKS(10));

    }
    webSocket.sendTXT("42[\"log_status\", \"Tamamlandı\"]");
    Serial.println("[BBOX] Log transferi basariyla bitti.");
}



// ─────────────────────────────────────────────
void initBlackBox() {
    offlineBuffer = (TelemetryPacket*)ps_malloc(MAX_OFFLINE_PACKETS * sizeof(TelemetryPacket));

    if (offlineBuffer == NULL) {
        Serial.println("[BBOX][UYARI] PSRAM tahsisi BAŞARISIZ! Normal RAM deneniyor...");
        Serial.printf("[BBOX] Mevcut serbest heap: %d byte\n", ESP.getFreeHeap());
        offlineBuffer = (TelemetryPacket*)malloc(MAX_OFFLINE_PACKETS * sizeof(TelemetryPacket));
    } else {
        Serial.println("[BBOX] PSRAM tahsisi BAŞARILI.");
    }

    if (offlineBuffer) {
        Serial.println("[BBOX] BlackBox AKTİF.");
    } else {
        Serial.println("[BBOX][HATA] Bellek tahsisi TAMAMEN BAŞARISIZ! BlackBox DEVRE DIŞI.");
    }
}
static int8_t isArmed;

// ─────────────────────────────────────────────
void onWebSocketEvent(WStype_t type, uint8_t* payload, size_t length) {
    switch (type) {
        case WStype_DISCONNECTED:
            webSocket.disconnect();
            Serial.println("[WS] BAĞLANTI KESİLDİ!");
            break;
        case WStype_CONNECTED:
            Serial.println("[WS] BAĞLANDI! Socket.IO handshake gönderiliyor...");
            webSocket.sendTXT("40");
            break;
        case WStype_TEXT: {
            String text = (char*)payload;
            if (text.indexOf("{") != -1) {
                JsonDocument doc;
                DeserializationError err = deserializeJson(doc, text.substring(text.indexOf('{'), text.lastIndexOf('}') + 1));

                if (err) {
                    Serial.printf("[WS][HATA] JSON parse hatası: %s\n", err.c_str());
                    break;
                }

                ControlPacket cmd = {0};
                const char* action = doc["action"] | "";
                //Serial.printf("[WS] Action: '%s'\n", action);

                if (strcmp(action, "ARM") == 0) {
                    cmd.action = 1;
                    isArmed = 1;
                }
                else if (strcmp(action, "DISARM") == 0){
                    cmd.action = 2;
                    isArmed = 0;
                }
                else if (strcmp(action, "RESET") == 0)  cmd.action = 3;
                else if (strcmp(action, "PID_SETPOINT_UPDATE") == 0) {
                    cmd.action = 4;
                    cmd.kp_p = doc["kppitch"] | 0.0f;
                    cmd.ki_p = doc["kipitch"] | 0.0f;
                    cmd.kd_p = doc["kdpitch"] | 0.0f;
                    cmd.kp_y = doc["kpyaw"]   | 0.0f; 
                    cmd.ki_y = doc["kiyaw"]   | 0.0f;  
                    cmd.kd_y = doc["kdyaw"]   | 0.0f;  
                    cmd.set_p = doc["setpitch"] | 0.0f;
                    cmd.set_y = doc["setyaw"]   | 0.0f;
                    Serial.printf("[WS] PID Pitch → Kp:%.2f Ki:%.2f Kd:%.2f Set:%.2f\n",
                        cmd.kp_p, cmd.ki_p, cmd.kd_p, cmd.set_p);
                    Serial.printf("[WS] PID Yaw   → Kp:%.2f Ki:%.2f Kd:%.2f Set:%.2f\n",
                        cmd.kp_y, cmd.ki_y, cmd.kd_y, cmd.set_y);
                }
                else if(strcmp(action , "DOWNLOAD") == 0)
                {
                    drainOfflineBuffer();
                }
     

                if(cmd.action != 0) xQueueSend(cmdQueue, &cmd, pdMS_TO_TICKS(10));
            }
            break;
        }
        case WStype_ERROR:
            Serial.println("[WS][HATA] WebSocket hatası!");
            break;
        case WStype_PING:
            Serial.println("[WS] PING alındı.");
            break;
        case WStype_PONG:
            Serial.println("[WS] PONG alındı.");
            break;
        default:
            Serial.printf("[WS] Bilinmeyen event tipi: %d\n", type);
            break;
    }
}
// ─────────────────────────────────────────────
void transmitToGCS(const TelemetryPacket& data) {
    JsonDocument doc;
    doc["Time"]     = data.timestamp;
    doc["Depth"]    = data.depth;
    doc["Ax"]       = data.ax;
    doc["Ay"]       = data.ay;
    doc["Az"]       = data.az;
    doc["pitch"]    = data.pitch;
    doc["roll"]     = data.roll;
    doc["yaw"]      = data.yaw;
    doc["velocity"] = data.velocity;
    doc["distance"] = data.distance;
    doc["armed"]    = isArmed;
    String payload;
    serializeJson(doc, payload);

    bool sent = webSocket.sendTXT("42[\"telemetry\"," + payload + "]");
    if (!sent) {
        Serial.println("[GCS][HATA] Telemetri gönderilemedi! WebSocket?");
    }
}

// ─────────────────────────────────────────────
void sendTelemetryToGCS(const TelemetryPacket& data) {
    if (webSocket.isConnected()) {
        transmitToGCS(data);
    } else if (offlineBuffer != nullptr) {
        portENTER_CRITICAL(&bufferMux);
        offlineBuffer[bufferHead] = data;
        bufferHead = (bufferHead + 1) % MAX_OFFLINE_PACKETS;
        if (bufferCount < MAX_OFFLINE_PACKETS) bufferCount++;
        else bufferTail = (bufferTail + 1) % MAX_OFFLINE_PACKETS;
        portEXIT_CRITICAL(&bufferMux);
    } else {
        Serial.println("[GCS][HATA] WS bağlı değil VE buffer null! Telemetri KAYBOLDU.");
    }
}

// ─────────────────────────────────────────────
void networkTask(void* parameters) {
    initBlackBox();
    initFOTA();

    IPAddress local_IP(10, 126, 19, 100);
    IPAddress gateway(10, 126, 19, 1);
    IPAddress subnet(255, 255, 255, 0);
    
    WiFi.mode(WIFI_STA); 
    delay(100); 
    

    Serial.printf("[NET] WiFi'a bağlanılıyor: %s\n", SSID_NAME);
    WiFi.begin(SSID_NAME, PASSWORD);

    int wifiRetry = 0;
    while (WiFi.status() != WL_CONNECTED) {
        vTaskDelay(pdMS_TO_TICKS(500));
        wifiRetry++;
        if (wifiRetry % 10 == 0) {
            Serial.printf("[NET] WiFi bekleniyor... (%d saniye)\n", wifiRetry / 2);
        }
        if (wifiRetry > 60) {
            Serial.println("[NET][HATA] WiFi bağlantısı 30 saniyede kurulamadı! Yeniden başlatılıyor...");
            ESP.restart();
        }
    }

    Serial.printf("[NET] WiFi BAĞLANDI! IP: %s | RSSI: %d dBm\n",
        WiFi.localIP().toString().c_str(), WiFi.RSSI());
    server.begin(); // ← WiFi bağlandıktan SONRA buraya taşı
    Serial.println("[FOTA] HTTP Server başlatıldı.");
    
    
    
    Serial.printf("[NET] WebSocket bağlanılıyor: %s:%d\n", SERVER_IP, SERVER_PORT);
    
    
    //webSocket.enableHeartbeat(5000 , 2000 , 2);
    webSocket.begin(SERVER_IP, SERVER_PORT, "/socket.io/?EIO=4&transport=websocket");
    webSocket.onEvent(onWebSocketEvent);

    Serial.println("[NET] Ana döngü başlıyor...");

    uint32_t lastStatusLog = 0;
    WiFi.setSleep(false);
    
    
    for (;;) {
        if (WiFi.status() != WL_CONNECTED) {
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }
        server.handleClient();
        vTaskDelay(2);
        webSocket.loop();
        vTaskDelay(3);
        // Her 5 saniyede bir durum raporu
        if (millis() - lastStatusLog > 5000) {
            lastStatusLog = millis();
            Serial.printf("[NET] Durum → WiFi:%s | WS:%s | Buffer:%d | Heap:%d\n",
                WiFi.status() == WL_CONNECTED ? "OK" : "KOPUK",
                webSocket.isConnected() ? "OK" : "KOPUK",
                bufferCount,
                ESP.getFreeHeap());
        }
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}