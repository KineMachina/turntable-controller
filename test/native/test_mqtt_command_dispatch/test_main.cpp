// Include mocks first to set include guards before MQTTController.h
// pulls in real headers from src/
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
    ctrl.buildTopics();
}

void tearDown() {}

// Helper: send a command JSON and return the response topic publishes
static void sendCommand(const char* json) {
    ctrl.handleCommand(json, strlen(json));
}

// Helper: get last response payload as a string
static std::string lastResponsePayload() {
    auto& pubs = ctrl.mqttClient._getPublishes();
    for (int i = (int)pubs.size() - 1; i >= 0; i--) {
        if (pubs[i].topic.find("/response") != std::string::npos) {
            return pubs[i].payload;
        }
    }
    return "";
}

// Helper: check if last response was success
static bool lastResponseSuccess() {
    std::string payload = lastResponsePayload();
    return payload.find("\"status\":\"ok\"") != std::string::npos;
}

// Helper: check if last response was error
static bool lastResponseError() {
    std::string payload = lastResponsePayload();
    return payload.find("\"status\":\"error\"") != std::string::npos;
}

// ── Command dispatch: each command routes to the correct handler ─────────────

void test_dispatch_position(void) {
    sendCommand(R"({"command":"move","joint":"turntable","position":90.0})");
    TEST_ASSERT_TRUE(lastResponseSuccess());
    TEST_ASSERT_EQUAL(1, (int)mockQueue.getCommands().size());
    TEST_ASSERT_EQUAL(MotorCommandType::MOVE_TO, mockQueue.getCommands()[0].type);
}

void test_dispatch_heading(void) {
    sendCommand(R"({"command":"move","joint":"turntable","heading":180.0})");
    TEST_ASSERT_TRUE(lastResponseSuccess());
    TEST_ASSERT_TRUE(mockStepper.hasCall("moveToHeadingDegrees"));
}

void test_dispatch_enable(void) {
    sendCommand(R"({"command":"enable","enable":true})");
    TEST_ASSERT_TRUE(lastResponseSuccess());
    TEST_ASSERT_EQUAL(1, (int)mockQueue.getCommands().size());
    TEST_ASSERT_EQUAL(MotorCommandType::ENABLE, mockQueue.getCommands()[0].type);
}

void test_dispatch_speed(void) {
    sendCommand(R"({"command":"speed","speed":500.0})");
    TEST_ASSERT_TRUE(lastResponseSuccess());
    TEST_ASSERT_TRUE(mockStepper.hasCall("setMaxSpeed"));
}

void test_dispatch_acceleration(void) {
    sendCommand(R"({"command":"acceleration","accel":200.0})");
    TEST_ASSERT_TRUE(lastResponseSuccess());
    TEST_ASSERT_TRUE(mockStepper.hasCall("setAcceleration"));
}

void test_dispatch_microsteps(void) {
    sendCommand(R"({"command":"microsteps","microsteps":16})");
    TEST_ASSERT_TRUE(lastResponseSuccess());
    TEST_ASSERT_TRUE(mockStepper.hasCall("setMicrosteps"));
}

void test_dispatch_gearratio(void) {
    sendCommand(R"({"command":"gearratio","ratio":2.0})");
    TEST_ASSERT_TRUE(lastResponseSuccess());
    TEST_ASSERT_TRUE(mockStepper.hasCall("setGearRatio"));
}

void test_dispatch_speedhz(void) {
    sendCommand(R"({"command":"speedhz","speedHz":1000.0})");
    TEST_ASSERT_TRUE(lastResponseSuccess());
    TEST_ASSERT_TRUE(mockStepper.hasCall("setSpeedInHz"));
}

void test_dispatch_runforward(void) {
    sendCommand(R"({"command":"runforward"})");
    TEST_ASSERT_TRUE(lastResponseSuccess());
    TEST_ASSERT_TRUE(mockStepper.hasCall("runForward"));
}

