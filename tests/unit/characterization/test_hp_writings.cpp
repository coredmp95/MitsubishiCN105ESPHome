/// test_hp_writings.cpp — Characterization of the CN105 write path (hp_writings.cpp)
/// Role: pins the exact bytes the unmodified component writes to the UART (info requests, CONNECT,
///       0x41 SET packets), the writePacket fallback with its single pending_packet_ slot and the
///       HA-visible effects of queued writes, known bugs included (audit BUG-03, BUG-04), before
///       Phases 5 (LINK-01/02) and 6 (CMD-04) change them.
/// Deps: fixture.h, real_frames.h, cn105_protocol.h, cn105.h

#include "fixture.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "cn105_protocol.h"
#include "hvac_option_switch.h"
#include "real_frames.h"

namespace {

using cn105_test::hex;

/// An info request frame as the component writes it: FC 42 01 30 10 <code> + 15 zeros + checksum.
std::vector<uint8_t> info_request_frame(uint8_t code, uint8_t expected_checksum) {
    std::vector<uint8_t> frame = {0xFC, 0x42, 0x01, 0x30, 0x10, code};
    frame.resize(21, 0x00);
    frame.push_back(expected_checksum);
    return frame;
}

}  // namespace

namespace {

const std::vector<uint8_t> kConnectFrame = {0xFC, 0x5A, 0x01, 0x30, 0x02, 0xCA, 0x01, 0xA8};

/// Number of frames in `frames` whose command byte (index 1) is `command`.
size_t count_command(const std::vector<std::vector<uint8_t>>& frames, uint8_t command) {
    return static_cast<size_t>(std::count_if(frames.begin(), frames.end(), [command](const std::vector<uint8_t>& f) {
        return f.size() > 1 && f[1] == command;
    }));
}

}  // namespace

class HpWritings : public cn105_test::Cn105Fixture {
   protected:
    /// Connects, then stops answering and steps until the link counts as inactive (no response for
    /// 10 * update_interval). Poll requests sent meanwhile are discarded. Sets inactive_after_ms_ to the
    /// time elapsed since the last heat pump response. The state stays CONNECTED: nothing reconnects until
    /// a write is attempted (the point of the BUG-04 pins below).
    void make_connection_inactive() {
        connect();
        ASSERT_FALSE(HasFatalFailure());
        const uint32_t last_response = static_cast<uint32_t>(hp().lastResponseMs);
        for (uint32_t guard = 0; hp().isHeatpumpConnectionActive() && guard < 2000; guard++) step(10);
        ASSERT_FALSE(hp().isHeatpumpConnectionActive());
        inactive_after_ms_ = cn105_test::TestClock::now() - last_response;
        take_tx();
    }

    /// A 22-byte real SET frame (SET_HEAT_21_5 + checksum), writable through writePacket.
    static std::vector<uint8_t> set_heat_frame() { return cn105_test::with_checksum(cn105_test::real_frames::SET_HEAT_21_5); }

    uint32_t inactive_after_ms_ = 0;
};

// ════════════════════════════════════════════════════════════════
// Connected writes — info request bytes
// ════════════════════════════════════════════════════════════════

TEST_F(HpWritings, Characterize_InfoPacketBytesForEachCode) {
    connect();
    ASSERT_FALSE(HasFatalFailure());
    take_tx();

    // Hand-computed checksums: (0xFC - sum of the 21 first bytes) & 0xFF, e.g. code 0x02: sum 0x181 -> 0x7B.
    const struct {
        uint8_t code;
        uint8_t checksum;
    } cases[] = {{0x02, 0x7B}, {0x03, 0x7A}, {0x04, 0x79}, {0x05, 0x78}, {0x06, 0x77}, {0x09, 0x74}, {0x42, 0x3B}};

    for (const auto& c : cases) {
        step(7);  // moves the clock so lastSend is distinguishable from its previous value
        const unsigned long before = hp().lastSend;
        ASSERT_NE(before, static_cast<unsigned long>(esphome::millis())) << "clock did not move";

        hp().buildAndSendInfoPacket(c.code);

        const std::vector<uint8_t> expected = info_request_frame(c.code, c.checksum);
        ASSERT_EQ(expected.size(), 22u);
        const std::vector<uint8_t> written = take_tx();
        EXPECT_EQ(written, expected) << "code 0x" << std::hex << int(c.code) << ": " << hex(written);
        EXPECT_EQ(hp().lastSend, static_cast<unsigned long>(cn105_test::TestClock::now()))
            << "writePacket stamps lastSend with the write time";
        // The checksum the component computes is the protocol checksum of the first 21 bytes.
        EXPECT_EQ(cn105_protocol::checksum(const_cast<uint8_t*>(written.data()), 21), c.checksum);
    }
}

