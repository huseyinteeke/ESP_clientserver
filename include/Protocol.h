#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <Arduino.h>



typedef struct __attribute__((packed)){
    uint8_t action; //0: NONE, 1: ARM, 2: DISARM, 3: FOTA, 4: PID_UPDATE
    float kp_p, ki_p, kd_p; 
    float kp_y, ki_y, kd_y; 
    float set_p , set_y;
} ControlPacket;



typedef struct __attribute__((packed)){
    uint16_t header;
    uint32_t timestamp;
    float depth, ax, ay, az, pitch, roll, yaw , velocity , distance;
    uint16_t footer;
} TelemetryPacket;


extern QueueHandle_t cmdQueue;



#endif