void test_dispatch_runbackward(void) {
    sendCommand(R"({"command":"runbackward"})");
    TEST_ASSERT_TRUE(lastResponseSuccess());
    TEST_ASSERT_TRUE(mockStepper.hasCall("runBackward"));
}

void test_dispatch_stopmove(void) {
    sendCommand(R"({"command":"stopmove"})");
    TEST_ASSERT_TRUE(lastResponseSuccess());
    TEST_ASSERT_TRUE(mockStepper.hasCall("stopMove"));
}

void test_dispatch_forcestop(void) {
    sendCommand(R"({"command":"forcestop"})");
    TEST_ASSERT_TRUE(lastResponseSuccess());
    TEST_ASSERT_TRUE(mockStepper.hasCall("stopVelocity"));
}

void test_dispatch_reset(void) {
    sendCommand(R"({"command":"reset"})");
    TEST_ASSERT_TRUE(lastResponseSuccess());
    TEST_ASSERT_TRUE(mockStepper.hasCall("resetEngine"));
}

void test_dispatch_zero(void) {
    sendCommand(R"({"command":"zero"})");
    TEST_ASSERT_TRUE(lastResponseSuccess());
    TEST_ASSERT_EQUAL(1, (int)mockQueue.getCommands().size());
    TEST_ASSERT_EQUAL(MotorCommandType::ZERO_POSITION, mockQueue.getCommands()[0].type);
}

void test_dispatch_home(void) {
    sendCommand(R"({"command":"home"})");
    TEST_ASSERT_TRUE(lastResponseSuccess());
    TEST_ASSERT_EQUAL(1, (int)mockQueue.getCommands().size());
    TEST_ASSERT_EQUAL(MotorCommandType::HOME, mockQueue.getCommands()[0].type);
}

void test_dispatch_dance(void) {
    sendCommand(R"({"command":"behavior","name":"twist"})");
    TEST_ASSERT_TRUE(lastResponseSuccess());
    TEST_ASSERT_TRUE(mockStepper.hasCall("startDance"));
}

void test_dispatch_stopdance(void) {
    sendCommand(R"({"command":"behavior","stop":true})");
    TEST_ASSERT_TRUE(lastResponseSuccess());
}

void test_dispatch_behavior(void) {
    sendCommand(R"({"command":"behavior","name":"scanning"})");
    TEST_ASSERT_TRUE(lastResponseSuccess());
    TEST_ASSERT_TRUE(mockStepper.hasCall("startBehavior"));
}

void test_dispatch_stopbehavior(void) {
    sendCommand(R"({"command":"behavior","stop":true})");
    TEST_ASSERT_TRUE(lastResponseSuccess());
}

// ── Stop alias ───────────────────────────────────────────────────────────────

void test_dispatch_stop_alias(void) {
    sendCommand(R"({"command":"stop"})");
    TEST_ASSERT_TRUE(lastResponseSuccess());
    TEST_ASSERT_TRUE(mockStepper.hasCall("stopMove"));
}

// ── Case insensitivity ──────────────────────────────────────────────────────

void test_dispatch_case_insensitive_uppercase(void) {
    sendCommand(R"({"command":"RUNFORWARD"})");
    TEST_ASSERT_TRUE(lastResponseSuccess());
    TEST_ASSERT_TRUE(mockStepper.hasCall("runForward"));
}

void test_dispatch_case_insensitive_mixedcase(void) {
    sendCommand(R"({"command":"RunBackward"})");
    TEST_ASSERT_TRUE(lastResponseSuccess());
    TEST_ASSERT_TRUE(mockStepper.hasCall("runBackward"));
}

// ── Error cases ─────────────────────────────────────────────────────────────

void test_error_invalid_json(void) {
    sendCommand("not json at all{{{");
    TEST_ASSERT_TRUE(lastResponseError());
    std::string resp = lastResponsePayload();
    TEST_ASSERT_TRUE(resp.find("Invalid JSON") != std::string::npos);
}

