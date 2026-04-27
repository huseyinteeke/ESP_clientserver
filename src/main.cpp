#include <Arduino.h>
#include <WiFi.h>

#include "Protocol.h"
#include "NetworkManager.h"
#include "CommManager.h"

// Global Kuyruk Tanımı
QueueHandle_t cmdQueue;

void setup() {
    Serial.begin(115200);
    Serial.println("\n======= SARA SISTEMI V1.0 =======");

    // 1. İletişim Kuyruğunu Oluştur (10 paketlik yer ayır)
    cmdQueue = xQueueCreate(10, sizeof(ControlPacket));

    if (cmdQueue != NULL) {
        // 2. Network Task (Core 1) - WiFi ve GCS işleri
        xTaskCreatePinnedToCore(
            networkTask, 
            "Net", 
            10240, 
            NULL, 
            2, 
            NULL, 
            1 
        );

        // 3. Comm Task (Core 0) - STM32 UART işleri
        xTaskCreatePinnedToCore(
            commTask, 
            "Comm", 
            8192, 
            NULL, 
            3, // UART bizim için daha kritik, önceliği yüksek
            NULL, 
            0 
        );
    }
}

void loop() {
    // RTOS Taskları işi hallediyor, burayı boş bırakıyoruz.
    vTaskDelay(pdMS_TO_TICKS(1000));
}