TEST_F(HpWritings, Characterize_LegacyRequestPacketIndexMapsToInfoCodes) {
    connect();
    ASSERT_FALSE(HasFatalFailure());
    take_tx();

    // buildAndSendRequestPacket(int) is the legacy index API; unknown indices fall back to 0x02.
    const struct {
        int index;
        uint8_t code;
    } cases[] = {{0, 0x02}, {1, 0x03}, {2, 0x04}, {3, 0x05}, {4, 0x06}, {5, 0x09}, {6, 0x42}, {99, 0x02}, {-1, 0x02}};
    for (const auto& c : cases) {
        hp().buildAndSendRequestPacket(c.index);
        const auto frames = take_tx_frames();
        ASSERT_EQ(frames.size(), 1u) << "index " << c.index;
        EXPECT_EQ(frames[0][5], c.code) << "index " << c.index;
    }
}

TEST_F(HpWritings, Characterize_ConnectPacketStandardMode) {
    // The CONNECT frame leaves after the start-up grace delay (maybe_start_connection_), before any reply.
    std::vector<uint8_t> written;
    for (uint32_t waited = 0; written.empty() && waited < 30000; waited += 10) {
        step(10);
        written = take_tx();
    }
    const std::vector<uint8_t> connect_frame = {0xFC, 0x5A, 0x01, 0x30, 0x02, 0xCA, 0x01, 0xA8};
    EXPECT_EQ(written, connect_frame) << hex(written);

    // Written immediately (not queued): the first packet bypasses the "connection active" check
    // (writePacket(..., checkIsActive = false)) although the heat pump never replied.
    EXPECT_FALSE(hp().has_pending_packet_);
    EXPECT_FALSE(hp().isHeatpumpConnected());
    EXPECT_FALSE(hp().isHeatpumpConnectionActive()) << "no response has ever been received";
    EXPECT_EQ(hp().nbHeatpumpConnections_, 1u);
    EXPECT_EQ(hp().lastSend, static_cast<unsigned long>(cn105_test::TestClock::now()));
    EXPECT_EQ(hp().lastConnectRqTimeMs, static_cast<unsigned long>(cn105_test::TestClock::now()));
    EXPECT_TRUE(cn105_test::TestLog::contains("Standard"));
}
// ════════════════════════════════════════════════════════════════
// writePacket fallback — single pending slot, reconnect bursts, retries
// ════════════════════════════════════════════════════════════════

TEST_F(HpWritings, Characterize_ConnectionBecomesInactiveAfterTenIntervals) {
    make_connection_inactive();
    ASSERT_FALSE(HasFatalFailure());
    // isHeatpumpConnectionActive(): (millis() - lastResponseMs) < 10 * update_interval_, so exactly 10000 ms
    // after the last response at the 1000 ms default interval. The FSM still says CONNECTED.
    EXPECT_EQ(inactive_after_ms_, 10000u);
    EXPECT_TRUE(hp().isHeatpumpConnected());
}

