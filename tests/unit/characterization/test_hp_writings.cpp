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

class HpWritings : public cn105_test::Cn105Fixture {};

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
