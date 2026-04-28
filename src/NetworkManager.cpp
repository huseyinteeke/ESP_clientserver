#include "NetworkManager.h"
#include <ArduinoJson.h>
#include "Config.h"
#include <CRC32.h>

WebSocketsClient webSocket;
WebServer server(80);


TelemetryPacket* offlineBuffer = nullptr;
int bufferHead = 0;
int bufferTail = 0;
int bufferCount = 0;
portMUX_TYPE bufferMux = portMUX_INITIALIZER_UNLOCKED;

void verifyAndCompleteFOTA() {
    String expectedCrcStr = server.header("X-File-CRC");
    uint32_t expectedCrc = strtoul(expectedCrcStr.c_str(), NULL, 16);

    File f = LittleFS.open("/stm32_fw.bin", FILE_READ);
    if (!f) return;

    CRC32 crc;
    while (f.available()) {
        crc.update(f.read());
    }
    f.close();

    uint32_t calculatedCrc = crc.finalize();

    if (calculatedCrc == expectedCrc) {
        Serial.printf("[FOTA] Basarili! CRC eslesti: %08X\n", calculatedCrc);
        // STM32 Upload ToDo
    } else {
        Serial.printf("[HATA] CRC Hatasi! Beklenen: %08X, Hesaplanan: %08X\n", expectedCrc, calculatedCrc);
        LittleFS.remove("/stm32_fw.bin"); 
    }
}


void initFOTA()
{
    if(!LittleFS.begin(true))
    {
        Serial.println("[ERROR] LittleFS fail");
        return;
    }

    server.on("/upload_firmware" , HTTP_POST , []() {
        Serial.println("[HTTP] POST Request Received"); 
        server.send(200, "text/plain", "OK: File at ESP32. STM32 transfer part.");
    } , []() {
        HTTPUpload& upload = server.upload();
        if (upload.status == UPLOAD_FILE_START) {
            Serial.printf("FOTA Starting: %s\n", upload.filename.c_str());
            // Eski dosyayı sil ve yeni dosya aç
            File f = LittleFS.open("/stm32_fw.bin", FILE_WRITE);
            f.close();
        } else if (upload.status == UPLOAD_FILE_WRITE) {
            File f = LittleFS.open("/stm32_fw.bin", FILE_APPEND);
            if (f) {
                f.write(upload.buf, upload.currentSize);
                f.close();
            }
        } else if (upload.status == UPLOAD_FILE_END) {
            Serial.printf("FOTA ok: %u byte\n", upload.totalSize);
            verifyAndCompleteFOTA();
        }

        server.begin();
        Serial.println("[SYS] FOTA Server Activated");
    });

}




void initBlackBox() {
    offlineBuffer = (TelemetryPacket*)ps_malloc(MAX_OFFLINE_PACKETS * sizeof(TelemetryPacket));
    if (offlineBuffer == NULL) {
        Serial.println("[HATA] psRAM tahsisi basarisi , normal RAM dan ayırılıyor.");
        offlineBuffer = (TelemetryPacket*)malloc(MAX_OFFLINE_PACKETS * sizeof(TelemetryPacket));
    }

    Serial.println(offlineBuffer ? "[SYS] BlackBOX activated." : "[ERROR] NoMemory!");
}

void onWebSocketEvent(WStype_t type, uint8_t * payload, size_t length) {
    if (type == WStype_CONNECTED) {
        webSocket.sendTXT("40");
    } else if (type == WStype_TEXT) {
        String text = (char*)payload;
        if (text.indexOf("{") != -1) {
            JsonDocument doc; 
            deserializeJson(doc, text.substring(text.indexOf('{'), text.lastIndexOf('}') + 1));
            
            ControlPacket cmd = {0};
            const char* action = doc["action"] | "";
            
            if (strcmp(action, "ARM") == 0) cmd.action = 1;
            else if (strcmp(action, "DISARM") == 0) cmd.action = 2;
            else if (strcmp(action, "RESET") == 0) cmd.action = 3;
            else if (strcmp(action, "PID_SETPOINT_UPDATE") == 0){
                cmd.action = 4;
                cmd.kp_p = doc["kppitch"] | 0.0f;
                cmd.ki_p = doc["kipitch"] | 0.0f;
                cmd.kd_p = doc["kdpitch"] | 0.0f;

                cmd.kp_y = doc["kppitch"] | 0.0f;
                cmd.ki_y = doc["kipitch"] | 0.0f;
                cmd.kd_y = doc["kdpitch"] | 0.0f;
                
                cmd.set_p = doc["setpitch"] | 0.0f;
                cmd.set_y = doc["setyaw"] | 0.0f;
                }
            xQueueSend(cmdQueue, &cmd, pdMS_TO_TICKS(10));
        }
    }
}

// 9 Grafiğin ve Telemetrinin Tam Listesi Burada
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
    webSocket.sendTXT("42[\"telemetry\"," + payload + "]");
}

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
    }
}

void networkTask(void* parameters) {
    initBlackBox();
    initFOTA();

    //Static IP configuration
    IPAddress local_IP(192, 168, 43, 100);
    IPAddress gateway(192, 168, 43, 1);
    IPAddress subnet(255, 255, 255, 0);

    if(!WiFi.config(local_IP, gateway, subnet)) {
        Serial.println("[Sys] Statik IP cannot established!");
    }


    WiFi.begin(SSID_NAME, PASSWORD);
    while (WiFi.status() != WL_CONNECTED) { vTaskDelay(pdMS_TO_TICKS(500)); }
    
    webSocket.begin(SERVER_IP, SERVER_PORT, "/socket.io/?EIO=4&transport=websocket");
    webSocket.onEvent(onWebSocketEvent);

    for(;;) {
        server.handleClient(); //For FOTA
        webSocket.loop(); //Listen for telemetry
        if (WiFi.status() == WL_CONNECTED && webSocket.isConnected() && bufferCount > 0) {
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