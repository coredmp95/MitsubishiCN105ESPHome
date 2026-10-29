/// test_readings_decode.cpp — Characterization of the CN105 read path (hp_readings.cpp)
/// Role: pins, on the unmodified component, what happens to the bytes the heat pump sends: checksum
///       mismatch, unknown command and info-code bytes, a frame declaring an oversized payload, the 0x04
///       error info, the 0x09 stage / sub-mode / auto sub-mode decode from real frames, the two
///       room-temperature encodings and the suppression of duplicate status publishes (PR 1b, D-15).
///       Frames fed to the component are the real_frames.h constants. The negative tests feed a real frame
///       modified on purpose; each says which byte was changed and the frame is never presented as a capture.
///       The 0x09 DEFROST sub-mode is NOT pinned: plan 01-12 recorded no DEFROST capture (see "Wanted
///       captures" in tests/unit/README.md), so no frame is invented for it.
/// Deps: fixture.h, real_frames.h, captured_frames_1b.h (empty), cn105.h, the text sensor entities

#include "fixture.h"

#include <cmath>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "auto_sub_mode_sensor.h"
#include "captured_frames_1b.h"  // empty on purpose: no real capture was supplied for PR 1b
#include "error_code_sensor.h"
#include "real_frames.h"
#include "stage_sensor.h"
#include "sub_mode_sensor.h"

namespace {

using cn105_test::TestLog;
namespace rf = cn105_test::real_frames;

std::string str(const char* s) { return s == nullptr ? std::string("<null>") : std::string(s); }

/// The first record whose message contains `needle`, or nullptr.
const cn105_test::LogRecord* find_log(const std::string& needle) {
    for (const auto& r : TestLog::records()) {
        if (r.message.find(needle) != std::string::npos) return &r;
    }
    return nullptr;
}

/// True when the checksum of a received frame was accepted (debug log "OK" under the "chkSum" tag).
bool has_checksum_ok_log() {
    for (const auto& r : TestLog::records()) {
        if (r.tag == "chkSum" && r.message == "OK") return true;
    }
    return false;
}

}  // namespace

class ReadingsDecode : public cn105_test::Cn105Fixture {
   protected:
    /// Handshake plus one default real poll cycle (settings ON HEAT 21.5, room 23 C, status idle, power idle,
    /// error info), then forget what was written and published so far.
    void connect_and_poll() {
        connect();
        ASSERT_FALSE(HasFatalFailure());
        run_poll_cycle();
        take_tx();
        publishes_.clear();
        TestLog::clear();
    }

    /// One poll cycle where the answer to `code` is `frame` (and the default real answers otherwise).
    std::vector<uint8_t> poll_with(uint8_t code, const std::vector<uint8_t>& frame) {
        auto responses = default_responses();
        responses[code] = frame;
        return run_poll_cycle(responses);
    }
};

// ════════════════════════════════════════════════════════════════
// Checksum, unknown command, unknown info code, oversized frame
// ════════════════════════════════════════════════════════════════

TEST_F(ReadingsDecode, Characterize_ChecksumMismatchDropped) {
    connect_and_poll();
    ASSERT_FALSE(HasFatalFailure());
    step(100);
    const uint32_t last_response = static_cast<uint32_t>(hp().lastResponseMs);
    const std::string fan = str(hp().currentSettings.fan);

    // Derived from a real frame, checksum corrupted on purpose: SETTINGS_FAN2_VANE_SWING differs from the
    // current state (ON HEAT 21.5), so a decode would change the fan and publish.
    auto corrupted = cn105_test::with_checksum(rf::SETTINGS_FAN2_VANE_SWING);
    corrupted.back() = static_cast<uint8_t>(corrupted.back() + 1);
    feed_raw(corrupted);
    step(10);

    const auto* ko = find_log("KO -> checksum mismatch (cmd=0x62 len=16)");
    ASSERT_NE(ko, nullptr) << "the checksum-mismatch warning is missing";
    EXPECT_EQ(ko->level, ESPHOME_LOG_LEVEL_WARN);
    EXPECT_EQ(ko->tag, "chkSum");
    EXPECT_FALSE(has_checksum_ok_log());
    // The frame is dropped: no state change, no publish, and lastResponseMs does NOT move (it is only
    // refreshed by a frame whose checksum is valid).
    EXPECT_EQ(static_cast<uint32_t>(hp().lastResponseMs), last_response);
    EXPECT_EQ(str(hp().currentSettings.fan), fan);
    EXPECT_TRUE(publishes_.empty());

    // The parser is clean afterwards: the same real frame with its valid checksum is decoded.
    feed(rf::SETTINGS_FAN2_VANE_SWING);
    step(10);
    EXPECT_GT(static_cast<uint32_t>(hp().lastResponseMs), last_response);
    EXPECT_EQ(str(hp().currentSettings.fan), "2");
    EXPECT_FALSE(publishes_.empty());
}

