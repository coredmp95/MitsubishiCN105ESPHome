/// test_connection_lifecycle.cpp — Characterization of the CN105 connection state machine
/// Role: pins, on the unmodified component, how CN105Climate leaves BOOT (WAIT_WIFI / WAIT_GRACE), starts
///       the handshake and connects, with the Wi-Fi component present but not connected (PR 1b, D-15).
///       Task 2 of plan 01-13 extends it with the bootstrap timeout, the retry and reconnect timing and the
///       BUG-05 / BUG-08 pins.
/// Deps: fixture.h, real_frames.h, test_platform.h

#include "fixture.h"

#include <cstdint>
#include <vector>

#include "real_frames.h"
#include "test_platform.h"

namespace {

using cn105_test::TestLog;
using cn105_test::TestWifi;

/// CONNECT as CN105Climate::sendFirstConnectionPacket() writes it for a standard heat pump (profile 0x30).
const std::vector<uint8_t> kConnectFrame = {0xFC, 0x5A, 0x01, 0x30, 0x02, 0xCA, 0x01, 0xA8};

}  // namespace

// ════════════════════════════════════════════════════════════════
// Wi-Fi gated bootstrap: the Wi-Fi component exists but is not connected
// ════════════════════════════════════════════════════════════════

/// Wi-Fi component present and disconnected when hp.setup() runs, as on a real board that just booted.
class ConnectionLifecycleWifi : public cn105_test::Cn105Fixture {
   protected:
    void before_setup(esphome::CN105Climate& hp) override {
        (void) hp;
        TestWifi::install(false);
    }
};

TEST_F(ConnectionLifecycleWifi, Characterize_WaitsForWifiBeforeConnect) {
    // setup() does not touch the state machine: the FSM starts in loop().
    EXPECT_EQ(hp().driver_state(), esphome::DriverState::BOOT);

    // First loop: BOOT -> WAIT_WIFI because the Wi-Fi component is present and not connected.
    step(10);
    EXPECT_EQ(hp().driver_state(), esphome::DriverState::WAIT_WIFI);
    EXPECT_TRUE(TestLog::contains("State: BOOT -> WAIT_WIFI"));
    EXPECT_TRUE(TestLog::contains("attente WiFi avant init UART/CONNECT"));

    // 60 s without Wi-Fi: still waiting, nothing is ever written, the grace delay (10 s) is not counted
    // while Wi-Fi is down, and the UART is not considered ready.
    run_for(60000, 100);
    EXPECT_EQ(hp().driver_state(), esphome::DriverState::WAIT_WIFI);
    EXPECT_TRUE(take_tx().empty());
    EXPECT_FALSE(hp().isUARTReady_());
    EXPECT_FALSE(TestLog::contains("State: WAIT_WIFI -> WAIT_GRACE"));

    // Wi-Fi comes up. The next loop leaves WAIT_WIFI for WAIT_GRACE. The grace delay is measured from
    // boot_ms_ (set by setup()), not from the moment Wi-Fi connects: 60 s have already elapsed, so the
    // delay is over and the CONNECT leaves on the following loop.
    TestWifi::set_connected(true);
    step(10);
    EXPECT_EQ(hp().driver_state(), esphome::DriverState::WAIT_GRACE);
    EXPECT_TRUE(TestLog::contains("State: WAIT_WIFI -> WAIT_GRACE"));
    EXPECT_TRUE(TestLog::contains("WiFi connecté"));
    EXPECT_TRUE(take_tx().empty());

    step(10);
    EXPECT_EQ(hp().driver_state(), esphome::DriverState::CONNECTING);
    EXPECT_TRUE(TestLog::contains("State: WAIT_GRACE -> CONNECTING"));
    EXPECT_EQ(take_tx(), kConnectFrame);
    EXPECT_FALSE(hp().isHeatpumpConnected());

    // The real FC 7A reply connects.
    feed(cn105_test::real_frames::CONNECT_REPLY);
    step(10);
    EXPECT_EQ(hp().driver_state(), esphome::DriverState::CONNECTED);
    EXPECT_TRUE(TestLog::contains("State: CONNECTING -> CONNECTED"));
    EXPECT_TRUE(hp().isHeatpumpConnected());
}
