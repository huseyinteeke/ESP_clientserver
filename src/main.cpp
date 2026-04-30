
#include <Arduino.h>
#include <WiFi.h>
#include <Config.h>
#include <Protocol.h>
extern void networkTask(void* paramaters);
extern void commTask(void* parameters);

TaskHandle_t networkTaskHandle = NULL;
TaskHandle_t commTaskHandle = NULL;
QueueHandle_t cmdQueue;
#include <Adafruit_NeoPixel.h>

#define RGB_PIN 48  
#define NUM_PIXELS 1   

Adafruit_NeoPixel pixel(NUM_PIXELS, RGB_PIN, NEO_GRB + NEO_KHZ800);

void setup() {
    Serial.begin(115200);
    delay(500);
    pixel.begin(); 
    cmdQueue = xQueueCreate(10, sizeof(ControlPacket));
    pixel.setPixelColor(0, 255, 0, 0); 
    pixel.show();
    if (cmdQueue != NULL) {
        // 2. Network Task (Core 1) - WiFi ve GCS işleri
        xTaskCreatePinnedToCore(
            networkTask, 
            "Net", 
            10240, 
            NULL, 
            2, 
            &networkTaskHandle, 
            1 
        );

        // STM32 UART işleri
        xTaskCreatePinnedToCore(
            commTask, 
            "Comm", 
            20480, 
            NULL, 
            3, 
            &commTaskHandle, 
            0 
        );
    }
}

void loop() {
    vTaskDelay(pdMS_TO_TICKS(10));
}

