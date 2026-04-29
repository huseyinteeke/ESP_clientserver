#include "NetworkManager.h"
#include <ArduinoJson.h>
#include "Config.h"
#include <CRC32.h>

WebSocketsClient webSocket;
WebServer server(80);
static File fotaFile;
TelemetryPacket* offlineBuffer = nullptr;
int bufferHead = 0;
int bufferTail = 0;
int bufferCount = 0;
portMUX_TYPE bufferMux = portMUX_INITIALIZER_UNLOCKED;

// ─────────────────────────────────────────────
void verifyAndCompleteFOTA() {
    Serial.println("[FOTA] verifyAndCompleteFOTA() başladı.");

    String expectedCrcStr = server.header("X-File-CRC");
    Serial.printf("[FOTA] Beklenen CRC string: '%s'\n", expectedCrcStr.c_str());

    if (expectedCrcStr.isEmpty()) {
        Serial.println("[FOTA][HATA] X-File-CRC header'ı boş! İstek header'ı eksik.");
        return;
    }

    uint32_t expectedCrc = strtoul(expectedCrcStr.c_str(), NULL, 16);
    Serial.printf("[FOTA] Beklenen CRC (hex): %08X\n", expectedCrc);

    File f = LittleFS.open("/stm32_fw.bin", FILE_READ);
    if (!f) {
        Serial.println("[FOTA][HATA] /stm32_fw.bin açılamadı! Dosya yok mu?");
        return;
    }

    Serial.printf("[FOTA] Dosya boyutu: %d byte\n", f.size());
    Serial.println("[FOTA] CRC hesaplanıyor...");

    CRC32 crc;
    size_t byteCount = 0;
    while (f.available()) {
        crc.update(f.read());
        byteCount++;
    }
    f.close();

    Serial.printf("[FOTA] Toplam okunan byte: %d\n", byteCount);

    uint32_t calculatedCrc = crc.finalize();
    Serial.printf("[FOTA] Hesaplanan CRC: %08X\n", calculatedCrc);

    if (calculatedCrc == expectedCrc) {
        Serial.println("[FOTA] BAŞARILI! CRC eşleşti. STM32'ye aktarım başlayacak.");
        // STM32 Upload ToDo
    } else {
        Serial.printf("[FOTA][HATA] CRC UYUŞMUYOR! Beklenen: %08X | Hesaplanan: %08X\n", expectedCrc, calculatedCrc);
        Serial.println("[FOTA] Bozuk firmware siliniyor...");
        LittleFS.remove("/stm32_fw.bin");
        Serial.println("[FOTA] Dosya silindi.");
    }
}

