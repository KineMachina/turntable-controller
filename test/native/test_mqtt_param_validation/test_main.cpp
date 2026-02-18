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
    ctrl.config.baseTopic = "test";
    ctrl.config.deviceId = "t1";
    ctrl.config.qosCommands = 1;
    ctrl.pendingMoveComplete = false;
    ctrl.pendingMoveRequestId[0] = '\0';
    ctrl.buildTopics();
}

void tearDown() {}

static void sendCommand(const char* json) {
    ctrl.handleCommand(json, strlen(json));
}

static std::string lastResponsePayload() {
    auto& pubs = ctrl.mqttClient._getPublishes();
    for (int i = (int)pubs.size() - 1; i >= 0; i--) {
        if (pubs[i].topic.find("/response") != std::string::npos) {
            return pubs[i].payload;
        }
    }
    return "";
}

static bool lastResponseSuccess() {
    return lastResponsePayload().find("\"status\":\"success\"") != std::string::npos;
}

static bool lastResponseError() {
    return lastResponsePayload().find("\"status\":\"error\"") != std::string::npos;
}

// ── Position parameter validation ───────────────────────────────────────────

void test_position_missing_param(void) {
    sendCommand(R"({"command":"position"})");
    TEST_ASSERT_TRUE(lastResponseError());
}

void test_position_wrong_type(void) {
    sendCommand(R"({"command":"position","position":"abc"})");
    TEST_ASSERT_TRUE(lastResponseError());
}

void test_position_queue_full(void) {
    mockQueue.setFailSend(true);
    sendCommand(R"({"command":"position","position":90.0})");
    TEST_ASSERT_TRUE(lastResponseError());
    std::string resp = lastResponsePayload();
    TEST_ASSERT_TRUE(resp.find("queue full") != std::string::npos);
}

// ── Heading parameter validation ────────────────────────────────────────────

void test_heading_missing_param(void) {
    sendCommand(R"({"command":"heading"})");
    TEST_ASSERT_TRUE(lastResponseError());
}

void test_heading_wrong_type(void) {
    sendCommand(R"({"command":"heading","heading":"north"})");
    TEST_ASSERT_TRUE(lastResponseError());
}

void test_heading_stepper_failure(void) {
    mockStepper._moveToHeadingResult = false;
    sendCommand(R"({"command":"heading","heading":90.0})");
    TEST_ASSERT_TRUE(lastResponseError());
}

// ── Enable parameter validation ─────────────────────────────────────────────

void test_enable_missing_param(void) {
    sendCommand(R"({"command":"enable"})");
    TEST_ASSERT_TRUE(lastResponseError());
}

void test_enable_wrong_type(void) {
    sendCommand(R"({"command":"enable","enable":"yes"})");
    TEST_ASSERT_TRUE(lastResponseError());
}

// ── Speed parameter validation ──────────────────────────────────────────────

void test_speed_missing_param(void) {
    sendCommand(R"({"command":"speed"})");
    TEST_ASSERT_TRUE(lastResponseError());
}

void test_speed_zero_rejected(void) {
    sendCommand(R"({"command":"speed","speed":0})");
    TEST_ASSERT_TRUE(lastResponseError());
}

void test_speed_negative_rejected(void) {
    sendCommand(R"({"command":"speed","speed":-10.0})");
    TEST_ASSERT_TRUE(lastResponseError());
}

// ── Acceleration parameter validation ───────────────────────────────────────

void test_acceleration_missing_param(void) {
    sendCommand(R"({"command":"acceleration"})");
    TEST_ASSERT_TRUE(lastResponseError());
}

void test_acceleration_zero_rejected(void) {
    sendCommand(R"({"command":"acceleration","accel":0})");
    TEST_ASSERT_TRUE(lastResponseError());
}

// ── Microsteps parameter validation ─────────────────────────────────────────

void test_microsteps_missing_param(void) {
    sendCommand(R"({"command":"microsteps"})");
    TEST_ASSERT_TRUE(lastResponseError());
}

void test_microsteps_invalid_value(void) {
    sendCommand(R"({"command":"microsteps","microsteps":3})");
    TEST_ASSERT_TRUE(lastResponseError());
}

// ── Gear ratio parameter validation ─────────────────────────────────────────

void test_gearratio_missing_param(void) {
    sendCommand(R"({"command":"gearratio"})");
    TEST_ASSERT_TRUE(lastResponseError());
}

void test_gearratio_zero_rejected(void) {
    sendCommand(R"({"command":"gearratio","ratio":0})");
    TEST_ASSERT_TRUE(lastResponseError());
}

void test_gearratio_over_100_rejected(void) {
    sendCommand(R"({"command":"gearratio","ratio":101.0})");
    TEST_ASSERT_TRUE(lastResponseError());
}

// ── SpeedHz parameter validation ────────────────────────────────────────────

void test_speedhz_missing_param(void) {
    sendCommand(R"({"command":"speedhz"})");
    TEST_ASSERT_TRUE(lastResponseError());
}

void test_speedhz_negative_rejected(void) {
    sendCommand(R"({"command":"speedhz","speedHz":-1.0})");
    TEST_ASSERT_TRUE(lastResponseError());
}

// ── Dance parameter validation ──────────────────────────────────────────────

