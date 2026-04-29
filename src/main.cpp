
#include <Arduino.h>
#include <WiFi.h>
#include <Config.h>
#include <Protocol.h>
extern void networkTask(void* paramaters);
extern void commTask(void* parameters);

// Global Kuyruk Tanımı
QueueHandle_t cmdQueue;
#include <Adafruit_NeoPixel.h>

#define RGB_PIN 48     // ESP32-S3 DevKit dahili LED pini genellikle 48'dir
#define NUM_PIXELS 1   // Kartta 1 adet RGB LED bulunur

Adafruit_NeoPixel pixel(NUM_PIXELS, RGB_PIN, NEO_GRB + NEO_KHZ800);

void setup() {
    Serial.begin(115200);

    delay(5000);
    pixel.begin(); // LED'i başlat

    Serial.println("\n======= SARA SISTEMI V1.0 =======");

    // 1. İletişim Kuyruğunu Oluştur (10 paketlik yer ayır)
    cmdQueue = xQueueCreate(10, sizeof(ControlPacket));

    pixel.setPixelColor(0, 255, 0, 0); // Kırmızı renk (R=255, G=0, B=0)
    pixel.show();
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
            20480, 
            NULL, 
            3, // UART bizim için daha kritik, önceliği yüksek
            NULL, 
            0 
        );
    }
}

void loop() {
    // RTOS Taskları işi hallediyor, burayı boş bırakıyoruz.
    //webSocket.loop();
    vTaskDelay(pdMS_TO_TICKS(10));
}

