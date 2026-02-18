#ifndef ARDUINO_H_MOCK
#define ARDUINO_H_MOCK

#include <cstdint>
#include <cstddef>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <string>
#include <algorithm>

// Arduino type aliases
typedef uint8_t byte;
typedef bool boolean;

// GPIO defines (unused in tests but required for compilation)
#define INPUT 0
#define OUTPUT 1
#define HIGH 1
#define LOW 0

// Common Arduino macros
#define PI 3.14159265358979323846
#define constrain(amt,low,high) ((amt)<(low)?(low):((amt)>(high)?(high):(amt)))
#define min(a,b) ((a)<(b)?(a):(b))
#define max(a,b) ((a)>(b)?(a):(b))

// Forward declare
unsigned long millis();
void delay(unsigned long ms);
void pinMode(int pin, int mode);
void digitalWrite(int pin, int value);
int digitalRead(int pin);

// Print base class (required by ArduinoJson)
class Print {
public:
    virtual ~Print() {}
    virtual size_t write(uint8_t) { return 1; }
    virtual size_t write(const uint8_t* buf, size_t size) {
        for (size_t i = 0; i < size; i++) write(buf[i]);
        return size;
    }
    size_t write(const char* s) { return write((const uint8_t*)s, strlen(s)); }
    size_t print(const char*) { return 0; }
    size_t println(const char*) { return 0; }
    size_t println() { return 0; }
};

// Arduino String class (simplified)
class String {
public:
    String() : _buf() {}
    String(const char* s) : _buf(s ? s : "") {}
    String(const String& o) : _buf(o._buf) {}
    String(int val) : _buf(std::to_string(val)) {}
    String(float val) : _buf(std::to_string(val)) {}

    String& operator=(const String& o) { _buf = o._buf; return *this; }
    String& operator=(const char* s) { _buf = s ? s : ""; return *this; }

    const char* c_str() const { return _buf.c_str(); }
    unsigned int length() const { return (unsigned int)_buf.length(); }
    bool isEmpty() const { return _buf.empty(); }

    bool operator==(const String& o) const { return _buf == o._buf; }
    bool operator==(const char* s) const { return _buf == s; }
    bool operator!=(const String& o) const { return _buf != o._buf; }
    bool operator!=(const char* s) const { return _buf != s; }

    String operator+(const String& o) const { return String((_buf + o._buf).c_str()); }
    String operator+(const char* s) const { return String((_buf + s).c_str()); }
    String& operator+=(const String& o) { _buf += o._buf; return *this; }
    String& operator+=(const char* s) { _buf += s; return *this; }
    String& operator+=(char c) { _buf += c; return *this; }

    // ArduinoJson compatibility
    bool concat(const char* s) { if (s) _buf += s; return true; }
    bool concat(const char* s, size_t len) { if (s) _buf.append(s, len); return true; }
    bool concat(char c) { _buf += c; return true; }
    void reserve(unsigned int size) { _buf.reserve(size); }

    void toLowerCase() {
        std::transform(_buf.begin(), _buf.end(), _buf.begin(), ::tolower);
    }

    char charAt(unsigned int index) const { return _buf[index]; }
    int indexOf(char c) const { auto p = _buf.find(c); return p == std::string::npos ? -1 : (int)p; }

    // Conversion
    long toInt() const { return std::atol(_buf.c_str()); }
    float toFloat() const { return std::atof(_buf.c_str()); }

    // Iterator support (for ArduinoJson)
    const char* begin() const { return _buf.c_str(); }
    const char* end() const { return _buf.c_str() + _buf.size(); }

private:
    std::string _buf;
};

// Serial stub
class SerialClass {
public:
    void begin(unsigned long) {}
    void println(const char*) {}
    void println(const String&) {}
    void println(int) {}
    void println() {}
    void print(const char*) {}
    void print(const String&) {}
    void print(int) {}
    void printf(const char*, ...) {}
    int available() { return 0; }
    int read() { return -1; }
    void flush() {}
};

extern SerialClass Serial;

// ESP class stub
class EspClass {
public:
    uint32_t getFreeHeap() { return 200000; }
    void restart() {}
};

extern EspClass ESP;

// IPAddress class stub
class IPAddress {
public:
    IPAddress() : _addr(0) {}
    IPAddress(uint8_t a, uint8_t b, uint8_t c, uint8_t d) : _addr((uint32_t)a | ((uint32_t)b << 8) | ((uint32_t)c << 16) | ((uint32_t)d << 24)) {}
    String toString() const { return String("192.168.1.100"); }
private:
    uint32_t _addr;
};

#endif // ARDUINO_H_MOCK