void test_dance_missing_dancetype(void) {
    sendCommand(R"({"command":"dance"})");
    TEST_ASSERT_TRUE(lastResponseError());
}

void test_dance_invalid_dancetype(void) {
    sendCommand(R"({"command":"dance","danceType":"moonwalk"})");
    TEST_ASSERT_TRUE(lastResponseError());
}

void test_dance_all_types(void) {
    const char* types[] = {"twist", "shake", "spin", "wiggle", "watusi", "pepperminttwist"};
    for (auto& dt : types) {
        setUp();
        char json[128];
        snprintf(json, sizeof(json), R"({"command":"dance","danceType":"%s"})", dt);
        sendCommand(json);
        TEST_ASSERT_TRUE_MESSAGE(lastResponseSuccess(), dt);
    }
}

void test_dance_peppermint_twist_underscore(void) {
    sendCommand(R"({"command":"dance","danceType":"peppermint_twist"})");
    TEST_ASSERT_TRUE(lastResponseSuccess());
}

// ── Behavior parameter validation ───────────────────────────────────────────

void test_behavior_missing_behaviortype(void) {
    sendCommand(R"({"command":"behavior"})");
    TEST_ASSERT_TRUE(lastResponseError());
}

void test_behavior_invalid_behaviortype(void) {
    sendCommand(R"({"command":"behavior","behaviorType":"flying"})");
    TEST_ASSERT_TRUE(lastResponseError());
}

void test_behavior_all_types(void) {
    const char* types[] = {"scanning", "sleeping", "eating", "alert", "roaring",
                           "stalking", "playing", "resting", "hunting", "victory"};
    for (auto& bt : types) {
        setUp();
        char json[128];
        snprintf(json, sizeof(json), R"({"command":"behavior","behaviorType":"%s"})", bt);
        sendCommand(json);
        TEST_ASSERT_TRUE_MESSAGE(lastResponseSuccess(), bt);
    }
}

// ── Request ID tracking ─────────────────────────────────────────────────────

void test_position_request_id_stored(void) {
    sendCommand(R"({"command":"position","position":45.0,"request_id":"abc-123"})");
    TEST_ASSERT_TRUE(lastResponseSuccess());
    TEST_ASSERT_TRUE(ctrl.pendingMoveComplete);
    TEST_ASSERT_EQUAL_STRING("abc-123", ctrl.pendingMoveRequestId);
    TEST_ASSERT_EQUAL_STRING("position", ctrl.pendingMoveCommandType);
}

void test_heading_request_id_stored(void) {
    sendCommand(R"({"command":"heading","heading":270.0,"request_id":"xyz-789"})");
    TEST_ASSERT_TRUE(lastResponseSuccess());
    TEST_ASSERT_TRUE(ctrl.pendingMoveComplete);
    TEST_ASSERT_EQUAL_STRING("xyz-789", ctrl.pendingMoveRequestId);
    TEST_ASSERT_EQUAL_STRING("heading", ctrl.pendingMoveCommandType);
}

void test_position_no_request_id(void) {
    sendCommand(R"({"command":"position","position":45.0})");
    TEST_ASSERT_TRUE(lastResponseSuccess());
    TEST_ASSERT_TRUE(ctrl.pendingMoveComplete);
    TEST_ASSERT_EQUAL_STRING("", ctrl.pendingMoveRequestId);
}

// ── Main ─────────────────────────────────────────────────────────────────────

int main(int, char**) {
    UNITY_BEGIN();

    // Position
    RUN_TEST(test_position_missing_param);
    RUN_TEST(test_position_wrong_type);
    RUN_TEST(test_position_queue_full);

    // Heading
    RUN_TEST(test_heading_missing_param);
    RUN_TEST(test_heading_wrong_type);
    RUN_TEST(test_heading_stepper_failure);

    // Enable
    RUN_TEST(test_enable_missing_param);
    RUN_TEST(test_enable_wrong_type);

    // Speed
    RUN_TEST(test_speed_missing_param);
    RUN_TEST(test_speed_zero_rejected);
    RUN_TEST(test_speed_negative_rejected);

    // Acceleration
    RUN_TEST(test_acceleration_missing_param);
    RUN_TEST(test_acceleration_zero_rejected);

    // Microsteps
    RUN_TEST(test_microsteps_missing_param);
    RUN_TEST(test_microsteps_invalid_value);

    // Gear ratio
    RUN_TEST(test_gearratio_missing_param);
    RUN_TEST(test_gearratio_zero_rejected);
    RUN_TEST(test_gearratio_over_100_rejected);

    // SpeedHz
    RUN_TEST(test_speedhz_missing_param);
    RUN_TEST(test_speedhz_negative_rejected);

    // Dance
    RUN_TEST(test_dance_missing_dancetype);
    RUN_TEST(test_dance_invalid_dancetype);
    RUN_TEST(test_dance_all_types);
    RUN_TEST(test_dance_peppermint_twist_underscore);

    // Behavior
    RUN_TEST(test_behavior_missing_behaviortype);
    RUN_TEST(test_behavior_invalid_behaviortype);
    RUN_TEST(test_behavior_all_types);

    // Request ID
    RUN_TEST(test_position_request_id_stored);
    RUN_TEST(test_heading_request_id_stored);
    RUN_TEST(test_position_no_request_id);

    return UNITY_END();
}