// ─────────────────────────────────────────────
void initFOTA() {
    const char * headerkeys[] = {"X-File-CRC"} ;
    size_t headerkeyssize = sizeof(headerkeys)/sizeof(char*);
    server.collectHeaders(headerkeys, headerkeyssize);
    Serial.println("[FOTA] initFOTA() başladı.");

    if (!LittleFS.begin(true)) {
        Serial.println("[FOTA][HATA] LittleFS mount BAŞARISIZ!");
        return;
    }
    Serial.println("[FOTA] LittleFS mount başarılı.");

    // Mevcut firmware dosyası var mı?
    if (LittleFS.exists("/stm32_fw.bin")) {
        File existing = LittleFS.open("/stm32_fw.bin", FILE_READ);
        Serial.printf("[FOTA] Mevcut firmware bulundu: %d byte\n", existing.size());
        existing.close();
    } else {
        Serial.println("[FOTA] Mevcut firmware yok (ilk kurulum).");
    }
        // OPTIONS isteklerine boş ama onaylayan bir cevap dön
        server.on("/upload_firmware", HTTP_OPTIONS, []() {
            server.sendHeader("Access-Control-Allow-Origin", "*");
            server.sendHeader("Access-Control-Allow-Methods", "POST,GET,OPTIONS");
            server.sendHeader("Access-Control-Allow-Headers", "*");
            server.send(200);
        });


    server.on("/upload_firmware", HTTP_POST, []() {
        server.sendHeader("Access-Control-Allow-Origin", "*");
        server.sendHeader("Access-Control-Allow-Methods", "POST,GET,OPTIONS");
        server.sendHeader("Access-Control-Allow-Headers", "*");
        Serial.println("[FOTA][HTTP] POST tamamlandı, response gönderiliyor.");
        server.send(200, "text/plain", "OK: File at ESP32. STM32 transfer part.");
    }, []() {
        HTTPUpload& upload = server.upload();

        if (upload.status == UPLOAD_FILE_START) {
            Serial.println("[FOTA][UPLOAD] ─── YENİ UPLOAD BAŞLADI ───");
            Serial.printf("[FOTA][UPLOAD] Dosya adı: %s\n", upload.filename.c_str());
            LittleFS.remove("/stm32_fw.bin");
            Serial.println("[FOTA][UPLOAD] Eski firmware silindi.");
            fotaFile = LittleFS.open("/stm32_fw.bin", FILE_WRITE);
            if (fotaFile) {
                Serial.println("[FOTA][UPLOAD] Yeni dosya açıldı, yazılmaya hazır.");
            } else {
                Serial.println("[FOTA][UPLOAD][HATA] Dosya açılamadı!");
            }

        } else if (upload.status == UPLOAD_FILE_WRITE) {
            if (fotaFile) {
                size_t written = fotaFile.write(upload.buf, upload.currentSize);
                Serial.printf("[FOTA][UPLOAD] Chunk yazıldı: %d byte (toplam: %d byte)\n", written, upload.totalSize);
            } else {
                Serial.println("[FOTA][UPLOAD][HATA] Dosya handle geçersiz! Yazma atlandı.");
            }

        } else if (upload.status == UPLOAD_FILE_END) {
            if (fotaFile) {
                fotaFile.close();
                Serial.println("[FOTA][UPLOAD] Dosya kapatıldı.");
            }
            Serial.printf("[FOTA][UPLOAD] ─── UPLOAD TAMAMLANDI: %u byte ───\n", upload.totalSize);
            verifyAndCompleteFOTA();

        } else if (upload.status == UPLOAD_FILE_ABORTED) {
            Serial.println("[FOTA][UPLOAD][HATA] Upload IPTAL EDİLDİ!");
            if (fotaFile) fotaFile.close();
            LittleFS.remove("/stm32_fw.bin");
        }
    });
    Serial.println("[FOTA] Route tanımlandı. Server henüz başlatılmadı.");

}

