#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <Arduino.h>

#pragma pack(push, 1)


typedef struct {
    uint8_t action; //0: NONE, 1: ARM, 2: DISARM, 3: FOTA, 4: PID_UPDATE
    float kp_p, ki_p, kd_p, sp_p; 
    float kp_y, ki_y, kd_y, sp_y; 
} ControlPacket;



typedef struct {
    uint32_t timestamp;
    float depth, ax, ay, az, pitch, roll, yaw , velocity , distance;
} TelemetryPacket;


extern QueueHandle_t cmdQueue;
#pragma pack()


#endif