TEST_F(ReadingsDecode, Characterize_UnknownCommandIgnored) {
    connect_and_poll();
    ASSERT_FALSE(HasFatalFailure());
    step(100);
    const uint32_t last_response = static_cast<uint32_t>(hp().lastResponseMs);
    const std::string fan = str(hp().currentSettings.fan);

    // Derived from the real SETTINGS_FAN2_VANE_SWING frame: command byte 0x62 changed to 0x63, checksum
    // recomputed so that it is valid. 0x63 is not a command the component knows.
    auto derived = rf::SETTINGS_FAN2_VANE_SWING;
    derived[1] = 0x63;
    feed(derived);
    step(10);

    // A valid checksum is enough to count as a heat pump reply (lastResponseMs moves) but processCommand()
    // has no case for 0x63: nothing is decoded, nothing is published, nothing is logged as unknown.
    EXPECT_GT(static_cast<uint32_t>(hp().lastResponseMs), last_response);
    EXPECT_EQ(str(hp().currentSettings.fan), fan);
    EXPECT_TRUE(publishes_.empty());
    EXPECT_TRUE(has_checksum_ok_log());
    EXPECT_EQ(find_log("unknown and unexpected"), nullptr);
    EXPECT_EQ(find_log("checksum mismatch"), nullptr);
}

TEST_F(ReadingsDecode, Characterize_UnknownInfoCodeLogged) {
    connect_and_poll();
    ASSERT_FALSE(HasFatalFailure());

    // Derived from the real ROOM_TEMP_23C frame: info code byte (offset 5) changed from 0x03 to 0x77, an
    // info code nothing registered and the switch does not know.
    auto unknown = rf::ROOM_TEMP_23C;
    unknown[5] = 0x77;
    feed(unknown);
    step(10);
    const auto* warn = find_log("packet type [77] <-- unknown and unexpected");
    ASSERT_NE(warn, nullptr);
    EXPECT_EQ(warn->level, ESPHOME_LOG_LEVEL_WARN);

    // Same frame with the timer code 0x05. The "timers" request is registered in the scheduler, so the
    // scheduler consumes the reply (process_response) and the "[0x05 is Timer : not implemented]" case of the
    // switch below it is never reached.
    take_tx();
    auto timer = rf::ROOM_TEMP_23C;
    timer[5] = 0x05;
    feed(timer);
    step(10);
    EXPECT_EQ(find_log("[0x05 is Timer : not implemented]"), nullptr);
    EXPECT_TRUE(take_tx().empty()) << "outside a cycle the consumed reply triggers no request";

    // And 0x10: debug log "Unknown : not implemented".
    auto ten = rf::ROOM_TEMP_23C;
    ten[5] = 0x10;
    feed(ten);
    step(10);
    const auto* ten_log = find_log("[0x10 is Unknown : not implemented]");
    ASSERT_NE(ten_log, nullptr);
    EXPECT_EQ(ten_log->level, ESPHOME_LOG_LEVEL_DEBUG);

    EXPECT_TRUE(publishes_.empty()) << "none of these frames may change the published state";
}

TEST_F(ReadingsDecode, Characterize_ParserOverflowResets) {
    connect_and_poll();
    ASSERT_FALSE(HasFatalFailure());

    // Header derived from the real frames (FC 62 01 30) declaring a payload of 0xFF bytes, followed by 70
    // filler bytes without a start byte. FrameParser refuses a declared length that cannot fit in
    // MAX_DATA_BYTES (64) at the fifth byte and resets; the filler is skipped while looking for 0xFC.
    // (The "bytes_read_ >= MAX_DATA_BYTES" overflow guard itself is unreachable through feed(): the
    // sanity check always fires first.)
    std::vector<uint8_t> oversized = {0xFC, 0x62, 0x01, 0x30, 0xFF};
    oversized.insert(oversized.end(), 70, 0x00);
    feed_raw(oversized);
    step(10);
    EXPECT_EQ(find_log("checksum mismatch"), nullptr) << "the oversized frame never completes";
    EXPECT_TRUE(publishes_.empty());

    // Garbage before a start byte is skipped too, and the next real frame decodes normally.
    feed_raw({0x00, 0x13, 0xA5});
    feed(rf::SETTINGS_FAN2_VANE_SWING);
    step(10);
    EXPECT_EQ(str(hp().currentSettings.fan), "2");
    EXPECT_FALSE(publishes_.empty());
}

