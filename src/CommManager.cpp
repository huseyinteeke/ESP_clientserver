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
        // --- 1. GCS'DEN GELEN KOMUTLARI STM32'YE İLET ---
        // Kuyrukta bekleyen komut var mı? (Beklemeden kontrol et)
        if (xQueueReceive(cmdQueue, &outgoingCmd, 0) == pdPASS) {
            // Struct'ı doğrudan binary olarak UART hattına basıyoruz
            Serial2.write((uint8_t*)&outgoingCmd, sizeof(ControlPacket));
            
            // Hata ayıklama logu
            Serial.printf("[Comm] STM32'ye Komut Basıldı: ID %d\n", outgoingCmd.action);
        }

        // --- 2. STM32'DEN GELEN TELEMETRİYİ YAKALA ---
        // Buffer'da en az bir tam paket (yaklaşık 40 byte) var mı?
        if (Serial2.available() >= sizeof(TelemetryPacket)) {
            // Gelen binary akışını doğrudan struct belleğine oku
            size_t readLen = Serial2.readBytes((uint8_t*)&incomingData, sizeof(TelemetryPacket));
            
            if (readLen == sizeof(TelemetryPacket)) {
                // NetworkManager'daki 'Akıllı Yönlendirici'ye gönder
                // Bu fonksiyon artık Tek Parametre (Struct) alıyor
                sendTelemetryToGCS(incomingData);
            }
        }

        // RTOS'un diğer çekirdek işlerini halletmesi ve Watchdog beslemesi için
        vTaskDelay(pdMS_TO_TICKS(1)); 
    }
}