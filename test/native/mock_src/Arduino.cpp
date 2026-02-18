#include "Arduino.h"
#include "WiFi.h"

SerialClass Serial;
EspClass ESP;
WiFiClass WiFi;

static unsigned long _millis_value = 0;

unsigned long millis() { return _millis_value; }
void delay(unsigned long) {}
void pinMode(int, int) {}
void digitalWrite(int, int) {}
int digitalRead(int) { return 0; }

// Test helper to advance millis
void _setMillis(unsigned long ms) { _millis_value = ms; }