// ════════════════════════════════════════════════════════════════
// 0x04 error info
// ════════════════════════════════════════════════════════════════

/// Error code text sensor attached, as the error_code_sensor YAML option does.
class ReadingsDecodeSensors : public ReadingsDecode {
   protected:
    void before_setup(esphome::CN105Climate& hp) override {
        hp.set_error_code_sensor(&error_sensor_);
        hp.set_stage_sensor(&stage_sensor_);
        hp.set_sub_mode_sensor(&sub_mode_sensor_);
        hp.set_auto_sub_mode_sensor(&auto_sub_mode_sensor_);
    }
    esphome::ErrorCodeSensor error_sensor_;
    esphome::StageSensor stage_sensor_;
    esphome::SubModSensor sub_mode_sensor_;
    esphome::AutoSubModSensor auto_sub_mode_sensor_;
};

TEST_F(ReadingsDecodeSensors, Characterize_ErrorInfoDecoded) {
    connect();
    ASSERT_FALSE(HasFatalFailure());
    const auto requested = run_poll_cycle();

    // The real 0x04 reply (extended status 0x80: bit 7 only announces that error reporting exists, the
    // code is 0 and the sub-code 0) is requested because the sensor is configured, and decoded to "No Error".
    bool asked_for_error_info = false;
    for (uint8_t code : requested) asked_for_error_info = asked_for_error_info || code == 0x04;
    EXPECT_TRUE(asked_for_error_info) << "0x04 must be requested when the error sensor is configured";
    EXPECT_EQ(error_sensor_.state, "No Error");
    // It was routed by the scheduler (onResponse), not by the switch fallback.
    EXPECT_EQ(find_log("reached switch fallback"), nullptr);
}

// ════════════════════════════════════════════════════════════════
// 0x09 stage / sub-mode / auto sub-mode from real power frames
// ════════════════════════════════════════════════════════════════

TEST_F(ReadingsDecodeSensors, Characterize_SubModesFromRealPowerFrames) {
    connect();
    ASSERT_FALSE(HasFatalFailure());

    // POWER_IDLE: all bytes 0 -> stage IDLE, sub-mode NORMAL, auto sub-mode AUTO_OFF.
    poll_with(0x09, rf::POWER_IDLE);
    EXPECT_EQ(stage_sensor_.state, "IDLE");
    EXPECT_EQ(sub_mode_sensor_.state, "NORMAL");
    EXPECT_EQ(auto_sub_mode_sensor_.state, "AUTO_OFF");
    EXPECT_EQ(str(hp().currentSettings.stage), "IDLE");
    EXPECT_EQ(str(hp().currentSettings.sub_mode), "NORMAL");
    EXPECT_EQ(str(hp().currentSettings.auto_sub_mode), "AUTO_OFF");

    // POWER_LOW: stage byte 1 -> LOW. The sub-mode does not change.
    publishes_.clear();
    poll_with(0x09, rf::POWER_LOW);
    EXPECT_EQ(stage_sensor_.state, "LOW");
    EXPECT_EQ(sub_mode_sensor_.state, "NORMAL");
    EXPECT_EQ(auto_sub_mode_sensor_.state, "AUTO_OFF");
    EXPECT_TRUE(publishes_.empty()) << "a stage change alone is only published on the stage sensor";

    // POWER_OFF_SUBMODE_0x10: sub-mode byte 0x10 -> OFF, stage back to IDLE. A sub-mode change is published
    // to Home Assistant (decode_submode returns true), a stage change alone is not.
    publishes_.clear();
    poll_with(0x09, rf::POWER_OFF_SUBMODE_0x10);
    EXPECT_EQ(stage_sensor_.state, "IDLE");
    EXPECT_EQ(sub_mode_sensor_.state, "OFF");
    EXPECT_EQ(auto_sub_mode_sensor_.state, "AUTO_OFF");
    EXPECT_EQ(str(hp().currentSettings.sub_mode), "OFF");
    EXPECT_FALSE(publishes_.empty()) << "a sub-mode change refreshes the climate action";

    // Back to the idle frame: sub-mode returns to NORMAL.
    poll_with(0x09, rf::POWER_IDLE);
    EXPECT_EQ(sub_mode_sensor_.state, "NORMAL");
}

