// Wrapper that compiles MQTTController.cpp with mock dependencies.
// Including mock headers first sets their include guards, so when
// the real MQTTController.h tries to #include "StepperMotorController.h" etc.,
// the guards are already defined and the real headers are skipped.

#include "StepperMotorController.h"   // mock (via -I test/native/mocks)
#include "MotorCommandQueue.h"        // mock
#include "ConfigurationManager.h"     // mock

// Now include the real implementation - its #includes of the above
// headers will be no-ops due to the already-defined include guards.
#include "../../../src/MQTTController.cpp"
