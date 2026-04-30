#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>

// WiFi 
const char* const SSID_NAME = "huseyin";
const char* const PASSWORD  = "12345677";

// Server 
const char* const SERVER_IP = "10.172.218.138";
const uint16_t SERVER_PORT   = 8080;

// UART Pin Definitions
#define RX2_PIN 16
#define TX2_PIN 17

#define BOOT0_PIN   40
#define NRST_PIN    41

#define MAX_OFFLINE_PACKETS 5000


extern TaskHandle_t networkTaskHandle;
extern TaskHandle_t commTaskHandle;

#endif