// ════════════════════════════════════════════════════════════════
// Room temperature encodings
// ════════════════════════════════════════════════════════════════

TEST_F(ReadingsDecode, Characterize_RoomTemperatureEncodings) {
    connect();
    ASSERT_FALSE(HasFatalFailure());

    // Real ROOM_TEMP_23C: data[6] = 0xAE, (174 - 128) / 2 = 23.0, which takes precedence; data[3] = 0x0D is
    // the legacy encoding of the same 23 C (RealRoomTemp.BothEncodings_23C). The outside temperature byte
    // (data[5]) is 0: no outside temperature (NaN).
    run_poll_cycle();
    EXPECT_FLOAT_EQ(hp().currentStatus.roomTemperature, 23.0f);
    EXPECT_TRUE(std::isnan(hp().currentStatus.outsideAirTemperature));
    ASSERT_FALSE(publishes_.empty());
    EXPECT_FLOAT_EQ(publishes_.back().current_temperature, 23.0f);
    EXPECT_FLOAT_EQ(hp().current_temperature, 23.0f);
}

/// Same, with a frame whose precise-temperature byte is zeroed.
TEST_F(ReadingsDecode, Characterize_RoomTemperatureLegacyEncoding) {
    connect();
    ASSERT_FALSE(HasFatalFailure());

    // Derived from the real ROOM_TEMP_23C frame: data[6] (offset 11) set to 0, which selects the legacy
    // lookup of data[3] = 0x0D in ROOM_TEMP_MAP (23 C).
    auto legacy = rf::ROOM_TEMP_23C;
    legacy[11] = 0x00;
    poll_with(0x03, legacy);
    EXPECT_FLOAT_EQ(hp().currentStatus.roomTemperature, 23.0f);
    EXPECT_FLOAT_EQ(hp().current_temperature, 23.0f);
    EXPECT_EQ(find_log("Unknown room_temp byte"), nullptr);
}

TEST_F(ReadingsDecode, Characterize_RoomTemperatureUnknownLegacyByteKeepsPrevious) {
    connect();
    ASSERT_FALSE(HasFatalFailure());

    // Derived from the real ROOM_TEMP_23C frame: data[6] set to 0 and data[3] (offset 8) set to 0xFF, a
    // byte absent from ROOM_TEMP_MAP. The room temperature keeps its previous value (none yet: NaN).
    auto unknown = rf::ROOM_TEMP_23C;
    unknown[11] = 0x00;
    unknown[8] = 0xFF;
    poll_with(0x03, unknown);
    const auto* warn = find_log("Unknown room_temp byte 0xFF");
    ASSERT_NE(warn, nullptr);
    EXPECT_EQ(warn->level, ESPHOME_LOG_LEVEL_WARN);
    EXPECT_TRUE(std::isnan(hp().currentStatus.roomTemperature));
}

// ════════════════════════════════════════════════════════════════
// Duplicate status suppression
// ════════════════════════════════════════════════════════════════

TEST_F(ReadingsDecode, Characterize_DuplicateStatusNotRepublished) {
    connect();
    ASSERT_FALSE(HasFatalFailure());

    // First cycle: the room temperature (NaN -> 23.0) and the settings are new: Home Assistant is told.
    run_poll_cycle();
    const size_t after_first = publishes_.size();
    EXPECT_GT(after_first, 0u);

    // Second and third cycles carry exactly the same 0x02 / 0x03 / 0x06 / 0x09 values: statusChanged() and
    // heatpumpUpdate() see no difference, nothing more is published (NaN outside temperature included:
    // heatpumpStatus::operator== compares NaN with NaN as equal).
    run_for(1100);  // let the update interval pass
    run_poll_cycle();
    EXPECT_EQ(publishes_.size(), after_first) << "an identical second cycle must not publish";
    run_for(1100);
    run_poll_cycle();
    EXPECT_EQ(publishes_.size(), after_first) << "an identical third cycle must not publish";
    EXPECT_FLOAT_EQ(hp().currentStatus.roomTemperature, 23.0f);
}