TEST_F(HpWritings, Characterize_WriteWhenInactiveQueuesAndReconnects) {
    make_connection_inactive();
    ASSERT_FALSE(HasFatalFailure());

    std::vector<uint8_t> set = set_heat_frame();
    ASSERT_EQ(set.size(), 22u);
    hp().writePacket(set.data(), static_cast<int>(set.size()));

    // The write is refused: reconnectUART() runs at once and a CONNECT burst goes out instead of the packet.
    const auto frames = take_tx_frames();
    ASSERT_EQ(frames.size(), 1u);
    EXPECT_EQ(frames[0], kConnectFrame) << hex(frames[0]);
    EXPECT_FALSE(hp().isHeatpumpConnected());
    EXPECT_TRUE(cn105_test::TestLog::contains("could not write as asked, because UART is not connected"));
    EXPECT_TRUE(cn105_test::TestLog::contains("delaying packet writing because we need to reconnect first"));

    // The packet sits in the single pending slot, with its checkIsActive flag.
    EXPECT_TRUE(hp().has_pending_packet_);
    EXPECT_EQ(hp().pending_packet_len_, 22);
    EXPECT_TRUE(hp().pending_check_is_active_);
    EXPECT_EQ(std::vector<uint8_t>(hp().pending_packet_, hp().pending_packet_ + 22), set);

    // Nothing is written during the 4000 ms retry delay.
    run_for(3990);
    EXPECT_TRUE(take_tx().empty());
    EXPECT_TRUE(hp().has_pending_packet_);
}

TEST_F(HpWritings, Characterize_RetryStillInactiveReconnectsAgainAndForgetsPacket) {
    make_connection_inactive();
    ASSERT_FALSE(HasFatalFailure());
    std::vector<uint8_t> set = set_heat_frame();
    hp().writePacket(set.data(), 22);
    take_tx();

    // At 4000 ms try_write_pending_packet() runs: the UART is ready (CONNECTING) but the link is still
    // inactive (no reply to the CONNECT), so writePacket() fails again and starts another reconnect, which
    // re-queues the packet. try_write_pending_packet() then clears has_pending_packet_ unconditionally,
    // whatever writePacket() just did: the re-armed "write" timeout finds nothing to do.
    // Pins current behaviour — the pending-packet rework is Phase 5 (LINK-02).
    run_for(4000);
    const auto frames = take_tx_frames();
    ASSERT_EQ(frames.size(), 1u);
    EXPECT_EQ(frames[0], kConnectFrame) << hex(frames[0]);
    EXPECT_FALSE(hp().has_pending_packet_) << "flag cleared right after the packet was re-queued";
    EXPECT_EQ(std::vector<uint8_t>(hp().pending_packet_, hp().pending_packet_ + 22), set)
        << "the stale bytes remain in the slot";

    // The SET frame never reaches the wire, however long we wait without a heat pump reply.
    run_for(20000);
    EXPECT_EQ(count_command(take_tx_frames(), 0x41), 0u);
}

TEST_F(HpWritings, Characterize_OversizePacketDroppedAfterReconnect) {
    make_connection_inactive();
    ASSERT_FALSE(HasFatalFailure());

    std::vector<uint8_t> oversize(23, 0x00);
    hp().writePacket(oversize.data(), static_cast<int>(oversize.size()));

    // The reconnect (CONNECT burst) happens before the length check, then the packet is dropped.
    const auto frames = take_tx_frames();
    ASSERT_EQ(frames.size(), 1u);
    EXPECT_EQ(frames[0], kConnectFrame) << hex(frames[0]);
    EXPECT_TRUE(cn105_test::TestLog::contains("exceeds PACKET_LEN"));
    EXPECT_FALSE(hp().has_pending_packet_);
    run_for(8000);
    EXPECT_EQ(count_command(take_tx_frames(), 0x00), 0u);
}

TEST_F(HpWritings, Characterize_BootWriteBypassesGraceDelayAndConnects) {
    // Before the first loop() the FSM is in BOOT (UART not ready). A write at that point is refused, and
    // reconnectUART() brings the UART up and sends CONNECT immediately, ahead of the grace delay.
    ASSERT_EQ(hp().state_, esphome::DriverState::BOOT);
    std::vector<uint8_t> set = set_heat_frame();
    hp().writePacket(set.data(), 22);

    const auto frames = take_tx_frames();
    ASSERT_EQ(frames.size(), 1u);
    EXPECT_EQ(frames[0], kConnectFrame) << hex(frames[0]);
    EXPECT_EQ(hp().state_, esphome::DriverState::CONNECTING);
    EXPECT_TRUE(hp().has_pending_packet_);
}