void test_error_missing_command_field(void) {
    sendCommand(R"({"position":90.0})");
    TEST_ASSERT_TRUE(lastResponseError());
    std::string resp = lastResponsePayload();
    TEST_ASSERT_TRUE(resp.find("Missing") != std::string::npos);
}

void test_error_unknown_command(void) {
    sendCommand(R"({"command":"bogus"})");
    TEST_ASSERT_TRUE(lastResponseError());
    std::string resp = lastResponsePayload();
    TEST_ASSERT_TRUE(resp.find("Unknown command") != std::string::npos);
}

// ── KRP move dispatch ────────────────────────────────────────────────────────

void test_dispatch_move_missing_joint(void) {
    sendCommand(R"({"command":"move","heading":90.0})");
    TEST_ASSERT_TRUE(lastResponseError());
}

void test_dispatch_move_unknown_joint(void) {
    ctrl.mqttClient._clearPublishes();
    sendCommand(R"({"command":"move","joint":"arm","heading":90.0})");
    // Should be silently ignored - no response published
    auto& pubs = ctrl.mqttClient._getPublishes();
    bool hasResponse = false;
    for (const auto& p : pubs) {
        if (p.topic.find("/response") != std::string::npos) hasResponse = true;
    }
    TEST_ASSERT_FALSE(hasResponse);
}

// ── Legacy aliases ───────────────────────────────────────────────────────────

void test_dispatch_legacy_heading(void) {
    sendCommand(R"({"command":"heading","heading":180.0})");
    TEST_ASSERT_TRUE(lastResponseSuccess());
    TEST_ASSERT_TRUE(mockStepper.hasCall("moveToHeadingDegrees"));
}

void test_dispatch_legacy_position(void) {
    sendCommand(R"({"command":"position","position":90.0})");
    TEST_ASSERT_TRUE(lastResponseSuccess());
}

// ── Main ─────────────────────────────────────────────────────────────────────

int main(int, char**) {
    UNITY_BEGIN();

    // Command routes
    RUN_TEST(test_dispatch_position);
    RUN_TEST(test_dispatch_heading);
    RUN_TEST(test_dispatch_enable);
    RUN_TEST(test_dispatch_speed);
    RUN_TEST(test_dispatch_acceleration);
    RUN_TEST(test_dispatch_microsteps);
    RUN_TEST(test_dispatch_gearratio);
    RUN_TEST(test_dispatch_speedhz);
    RUN_TEST(test_dispatch_runforward);
    RUN_TEST(test_dispatch_runbackward);
    RUN_TEST(test_dispatch_stopmove);
    RUN_TEST(test_dispatch_forcestop);
    RUN_TEST(test_dispatch_reset);
    RUN_TEST(test_dispatch_zero);
    RUN_TEST(test_dispatch_home);
    RUN_TEST(test_dispatch_dance);
    RUN_TEST(test_dispatch_stopdance);
    RUN_TEST(test_dispatch_behavior);
    RUN_TEST(test_dispatch_stopbehavior);

    // Stop alias
    RUN_TEST(test_dispatch_stop_alias);

    // Case insensitivity
    RUN_TEST(test_dispatch_case_insensitive_uppercase);
    RUN_TEST(test_dispatch_case_insensitive_mixedcase);

    // Error cases
    RUN_TEST(test_error_invalid_json);
    RUN_TEST(test_error_missing_command_field);
    RUN_TEST(test_error_unknown_command);

    // KRP move dispatch
    RUN_TEST(test_dispatch_move_missing_joint);
    RUN_TEST(test_dispatch_move_unknown_joint);

    // Legacy aliases
    RUN_TEST(test_dispatch_legacy_heading);
    RUN_TEST(test_dispatch_legacy_position);

    return UNITY_END();
}
