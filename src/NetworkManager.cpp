#include "NetworkManager.h"
#include <ArduinoJson.h>
#include "Config.h"
#include "CRC32.h"


WebSocketsClient webSocket;
TelemetryPacket* offlineBuffer = nullptr;
int bufferHead = 0;
int bufferTail = 0;
int bufferCount = 0;
portMUX_TYPE bufferMux = portMUX_INITIALIZER_UNLOCKED;
static int8_t isArmed = 0;
bool startLogDownload = false;

extern QueueHandle_t telemetryQueue;



WebServer server(80);
static File fotaFile;



bool waitForAck(uint32_t timeout_ms = 1000) {
    Serial2.flush();
    uint32_t timeout = millis();
    while (millis() - timeout < timeout_ms) {
        if (Serial2.available()) {
            uint8_t response = Serial2.read();
            if (response == 0x79) {
                return true;  // ACK
            }
           else if (response == 0x1F) {
                    // STM32 isyan bayrağını çekti! Anında işlemi iptal et.
                    Serial.println("[FOTA][HATA] STM32 NACK (0x1F) dönderdi! Veri eksik veya bozuk.");
                    return false; 
                } 
                else {
                    // Hataya düşmese bile hattan serseri bir byte gelirse görelim (Debug için efsanedir)
                    Serial.printf("[FOTA][UYARI] Beklenmeyen byte alındı: 0x%02X\n", response);
                }
                yield();
    }
}
    Serial.println("[FOTA] STM32 Timeout: Cevap gelmedi.");
    return false;
}

//0xA1: Delete code
bool stm32GlobalErase() {
    Serial.println("[FOTA] Flash siliniyor...");

    Serial2.write(0xA1);

    if(!waitForAck(1000)) return false; 
    return waitForAck(15000); 
}


uint8_t calculateChecksum(uint8_t* data, uint32_t len) {
    uint8_t crc = 0;
    for (uint32_t i = 0; i < len; i++) {
        crc ^= data[i];
    }
    return crc;
}


bool WriteChunk(uint8_t* pData , uint16_t len)
{
    Serial2.write(0xA2);
    if(!waitForAck(1000)) { Serial.println("[FOTA] HATA: 0xA2 komut ACK'si gelmedi!"); return false; }

    uint8_t nMinusOne = (uint8_t)(len - 1);
    Serial2.write(nMinusOne);
    if(!waitForAck(1000)) { Serial.println("[FOTA] HATA: Uzunluk bilgisi ACK'si gelmedi!"); return false; }
    
    Serial.println("\n[ANALİZ] Gönderim Öncesi:");
    Serial.printf("   > RX Buffer'da bekleyen: %d byte\n", Serial2.available());
    
    delay(10); 
    Serial.println("[FOTA] 256 Byte Paket Basılıyor...");
    Serial2.write(pData , len);

    Serial2.flush(); 
    Serial.println("[FOTA] Donanımsal Flush Tamamlandı.");
    Serial.printf("   > RX Buffer Durumu: %d byte\n", Serial2.available());
    if(Serial2.available() > 0) {
        Serial.printf("   > Hat boş değil! Gelen ilk byte: 0x%02X\n", Serial2.peek());
    }
    if(!waitForAck(3000)) { 
        Serial.println("[FOTA] HATA: Flash'a yazma bitiş ACK'si gelmedi!"); 
        Serial.printf("[ANALİZ] Kritik Hata Anı RX Buffer: %d\n", Serial2.available());
        return false; 
    }
    return true;
}




bool JumpToApp()
{
    Serial.println("[FOTA] completed , main code");
    Serial2.write(0xA3);
    return waitForAck(1000);
}




