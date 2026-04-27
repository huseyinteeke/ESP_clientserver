#ifndef NETWORK_MANAGER_H
#define NETWORK_MANAGER_H

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <WebSocketsClient.h>
#include "Protocol.h"

void networkTask(void* parameters);
// Fonksiyon artık sadece struct alacak şekilde güncellendi
void sendTelemetryToGCS(const TelemetryPacket& data);

#endif