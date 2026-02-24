// Include mocks first to set include guards
#include "StepperMotorController.h"
#include "MotorCommandQueue.h"
#include "ConfigurationManager.h"

#include <unity.h>
#include <ArduinoJson.h>
#include "MQTTController.h"
#include <string>
#include <cstring>

// ── Shared test fixtures ──────────────────────────────────────────────────────

static MQTTController ctrl;
static StepperMotorController mockStepper;
static MotorCommandQueue mockQueue;

void setUp() {
    mockStepper.clearCalls();
    mockQueue.clearCommands();
    ctrl.stepperController = &mockStepper;
    ctrl.commandQueue = &mockQueue;
    ctrl.mqttClient._setConnected(true);
    ctrl.mqttClient._clearPublishes();
    ctrl.config.deviceId = "t1";
    ctrl.config.qosCommands = 1;
    ctrl.pendingMoveComplete = false;
    ctrl.pendingMoveCommandType[0] = '\0';
    ctrl.pendingMoveRequestId[0] = '\0';
    ctrl.buildTopics();
    // Initialize lastState to match mockStepper defaults
    ctrl.updateState();
}

void tearDown() {}

static void sendCommand(const char* json) {
    ctrl.handleCommand(json, strlen(json));
}

// ── hasStateChanged: boolean fields ─────────────────────────────────────────

void test_state_no_change(void) {
    // State matches mock defaults - no change expected
    TEST_ASSERT_FALSE(ctrl.hasStateChanged());
}

void test_state_enabled_change(void) {
    mockStepper._enabled = false;
    TEST_ASSERT_TRUE(ctrl.hasStateChanged());
}

void test_state_running_change(void) {
    mockStepper._running = true;
    TEST_ASSERT_TRUE(ctrl.hasStateChanged());
}

void test_state_position_change(void) {
    mockStepper._position = 100;
    TEST_ASSERT_TRUE(ctrl.hasStateChanged());
}

void test_state_behavior_change(void) {
    mockStepper._behaviorInProgress = true;
    TEST_ASSERT_TRUE(ctrl.hasStateChanged());
}

void test_state_dance_change(void) {
    mockStepper._danceInProgress = true;
    TEST_ASSERT_TRUE(ctrl.hasStateChanged());
}

// ── hasStateChanged: float thresholds ───────────────────────────────────────

void test_state_positiondegrees_below_threshold(void) {
    mockStepper._positionDegrees = 0.05f;  // below 0.1 threshold
    TEST_ASSERT_FALSE(ctrl.hasStateChanged());
}

void test_state_positiondegrees_above_threshold(void) {
    mockStepper._positionDegrees = 0.2f;  // above 0.1 threshold
    TEST_ASSERT_TRUE(ctrl.hasStateChanged());
}

void test_state_gearratio_below_threshold(void) {
    mockStepper._gearRatio = 1.005f;  // below 0.01 threshold
    TEST_ASSERT_FALSE(ctrl.hasStateChanged());
}

void test_state_gearratio_above_threshold(void) {
    mockStepper._gearRatio = 1.02f;  // above 0.01 threshold
    TEST_ASSERT_TRUE(ctrl.hasStateChanged());
}

// ── Move-complete flag management ───────────────────────────────────────────

void test_stopmove_clears_pending_move(void) {
    sendCommand(R"({"command":"move","joint":"turntable","position":90.0,"request_id":"r1"})");
    TEST_ASSERT_TRUE(ctrl.pendingMoveComplete);
    ctrl.mqttClient._clearPublishes();
    sendCommand(R"({"command":"stopmove"})");
    TEST_ASSERT_FALSE(ctrl.pendingMoveComplete);
    TEST_ASSERT_EQUAL_STRING("", ctrl.pendingMoveCommandType);
    TEST_ASSERT_EQUAL_STRING("", ctrl.pendingMoveRequestId);
}

void test_forcestop_clears_pending_move(void) {
    sendCommand(R"({"command":"move","joint":"turntable","heading":180.0,"request_id":"r2"})");
    TEST_ASSERT_TRUE(ctrl.pendingMoveComplete);
    ctrl.mqttClient._clearPublishes();
    sendCommand(R"({"command":"forcestop"})");
    TEST_ASSERT_FALSE(ctrl.pendingMoveComplete);
    TEST_ASSERT_EQUAL_STRING("", ctrl.pendingMoveCommandType);
    TEST_ASSERT_EQUAL_STRING("", ctrl.pendingMoveRequestId);
}

// ── Main ─────────────────────────────────────────────────────────────────────

int main(int, char**) {
    UNITY_BEGIN();

    // hasStateChanged: boolean fields
    RUN_TEST(test_state_no_change);
    RUN_TEST(test_state_enabled_change);
    RUN_TEST(test_state_running_change);
    RUN_TEST(test_state_position_change);
    RUN_TEST(test_state_behavior_change);
    RUN_TEST(test_state_dance_change);

    // hasStateChanged: float thresholds
    RUN_TEST(test_state_positiondegrees_below_threshold);
    RUN_TEST(test_state_positiondegrees_above_threshold);
    RUN_TEST(test_state_gearratio_below_threshold);
    RUN_TEST(test_state_gearratio_above_threshold);

    // Move-complete flag management
    RUN_TEST(test_stopmove_clears_pending_move);
    RUN_TEST(test_forcestop_clears_pending_move);

    return UNITY_END();
}