TEST_F(HpWritings, Characterize_PendingRetriesEvery2sWhileUartNotReady) {
    // The UART is "not ready" (state below CONNECTING) only before the first connection: no production
    // path leads back to BOOT/WAIT_*, and disconnectUART() from CONNECTED lands on DISCONNECTED, which
    // counts as ready. The plan's reading ("force the UART below CONNECTING through disconnectUART()") does
    // not hold; the 2000 ms re-arm is reached with a UART that setupUART() refuses to configure
    // (parity changed after SetUp), so the state never leaves BOOT.
    uart_.set_parity(esphome::uart::UART_CONFIG_PARITY_NONE);
    std::vector<uint8_t> set = set_heat_frame();
    hp().writePacket(set.data(), 22);

    EXPECT_EQ(hp().state_, esphome::DriverState::BOOT);
    EXPECT_TRUE(take_tx().empty()) << "no CONNECT: the UART cannot be configured";
    EXPECT_TRUE(hp().has_pending_packet_);
    const size_t not_configured_after_write = cn105_test::TestLog::count("UART is not configured as SERIAL_8E1");
    EXPECT_GT(not_configured_after_write, 0u);

    // Only the ESPHome scheduler is run here (no loop()): the grace-delay bootstrap in loop() would itself
    // call setupUART()/sendFirstConnectionPacket() on every iteration and blur the retry timeline.
    // Each attempt also delays 750 ms through CUSTOM_DELAY in sendFirstConnectionPacket (the test clock
    // jumps), and the next "write" timeout is armed after that delay.
    std::vector<uint32_t> retry_times;
    size_t last_count = cn105_test::TestLog::count("reconnectUART()");
    const uint32_t t0 = cn105_test::TestClock::now();
    for (uint32_t guard = 0; guard < 2000 && retry_times.size() < 4; guard++) {
        cn105_test::TestClock::advance(10);
        esphome::App.scheduler.call(esphome::millis());
        const size_t count = cn105_test::TestLog::count("reconnectUART()");
        if (count != last_count) retry_times.push_back(cn105_test::TestClock::now() - t0);
        last_count = count;
    }
    ASSERT_EQ(retry_times.size(), 4u);
    // Observed: first retry about 4000 ms after the write (the write itself took the 750 ms delay), then one
    // attempt every 2000 ms + the 750 ms delay of the attempt itself.
    EXPECT_GE(retry_times[0], 4750u);
    EXPECT_LE(retry_times[0], 4770u);
    for (size_t i = 1; i < retry_times.size(); i++) {
        const uint32_t gap = retry_times[i] - retry_times[i - 1];
        EXPECT_GE(gap, 2750u) << "retry " << i;
        EXPECT_LE(gap, 2780u) << "retry " << i;
    }
    EXPECT_TRUE(hp().has_pending_packet_) << "the packet stays queued while the UART is not ready";
    EXPECT_TRUE(take_tx().empty());
}

// ════════════════════════════════════════════════════════════════
// BUG-04 — one pending slot, one reconnect per failed write
// ════════════════════════════════════════════════════════════════

