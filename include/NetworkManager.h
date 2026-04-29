#ifndef NETWORK_MANAGER_H
#define NETWORK_MANAGER_H

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <WebSocketsClient.h>
#include <WebServer.h>
#include <LittleFS.h>

#include "Protocol.h"


extern WebServer server;
extern WebSocketsClient webSocket;

void initNetwork();
void initFOTA();
void networkTask(void* parameters);
void sendTelemetryToGCS(const TelemetryPacket& data);

#endif