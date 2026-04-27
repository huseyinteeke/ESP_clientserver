#ifndef COMM_MANAGER_H
#define COMM_MANAGER_H

#include <Arduino.h>

// Core 0'da çalışacak olan UART Haberleşme Task'ı
void commTask(void* parameters);

#endif