bool enterSTM32Bootloader()
{
    pinMode(NRST_PIN  , OUTPUT);
    digitalWrite(NRST_PIN , LOW);
    vTaskDelay(pdMS_TO_TICKS(100));

    while(Serial2.available()) Serial2.read(); // Clean bus
    
    digitalWrite(NRST_PIN , HIGH);
    vTaskDelay(pdMS_TO_TICKS(50));

    uint32_t tStart = millis();
    while(millis() - tStart < 600) {
        Serial2.write(0x7F); 
        
        vTaskDelay(pdMS_TO_TICKS(15)); 
        
        while (Serial2.available()) {
            uint8_t res = Serial2.read();
            if (res == 0x79) { // ACK yakalandı!
                return true;
            }
        }
    }
    return false;
    
}


void startFotaTransfer()
{
    File file =LittleFS.open("/stm32_fw.bin" , "r");
    uint8_t chunk[256];

    if (commTaskHandle != NULL)
    {
        vTaskSuspend(commTaskHandle);
        Serial.println("[FOTA] Comm Task askıya alındı. Queue'dan veri çekimi durduruldu.");
    }

    vTaskDelay(pdMS_TO_TICKS(50)); 
    while(Serial2.available()) Serial2.read(); //Empty the line


    if (!enterSTM32Bootloader()) { 
        if (commTaskHandle) vTaskResume(commTaskHandle);
        file.close();
        return;
    }

    if (!stm32GlobalErase()) {
        Serial.println("[FOTA] Flash silme başarısız!");
        if(commTaskHandle) vTaskResume(commTaskHandle);
        file.close();
        return;
    }

    size_t totalWritten = 0;
    size_t fileSize = file.size();
    while(file.available())
    {
        size_t bytesRead = file.read(chunk, sizeof(chunk));

        if(!WriteChunk(chunk , bytesRead))
        {
            Serial.printf("[FOTA][HATA] Yazma işlemi %d. byte'da koptu!\n", totalWritten);
            break;
        }

        totalWritten += bytesRead;

        if (totalWritten % 1024 == 0 || totalWritten == fileSize) {
            Serial.printf("[FOTA] İlerleme: %d%%\n", (totalWritten * 100) / fileSize);
        }
    }

    if(totalWritten == fileSize)
    {
        JumpToApp();
    }
    else {
         Serial.println("[FOTA][HATA] Eksik yazım yapıldı.");
    }
    if (commTaskHandle) vTaskResume(commTaskHandle);
    file.close();
}



uint32_t calculateIEEECRC32(File &f) {
    uint32_t crc = 0xFFFFFFFF;
    f.seek(0); // Dosya başına dön
    
    while (f.available()) {
        uint8_t b = f.read();
        crc ^= b;
        for (int j = 0; j < 8; j++) {
            if (crc & 1)
                crc = (crc >> 1) ^ 0xEDB88320;
            else
                crc >>= 1;
        }
    }
    return (crc ^ 0xFFFFFFFF);
}



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
    uint8_t buffer[1024];
    size_t byteCount = 0;
    f.seek(0);
    while (f.available()) {
        size_t n = f.read(buffer , sizeof(buffer));
        for (size_t i = 0; i < n; i++) {
            crc.update(buffer[i]);
        }
    }
    f.close();

    Serial.printf("[FOTA] Toplam okunan byte: %d\n", byteCount);

    uint32_t calculatedCrc = crc.finalize();
    Serial.printf("[FOTA] Hesaplanan CRC: %08X\n", calculatedCrc);

    if (calculatedCrc == expectedCrc) {
        Serial.println("[FOTA] BAŞARILI! CRC eşleşti. STM32'ye aktarım başlayacak.");
        startFotaTransfer();
    } else {
        Serial.printf("[FOTA][HATA] CRC UYUŞMUYOR! Beklenen: %08X | Hesaplanan: %08X\n", expectedCrc, calculatedCrc);
        LittleFS.remove("/stm32_fw.bin");
    }
}