TEST_F(HpWritings, Characterize_BUG04_InfoRequestOverwritesPendingCommand) {
    // Pins current buggy behaviour — flip in Phase 5 (LINK-02): the pending slot holds one packet, so an
    // info request queued after a user command replaces it; after reconnection only the info frame is sent.
    make_connection_inactive();
    ASSERT_FALSE(HasFatalFailure());

    std::vector<uint8_t> set = set_heat_frame();
    hp().writePacket(set.data(), 22);
    ASSERT_TRUE(hp().has_pending_packet_);
    ASSERT_EQ(std::vector<uint8_t>(hp().pending_packet_, hp().pending_packet_ + 22), set);
    take_tx();

    hp().buildAndSendInfoPacket(0x02);  // a poll request arriving while the command is still queued
    const std::vector<uint8_t> info = info_request_frame(0x02, 0x7B);
    EXPECT_TRUE(hp().has_pending_packet_);
    EXPECT_EQ(std::vector<uint8_t>(hp().pending_packet_, hp().pending_packet_ + 22), info)
        << "the SET frame was replaced by the info request: " << hex(std::vector<uint8_t>(hp().pending_packet_, hp().pending_packet_ + 22));
    take_tx();

    // Restore the link with the real CONNECT reply, then let the 4000 ms retry elapse and then some.
    feed(cn105_test::real_frames::CONNECT_REPLY);
    step(10);
    ASSERT_TRUE(hp().isHeatpumpConnected());
    std::vector<std::vector<uint8_t>> on_wire;
    std::vector<uint32_t> info_times;  // ms after the link came back
    for (uint32_t elapsed = 0; elapsed < 8000; elapsed += 10) {
        step(10);
        for (auto& f : take_tx_frames()) {
            on_wire.push_back(f);
            if (f == info) info_times.push_back(elapsed);
        }
    }
    EXPECT_EQ(count_command(on_wire, 0x41), 0u) << "the user's SET frame was never transmitted";
    // Observed: regular poll requests at about +3040 and +7060 ms, and the retried pending write at about
    // +3980 ms (4000 ms after it was queued, the "write" timeout having been re-armed by the info request):
    // that one is the replaced slot content.
    ASSERT_EQ(info_times.size(), 3u) << "two poll requests and the retried pending write";
    EXPECT_GE(info_times[1], 3900u);
    EXPECT_LE(info_times[1], 4000u);
    EXPECT_FALSE(hp().has_pending_packet_);
}

TEST_F(HpWritings, Characterize_BUG04_EachFailedWriteTriggersReconnect) {
    // Pins current buggy behaviour — flip in Phase 5 (LINK-01): every refused write starts its own
    // reconnect instead of sharing the one already in progress, so n failed writes give n CONNECT bursts.
    make_connection_inactive();
    ASSERT_FALSE(HasFatalFailure());
    const size_t reconnects_before = cn105_test::TestLog::count("reconnectUART()");
    const unsigned int connections_before = hp().nbHeatpumpConnections_;

    std::vector<uint8_t> set = set_heat_frame();
    hp().writePacket(set.data(), 22);
    hp().buildAndSendInfoPacket(0x03);
    hp().buildAndSendInfoPacket(0x06);

    const auto frames = take_tx_frames();
    ASSERT_EQ(frames.size(), 3u);
    for (const auto& f : frames) EXPECT_EQ(f, kConnectFrame) << hex(f);
    EXPECT_EQ(cn105_test::TestLog::count("reconnectUART()") - reconnects_before, 3u);
    EXPECT_EQ(hp().nbHeatpumpConnections_ - connections_before, 3u);
    EXPECT_EQ(cn105_test::TestLog::count("could not write as asked"), 3u);
}

// ════════════════════════════════════════════════════════════════
// BUG-03 — wanted state published to HA and cleared although only queued
// ════════════════════════════════════════════════════════════════

TEST_F(HpWritings, Characterize_BUG03_WantedSettingsPublishedAndClearedAfterQueuedWrite) {
    // Pins current buggy behaviour — flip in Phase 6 (CMD-04): sendWantedSettingsDelegate() publishes the
    // wanted state to HA and resets wantedSettings right after writePacket(), even when writePacket() only
    // queued the packet because the link is down. HA then shows a state the heat pump never received.
    make_connection_inactive();
    ASSERT_FALSE(HasFatalFailure());

    hp().make_call().set_mode(esphome::climate::CLIMATE_MODE_COOL).set_target_temperature(24.0f).perform();
    ASSERT_TRUE(hp().wantedSettings.hasChanged);
    ASSERT_STREQ(hp().wantedSettings.mode, "COOL");
    publishes_.clear();
    take_tx();

    hp().sendWantedSettingsDelegate();

    // Wire: only a CONNECT burst. The SET frame (0x41) is in the pending slot.
    const auto frames = take_tx_frames();
    ASSERT_EQ(frames.size(), 1u);
    EXPECT_EQ(frames[0], kConnectFrame) << hex(frames[0]);
    ASSERT_TRUE(hp().has_pending_packet_);
    EXPECT_EQ(hp().pending_packet_[1], 0x41);
    EXPECT_EQ(hp().pending_packet_[5], 0x01);
    EXPECT_EQ(cn105_protocol::checksum(hp().pending_packet_, 21), hp().pending_packet_[21]);

    // HA: published, with the wanted mode and temperature.
    ASSERT_FALSE(publishes_.empty());
    EXPECT_EQ(publishes_.back().mode, esphome::climate::CLIMATE_MODE_COOL);
    EXPECT_FLOAT_EQ(publishes_.back().target_temperature, 24.0f);

    // Component: wanted settings cleared as if sent, and the polling cycle deferred.
    EXPECT_FALSE(hp().wantedSettings.hasChanged);
    EXPECT_FALSE(hp().wantedSettings.hasBeenSent);
    EXPECT_EQ(hp().wantedSettings.mode, nullptr);
    EXPECT_EQ(hp().wantedSettings.temperature, -1.0f);
    EXPECT_GT(hp().loopCycle.lastCompleteCycleMs, static_cast<unsigned long>(cn105_test::TestClock::now()))
        << "deferCycle() ran unconditionally";
}

