#ifndef WIFI_H_MOCK
#define WIFI_H_MOCK

#include "Arduino.h"

#define WL_CONNECTED 3
#define WL_DISCONNECTED 6

class WiFiClass {
public:
    int _status = WL_CONNECTED;

    int status() { return _status; }
    IPAddress localIP() { return IPAddress(192, 168, 1, 100); }
};

extern WiFiClass WiFi;

#endif // WIFI_H_MOCK
