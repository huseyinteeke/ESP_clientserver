#include "CommManager.h"
#include "NetworkManager.h"
#include "Protocol.h"
#include "Config.h"

void commTask(void* parameters) {
    // UART2 konfigürasyonu (Config.h içerisindeki pinlerle)
    // S3-N8 üzerinde donanımsal UART birimini başlatıyoruz
    Serial2.begin(115200, SERIAL_8N1, RX2_PIN, TX2_PIN);
    
    ControlPacket outgoingCmd;
    TelemetryPacket incomingData;
    
    Serial.println("[Comm] STM32 UART Task'ı Başlatıldı (Core 0).");

    for(;;) {
        // --- GCS=>STM32---
        if (xQueueReceive(cmdQueue, &outgoingCmd, 0) == pdPASS) {
            Serial2.write((uint8_t*)&outgoingCmd, sizeof(ControlPacket));
            Serial.printf("[Comm] STM32'ye Komut Basıldı: ID %d\n", outgoingCmd.action);
        }

        // --- STM32 -> GCS ---
        if (Serial2.available() >= sizeof(TelemetryPacket)) {
            size_t readLen = Serial2.readBytes((uint8_t*)&incomingData, sizeof(TelemetryPacket));
            
            if (readLen == sizeof(TelemetryPacket)) {
                sendTelemetryToGCS(incomingData);
            }
        }

        vTaskDelay(pdMS_TO_TICKS(1)); 
    }
}