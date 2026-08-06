#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <Arduino.h>





typedef struct __attribute__((packed)){
    uint16_t header;
    uint32_t timestamp;
    float depth, ax, ay, az, pitch, roll, yaw , velocityx , velocityy , velocityz , distancex , distancey , distancez , rpm , rudderangle , sternangle;
    uint16_t footer;
} TelemetryPacket;





typedef enum
{
  TURN  = 0,
  DEPTH , 
  GO_TO , 
  SYSTEM_RESET , 
  ARM , 
  DISARM , 
  YUNUSLAMA
} Command_t;

typedef struct __attribute((packed))
{
  Command_t command;
  int16_t value;
}CommandData_t;

extern QueueHandle_t cmdQueue;



#endif