// ─────────────────────────────────────────────
void initBlackBox() {
    Serial.println("[BBOX] initBlackBox() başladı.");
    Serial.printf("[BBOX] İstenilen boyut: %d paket x %d byte = %d byte\n",
        MAX_OFFLINE_PACKETS, sizeof(TelemetryPacket),
        MAX_OFFLINE_PACKETS * sizeof(TelemetryPacket));

    Serial.printf("[BBOX] Mevcut serbest PSRAM: %d byte\n", ESP.getFreePsram());

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

// ─────────────────────────────────────────────
void onWebSocketEvent(WStype_t type, uint8_t* payload, size_t length) {
    switch (type) {
        case WStype_DISCONNECTED:
            Serial.println("[WS] BAĞLANTI KESİLDİ!");
            break;
        case WStype_CONNECTED:
            Serial.println("[WS] BAĞLANDI! Socket.IO handshake gönderiliyor...");
            webSocket.sendTXT("40");
            break;
        case WStype_TEXT: {
            Serial.printf("[WS] Mesaj alındı (%d byte): %s\n", length, payload);
            String text = (char*)payload;
            if (text.indexOf("{") != -1) {
                Serial.println("[WS] JSON içeriği tespit edildi, parse ediliyor...");
                JsonDocument doc;
                DeserializationError err = deserializeJson(doc, text.substring(text.indexOf('{'), text.lastIndexOf('}') + 1));

                if (err) {
                    Serial.printf("[WS][HATA] JSON parse hatası: %s\n", err.c_str());
                    break;
                }

                ControlPacket cmd = {0};
                const char* action = doc["action"] | "";
                Serial.printf("[WS] Action: '%s'\n", action);

                if (strcmp(action, "ARM") == 0)         cmd.action = 1;
                else if (strcmp(action, "DISARM") == 0) cmd.action = 2;
                else if (strcmp(action, "RESET") == 0)  cmd.action = 3;
                else if (strcmp(action, "PID_SETPOINT_UPDATE") == 0) {
                    cmd.action = 4;
                    cmd.kp_p = doc["kppitch"] | 0.0f;
                    cmd.ki_p = doc["kipitch"] | 0.0f;
                    cmd.kd_p = doc["kdpitch"] | 0.0f;
                    cmd.kp_y = doc["kpyaw"]   | 0.0f;  // Düzeltildi
                    cmd.ki_y = doc["kiyaw"]   | 0.0f;  // Düzeltildi
                    cmd.kd_y = doc["kdyaw"]   | 0.0f;  // Düzeltildi
                    cmd.set_p = doc["setpitch"] | 0.0f;
                    cmd.set_y = doc["setyaw"]   | 0.0f;
                    Serial.printf("[WS] PID Pitch → Kp:%.2f Ki:%.2f Kd:%.2f Set:%.2f\n",
                        cmd.kp_p, cmd.ki_p, cmd.kd_p, cmd.set_p);
                    Serial.printf("[WS] PID Yaw   → Kp:%.2f Ki:%.2f Kd:%.2f Set:%.2f\n",
                        cmd.kp_y, cmd.ki_y, cmd.kd_y, cmd.set_y);
                } else {
                    Serial.printf("[WS][UYARI] Tanınmayan action: '%s'\n", action);
                }

                BaseType_t qResult = xQueueSend(cmdQueue, &cmd, pdMS_TO_TICKS(10));
                if (qResult == pdPASS) {
                    Serial.printf("[WS] Komut kuyruğa eklendi (action: %d)\n", cmd.action);
                } else {
                    Serial.println("[WS][HATA] Kuyruk DOLU! Komut atıldı.");
                }
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

    String payload;
    serializeJson(doc, payload);

    bool sent = webSocket.sendTXT("42[\"telemetry\"," + payload + "]");
    if (!sent) {
        Serial.println("[GCS][HATA] Telemetri gönderilemedi! WebSocket kopuk mu?");
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

        // Her 50 pakette bir buffer doluluk oranını logla
        if (bufferCount % 50 == 0) {
            Serial.printf("[BBOX] Buffer doluluk: %d/%d paket\n", bufferCount, MAX_OFFLINE_PACKETS);
        }
    } else {
        Serial.println("[GCS][HATA] WS bağlı değil VE buffer null! Telemetri KAYBOLDU.");
    }
}

// ─────────────────────────────────────────────
void networkTask(void* parameters) {
    Serial.println("[NET] networkTask başladı (Core 1).");
    Serial.printf("[NET] Serbest heap: %d | Serbest PSRAM: %d\n",
        ESP.getFreeHeap(), ESP.getFreePsram());

    initBlackBox();
    initFOTA();

    Serial.println("[NET] Statik IP ayarlanıyor...");
    IPAddress local_IP(10, 172, 218, 100);
    IPAddress gateway(10, 172, 218, 3);
    IPAddress subnet(255, 255, 255, 0);
    
    WiFi.mode(WIFI_STA); 
    delay(100); 
    
    if (!WiFi.config(local_IP, gateway, subnet)) {
        Serial.println("[NET][HATA] Statik IP ayarlanamadı!");
    } else {
        Serial.println("[NET] Statik IP ayarlandı: 192.168.43.100");
    }

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
    
    
    
    webSocket.begin(SERVER_IP, SERVER_PORT, "/socket.io/?EIO=4&transport=websocket");
    webSocket.onEvent(onWebSocketEvent);

    Serial.println("[NET] Ana döngü başlıyor...");

    uint32_t lastStatusLog = 0;

    for (;;) {
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

        // Buffer'dan offline veriyi flush et
        if (WiFi.status() == WL_CONNECTED && webSocket.isConnected() && bufferCount > 0) {
            Serial.printf("[NET] Offline buffer flush: %d paket bekliyor\n", bufferCount);
            portENTER_CRITICAL(&bufferMux);
            TelemetryPacket old = offlineBuffer[bufferTail];
            bufferTail = (bufferTail + 1) % MAX_OFFLINE_PACKETS;
            bufferCount--;
            portEXIT_CRITICAL(&bufferMux);

            transmitToGCS(old);
            vTaskDelay(pdMS_TO_TICKS(5));
        }

        vTaskDelay(pdMS_TO_TICKS(1));
    }
}