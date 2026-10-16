#pragma once
// ESP-IDF compatibility adapter, not part of upstream Settimino.
#include <cstdint>
#include <cstddef>
#include <cstring>
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
using byte = uint8_t;
using word = uint16_t;
struct IPAddress { uint32_t addr=0; };
inline unsigned long millis() { return esp_timer_get_time()/1000; }
inline void delayMicroseconds(unsigned int) { vTaskDelay(1); }
// Reuse upstream's WiFiClient construction path with an lwIP adapter.
#define S7WIFI
class Client {
    int fd=-1;
public:
    ~Client() { stop(); }
    int connect(IPAddress address,uint16_t port);
    void stop();
    int available();
    bool connected();
    void flush();
    size_t read(uint8_t *buffer,size_t size);
    size_t write(const uint8_t *buffer,size_t size);
};
using WiFiClient=Client;
extern char settimino_interface[8];
