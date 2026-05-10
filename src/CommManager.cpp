#include "CommManager.h"
#include "NetworkManager.h"
#include "Protocol.h"
#include "Config.h"

void commTask(void* parameters) {

    Serial2.begin(115200, SERIAL_8E1, RX2_PIN, TX2_PIN);
    Serial2.setRxBufferSize(1024);
    ControlPacket outgoingCmd;
    TelemetryPacket incomingData;
    uint8_t rxBuffer[sizeof(TelemetryPacket)];
    Serial.println("[Comm] STM32 UART Task'ı Başlatıldı (Core 0).");

    for(;;) {
        // --- GCS=>STM32---
        if (xQueueReceive(cmdQueue, &outgoingCmd, 0) == pdPASS) {
            Serial2.write((uint8_t*)&outgoingCmd, sizeof(ControlPacket));
            vTaskDelay(500);
            Serial.printf("[Comm] STM32'ye Komut Basıldı: ID %d\n", outgoingCmd.action);
        }

        // --- STM32 -> GCS ---
        if (Serial2.available() >= sizeof(TelemetryPacket)) {
            uint8_t byte1 = Serial2.read();
            if(byte1 == 0xBB)
            {
            uint8_t byte2 = Serial2.peek();
            
            if(byte2 == 0xAA)
            {
                rxBuffer[0] = byte1;
                Serial2.readBytes(&rxBuffer[1], sizeof(TelemetryPacket) - 1);
                memcpy(&incomingData , rxBuffer , sizeof(TelemetryPacket));

                if(incomingData.footer == 0xCCDD) {
                 
                 sendTelemetryToGCS(incomingData);
             }
            }
            
        }
    }

        vTaskDelay(pdMS_TO_TICKS(1)); 
    }
}