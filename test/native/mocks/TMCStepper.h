#ifndef TMCSTEPPER_H_MOCK
#define TMCSTEPPER_H_MOCK

#include <cstdint>

class TMC2209Stepper {
public:
    TMC2209Stepper(void*, float, uint8_t) {}
    void begin() {}
};

#endif // TMCSTEPPER_H_MOCK
