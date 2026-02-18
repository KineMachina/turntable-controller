#ifndef PREFERENCES_H_MOCK
#define PREFERENCES_H_MOCK

#include "Arduino.h"

class Preferences {
public:
    bool begin(const char*, bool = false) { return true; }
    void end() {}
    bool putString(const char*, const char*) { return true; }
    bool putUShort(const char*, uint16_t) { return true; }
    bool putUChar(const char*, uint8_t) { return true; }
    bool putBool(const char*, bool) { return true; }
    bool putFloat(const char*, float) { return true; }
    String getString(const char*, const char* def = "") { return String(def); }
    uint16_t getUShort(const char*, uint16_t def = 0) { return def; }
    uint8_t getUChar(const char*, uint8_t def = 0) { return def; }
    bool getBool(const char*, bool def = false) { return def; }
    float getFloat(const char*, float def = 0.0f) { return def; }
};

#endif // PREFERENCES_H_MOCK