// ─────────────────────────────────────────────
void initFOTA() {
    const char * headerkeys[] = {"X-File-CRC"} ;
    size_t headerkeyssize = sizeof(headerkeys)/sizeof(char*);
    server.collectHeaders(headerkeys, headerkeyssize);

    if (!LittleFS.begin(true)) {
        Serial.println("[FOTA][ERR] LittleFS mount");
        return;
    }

    if (LittleFS.exists("/stm32_fw.bin")) {
        File existing = LittleFS.open("/stm32_fw.bin", FILE_READ);
        Serial.printf("[FOTA]: %d byte\n", existing.size());
        existing.close();
    }
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
        //Serial.println("[FOTA][HTTP] POST tamamlandı, response gönderiliyor.");
        server.send(200, "text/plain", "OK: File at ESP32. STM32 transfer part.");
    }, []() {
        HTTPUpload& upload = server.upload();

        if (upload.status == UPLOAD_FILE_START) {
            Serial.printf("[FOTA][UPLOAD] Dosya adı: %s\n", upload.filename.c_str());
            LittleFS.remove("/stm32_fw.bin");
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
                    portENTER_CRITICAL(&bufferMux);
                    bufferHead = 0;
                    bufferTail = 0;
                    bufferCount = 0;
                    isArmed = 1;
                    portEXIT_CRITICAL(&bufferMux);
                }
                else if (strcmp(action, "DISARM") == 0){
                    cmd.action = 2;
                    isArmed = 0;
                }
                else if (strcmp(action, "RESET") == 0){
                    cmd.action = 3;
                }
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
                    startLogDownload = true;
                    Serial.println("[BBOX] Log indirme isteği alındı, aktarım başlatılıyor...");
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
    if (WiFi.status() == WL_CONNECTED &&  webSocket.isConnected()) {
        xQueueSend(telemetryQueue , &data , 0);
    } 
    
    if (isArmed == 1 &&  offlineBuffer != nullptr) {
        portENTER_CRITICAL(&bufferMux);
        offlineBuffer[bufferHead] = data;
        bufferHead = (bufferHead + 1) % MAX_OFFLINE_PACKETS;
        if (bufferCount < MAX_OFFLINE_PACKETS) bufferCount++;
        else bufferTail = (bufferTail + 1) % MAX_OFFLINE_PACKETS;
        portEXIT_CRITICAL(&bufferMux);
    }
}

// ─────────────────────────────────────────────
void networkTask(void* parameters) {
    initBlackBox();
    initFOTA();

    IPAddress local_IP(10, 17 , 15, 101);
    IPAddress gateway(10, 17 , 15 , 1);
    IPAddress subnet(255, 255, 255, 0);
    WiFi.config(local_IP, gateway, subnet);
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

        TelemetryPacket liveData;
        if (xQueueReceive(telemetryQueue, &liveData, 0) == pdPASS) {
            transmitToGCS(liveData);
        }

        if (millis() - lastStatusLog > 5000) {
            lastStatusLog = millis();
            Serial.printf("[NET] Durum → WiFi:%s | WS:%s | Buffer:%d | Heap:%d\n",
                WiFi.status() == WL_CONNECTED ? "OK" : "KOPUK",
                webSocket.isConnected() ? "OK" : "KOPUK",
                bufferCount,
                ESP.getFreeHeap());
        }

        if(startLogDownload)
        {
            if (bufferCount == 0) {
                webSocket.sendTXT("42[\"log_status\", \"Buffer boş\"]");
                startLogDownload = false;
            } else {
                int batch = (bufferCount > 5) ? 5 : bufferCount;
                
                for(int i = 0; i < batch; i++) {
                    portENTER_CRITICAL(&bufferMux);
                    TelemetryPacket oldData = offlineBuffer[bufferTail];
                    bufferTail = (bufferTail + 1) % MAX_OFFLINE_PACKETS;
                    bufferCount--;
                    portEXIT_CRITICAL(&bufferMux);

                    static  JsonDocument doc;
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
                    serializeJson(doc , payload);

                    webSocket.sendTXT("42[\"log_data\"," + payload + "]");
                }

                if (bufferCount == 0) {
                    webSocket.sendTXT("42[\"log_status\", \"Tamamlandı\"]");
                    startLogDownload = false;
                    Serial.println("[BBOX] Tüm offline loglar başarıyla arayüze aktarıldı.");
                }
            }
        }

        vTaskDelay(pdMS_TO_TICKS(3));
    
}
}