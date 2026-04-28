#include "NetworkManager.h"
#include <ArduinoJson.h>
#include "Config.h"

WebSocketsClient webSocket;
WebServer server(80);


TelemetryPacket* offlineBuffer = nullptr;
int bufferHead = 0;
int bufferTail = 0;
int bufferCount = 0;
portMUX_TYPE bufferMux = portMUX_INITIALIZER_UNLOCKED;


void initFOTA()
{
    if(!LittleFS.begin(true))
    {
        Serial.println("[ERROR] LittleFS fail");
        return;
    }

    server.on("/")

}











void initBlackBox() {
    offlineBuffer = (TelemetryPacket*)ps_malloc(MAX_OFFLINE_PACKETS * sizeof(TelemetryPacket));
    if (offlineBuffer == NULL) {
        Serial.println("[HATA] RAM tahsisi basarisiz!");
    } else {
        Serial.println("[Sistem] SRAM Kara Kutu aktif.");
    }
}

void onWebSocketEvent(WStype_t type, uint8_t * payload, size_t length) {
    if (type == WStype_CONNECTED) {
        webSocket.sendTXT("40");
    } else if (type == WStype_TEXT) {
        String text = (char*)payload;
        if (text.indexOf("\"device_command\"") != -1 || text.indexOf("\"ui_command\"") != -1) {
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
            // Diğer JSON verilerini burada doldurabilirsin
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
        webSocket.loop();
        server.handleClient();
        if (WiFi.status() == WL_CONNECTED && webSocket.isConnected() && bufferCount > 0) {
            portENTER_CRITICAL(&bufferMux);
            TelemetryPacket old = offlineBuffer[bufferTail];
            bufferTail = (bufferTail + 1) % MAX_OFFLINE_PACKETS;
            bufferCount--;
            portEXIT_CRITICAL(&bufferMux);
            transmitToGCS(old);
            vTaskDelay(pdMS_TO_TICKS(5));
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}