TEST_F(HpWritings, Characterize_BUG03_NoRetryOfClearedWantedSettings) {
    // Pins current buggy behaviour — flip in Phase 6 (CMD-04): once cleared, nothing re-sends the wanted
    // settings; if the queued packet is lost (see Characterize_RetryStillInactiveReconnectsAgainAndForgetsPacket)
    // the command is silently gone and HA keeps showing it.
    make_connection_inactive();
    ASSERT_FALSE(HasFatalFailure());
    hp().make_call().set_mode(esphome::climate::CLIMATE_MODE_COOL).set_target_temperature(24.0f).perform();
    hp().sendWantedSettingsDelegate();
    take_tx();
    publishes_.clear();

    run_for(30000);
    EXPECT_FALSE(hp().wantedSettings.hasChanged);
    EXPECT_EQ(count_command(take_tx_frames(), 0x41), 0u) << "the SET frame never went out";
    ASSERT_FALSE(publishes_.empty());
    EXPECT_EQ(publishes_.back().mode, esphome::climate::CLIMATE_MODE_COOL);
}

class HpWritingsAirPurifier : public HpWritings {
   protected:
    void before_setup(esphome::CN105Climate& hp) override { hp.set_air_purifier_switch(&air_purifier_); }
    esphome::HVACOptionSwitch air_purifier_;
};

TEST_F(HpWritingsAirPurifier, Characterize_BUG03_RunStatesPublishedAndClearedAfterQueuedWrite) {
    // Pins current buggy behaviour — flip in Phase 6 (CMD-04): sendWantedRunStates() has no connection
    // check; it writes (queued here), publishes the optimistic switch state and resets wantedRunStates.
    make_connection_inactive();
    ASSERT_FALSE(HasFatalFailure());
    ASSERT_FALSE(air_purifier_.state);

    hp().wantedRunStates.air_purifier = 1;
    hp().wantedRunStates.hasChanged = true;
    hp().wantedRunStates.lastChange = static_cast<long>(cn105_test::TestClock::now());

    hp().sendWantedRunStates();

    const auto frames = take_tx_frames();
    ASSERT_EQ(frames.size(), 1u);
    EXPECT_EQ(frames[0], kConnectFrame) << hex(frames[0]);
    ASSERT_TRUE(hp().has_pending_packet_);
    EXPECT_EQ(hp().pending_packet_[1], 0x41);
    EXPECT_EQ(hp().pending_packet_[5], 0x08) << "run-state packet";
    EXPECT_EQ(hp().pending_packet_[17], 1) << "air purifier on";
    EXPECT_EQ(cn105_protocol::checksum(hp().pending_packet_, 21), hp().pending_packet_[21]);

    // HA sees the switch ON; the component's mirror of the unit state was never updated (still the -1
    // "unknown" set by the reconnect handshake reset).
    EXPECT_TRUE(air_purifier_.state);
    EXPECT_EQ(hp().currentRunStates.air_purifier, -1);
    // Wanted run states cleared as if sent, cycle deferred.
    EXPECT_FALSE(hp().wantedRunStates.hasChanged);
    EXPECT_EQ(hp().wantedRunStates.air_purifier, -1);
    EXPECT_GT(hp().loopCycle.lastCompleteCycleMs, static_cast<unsigned long>(cn105_test::TestClock::now()));
}
