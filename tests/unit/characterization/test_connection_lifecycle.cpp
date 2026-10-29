/// test_connection_lifecycle.cpp — Characterization of the CN105 connection state machine
/// Role: pins, on the unmodified component, how CN105Climate leaves BOOT (WAIT_WIFI / WAIT_GRACE), starts
///       the handshake and connects, with the Wi-Fi component present but not connected (PR 1b, D-15); then
///       the 120 s bootstrap timeout, the first-connection retry, the reconnect timing, the 10 x
///       update_interval activity window, and the two link-level quirks pinned for later phases:
///       BUG-08 (signed delta in isHeatpumpConnectionActive, Phase 4 TIME-03) and BUG-05 (low-level UART
///       reinit falling back to UART0 / GPIO1 / GPIO2, Phase 5 LINK-05).
/// Deps: fixture.h, real_frames.h, test_platform.h

#include "fixture.h"

#include <cstdint>
#include <utility>
#include <vector>

#include "real_frames.h"
#include "test_platform.h"

namespace {

using cn105_test::TestLog;
using cn105_test::TestWifi;

/// CONNECT as CN105Climate::sendFirstConnectionPacket() writes it for a standard heat pump (profile 0x30).
const std::vector<uint8_t> kConnectFrame = {0xFC, 0x5A, 0x01, 0x30, 0x02, 0xCA, 0x01, 0xA8};

#ifdef USE_ESP32
/// force_low_level_uart_reinit() calls CUSTOM_DELAY(2) twice; on the test clock that is 4 ms the clock
/// really advances. The ESP8266 build compiles the function empty.
constexpr uint32_t kReinitDelayMs = 4;
#else
constexpr uint32_t kReinitDelayMs = 0;
#endif

}  // namespace

/// Base of the suites below: no Wi-Fi component (the harness default), a test clock stepped 1 ms at a time
/// where exact timestamps matter.
class ConnectionLifecycle : public cn105_test::Cn105Fixture {
   protected:
    using TimedFrame = std::pair<uint32_t, std::vector<uint8_t>>;

    /// Steps 1 ms at a time until the test clock reaches `until_ms`, collecting every frame the component
    /// writes with the clock value read right after the step (the write itself may happen inside it).
    /// With answer_polls, every info request (FC 42) is answered with the default real responses, which keeps
    /// the link alive like a healthy heat pump does.
    std::vector<TimedFrame> frames_until(uint32_t until_ms, bool answer_polls = false) {
        std::vector<TimedFrame> out;
        const auto responses = default_responses();
        while (cn105_test::TestClock::now() < until_ms) {
            step(1);
            for (auto& f : take_tx_frames()) {
                if (answer_polls && f.size() > 5 && f[1] == 0x42) {
                    auto it = responses.find(f[5]);
                    if (it != responses.end()) feed(it->second);
                }
                out.emplace_back(cn105_test::TestClock::now(), std::move(f));
            }
        }
        return out;
    }

    /// Only the CONNECT frames of frames_until().
    std::vector<uint32_t> connect_times_until(uint32_t until_ms, bool answer_polls = false) {
        std::vector<uint32_t> times;
        for (const auto& tf : frames_until(until_ms, answer_polls)) {
            if (tf.second == kConnectFrame) times.push_back(tf.first);
        }
        return times;
    }
};

// ════════════════════════════════════════════════════════════════
// Wi-Fi gated bootstrap: the Wi-Fi component exists but is not connected
// ════════════════════════════════════════════════════════════════

/// Wi-Fi component present and disconnected when hp.setup() runs, as on a real board that just booted.
class ConnectionLifecycleWifi : public ConnectionLifecycle {
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

// ════════════════════════════════════════════════════════════════
// Bootstrap timeout: 120 s after the first loop, the UART is forced without Wi-Fi
// ════════════════════════════════════════════════════════════════

TEST_F(ConnectionLifecycleWifi, Characterize_BootstrapTimeoutForcesConnectWithoutWifi) {
    // Wi-Fi never comes up. The "cn105_bootstrap_timeout" timeout is armed by the first loop (t = 1 ms) and
    // fires 120000 ms later.
    const auto frames = frames_until(125000);
    ASSERT_FALSE(frames.empty()) << "no CONNECT written at all";
    EXPECT_EQ(frames.front().second, kConnectFrame);
    EXPECT_EQ(frames.front().first, 120001u);

    // The warning is logged by the timeout callback; WAIT_GRACE is never entered, setupUART() moves the FSM
    // straight from WAIT_WIFI to CONNECTING.
    bool warned = false;
    for (const auto& r : TestLog::records()) {
        if (r.message.find("timeout 120s") != std::string::npos) warned = (r.level == ESPHOME_LOG_LEVEL_WARN);
    }
    EXPECT_TRUE(warned) << "the 120 s timeout must log a warning";
    EXPECT_TRUE(TestLog::contains("State: WAIT_WIFI -> CONNECTING"));
    EXPECT_FALSE(TestLog::contains("State: WAIT_WIFI -> WAIT_GRACE"));
    EXPECT_EQ(hp().driver_state(), esphome::DriverState::CONNECTING);

    // The handshake then proceeds normally.
    feed(cn105_test::real_frames::CONNECT_REPLY);
    step(10);
    EXPECT_TRUE(hp().isHeatpumpConnected());
}

TEST_F(ConnectionLifecycle, Characterize_BootstrapTimeoutIgnoredOnceConnecting) {
    // No Wi-Fi component: the regular path connects at 10 s and the heat pump keeps answering its polls;
    // the timeout armed at t = 1 ms then fires at 120 s, sees state >= CONNECTING and does nothing.
    connect();
    ASSERT_FALSE(HasFatalFailure());
    take_tx();
    TestLog::clear();

    const auto connects = connect_times_until(125000, true);
    EXPECT_TRUE(connects.empty()) << "a second CONNECT left although the heat pump is connected";
    EXPECT_FALSE(TestLog::contains("timeout 120s"));
}

// ════════════════════════════════════════════════════════════════
// First connection: no reply to CONNECT
// ════════════════════════════════════════════════════════════════

TEST_F(ConnectionLifecycle, Characterize_FirstConnectionRetryWithoutReply) {
    // Heat pump silent. CONNECT leaves when the 10 s grace delay is over; "checkFirstConnection" fires 10 s
    // after the write, finds the heat pump not connected and calls reconnectUART(), which writes CONNECT
    // again. Each retry re-arms the check: one CONNECT every 10 s (+ the 4 ms reinit delay on ESP32).
    const auto connects = connect_times_until(45000);
    const uint32_t gap = 10000 + kReinitDelayMs;
    ASSERT_EQ(connects.size(), 4u);
    EXPECT_EQ(connects[0], 10000u);
    EXPECT_EQ(connects[1], connects[0] + gap);
    EXPECT_EQ(connects[2], connects[1] + gap);
    EXPECT_EQ(connects[3], connects[2] + gap);

    EXPECT_EQ(TestLog::count("Heatpump did not reply: NOT CONNECTED"), 3u);
    EXPECT_EQ(TestLog::count("Reinitializing UART and trying to connect again"), 3u);
    EXPECT_EQ(TestLog::count("reconnectUART()"), 3u);
    // The FSM stays in CONNECTING: no DISCONNECTED transition (it was never CONNECTED).
    EXPECT_EQ(hp().driver_state(), esphome::DriverState::CONNECTING);
    EXPECT_FALSE(TestLog::contains("-> DISCONNECTED"));

    // A late reply is accepted as the very first connection.
    feed(cn105_test::real_frames::CONNECT_REPLY);
    step(10);
    EXPECT_TRUE(hp().isHeatpumpConnected());
}

/// Installer mode requested in YAML: CONNECT is the installer variant (FC 5B) first.
class ConnectionLifecycleInstaller : public ConnectionLifecycle {
   protected:
    cn105_test::Cn105Options options() const override {
        cn105_test::Cn105Options opts;
        opts.installer_mode = true;
        return opts;
    }
};

TEST_F(ConnectionLifecycleInstaller, Characterize_InstallerHandshakeFallsBackToStandardOnce) {
    const std::vector<uint8_t> installer_connect = cn105_test::with_checksum({0xFC, 0x5B, 0x01, 0x30, 0x02, 0xCA, 0x01});

    const auto frames = frames_until(35000);
    std::vector<TimedFrame> connects;
    for (const auto& tf : frames) {
        if (tf.second[1] == 0x5A || tf.second[1] == 0x5B) connects.push_back(tf);
    }
    ASSERT_GE(connects.size(), 3u);
    // First attempt: installer handshake. The heat pump does not reply: the check logs the fallback, switches
    // to the standard handshake once, and every following attempt is standard (0x5A).
    EXPECT_EQ(connects[0].first, 10000u);
    EXPECT_EQ(connects[0].second, installer_connect) << cn105_test::hex(connects[0].second);
    EXPECT_EQ(connects[1].second, kConnectFrame) << cn105_test::hex(connects[1].second);
    EXPECT_EQ(connects[2].second, kConnectFrame) << cn105_test::hex(connects[2].second);
    EXPECT_EQ(TestLog::count("Falling back to standard handshake"), 1u);
}

// ════════════════════════════════════════════════════════════════
// reconnectIfConnectionLost and the 10 x update_interval activity window
// ════════════════════════════════════════════════════════════════

TEST_F(ConnectionLifecycle, Characterize_ConnectionInactiveAfterTenIntervals) {
    connect();
    ASSERT_FALSE(HasFatalFailure());

    // isHeatpumpConnectionActive(): (millis() - lastResponseMs) < 10 * update_interval_, strict.
    // update_interval is 1000 ms here: active up to 9999 ms of silence, inactive from 10000 ms.
    const uint32_t t0 = 20000;
    hp().lastResponseMs = t0;
    cn105_test::TestClock::set(t0 + 9999);
    EXPECT_TRUE(hp().isHeatpumpConnectionActive());
    cn105_test::TestClock::set(t0 + 10000);
    EXPECT_FALSE(hp().isHeatpumpConnectionActive());

    // The window follows the update interval.
    hp().set_update_interval(2000);
    cn105_test::TestClock::set(t0 + 19999);
    EXPECT_TRUE(hp().isHeatpumpConnectionActive());
    cn105_test::TestClock::set(t0 + 20000);
    EXPECT_FALSE(hp().isHeatpumpConnectionActive());
}

TEST_F(ConnectionLifecycle, Characterize_ReconnectIfConnectionLostWaitsUpdateInterval) {
    connect();
    ASSERT_FALSE(HasFatalFailure());
    take_tx();
    const uint32_t last_response = static_cast<uint32_t>(hp().lastResponseMs);

    // Link healthy (9999 ms of silence): nothing happens, however old the last reconnect is.
    cn105_test::TestClock::set(last_response + 9999);
    hp().reconnectIfConnectionLost();
    EXPECT_TRUE(take_tx().empty());
    EXPECT_TRUE(hp().isHeatpumpConnected());

    // 10 s of silence: connection judged lost, reconnectUART() runs and CONNECT leaves.
    cn105_test::TestClock::set(last_response + 10000);
    hp().reconnectIfConnectionLost();
    EXPECT_TRUE(TestLog::contains("Heatpump has not replied for 10 s"));
    EXPECT_TRUE(TestLog::contains("We think Heatpump is not connected anymore"));
    EXPECT_EQ(take_tx(), kConnectFrame);
    EXPECT_EQ(hp().driver_state(), esphome::DriverState::CONNECTING);
    EXPECT_TRUE(TestLog::contains("State: CONNECTED -> DISCONNECTED"));

    // lastReconnectTimeMs / lastConnectRqTimeMs were stamped when CONNECT was written.
    const uint32_t t_connect = static_cast<uint32_t>(hp().lastConnectRqTimeMs);
    EXPECT_EQ(t_connect, last_response + 10000 + kReinitDelayMs);
    EXPECT_EQ(static_cast<uint32_t>(hp().lastReconnectTimeMs), t_connect);

    // Right away and until update_interval (1000 ms) after the last attempt: no new CONNECT.
    hp().reconnectIfConnectionLost();
    cn105_test::TestClock::set(t_connect + 999);
    hp().reconnectIfConnectionLost();
    EXPECT_TRUE(take_tx().empty());

    // Exactly update_interval after: the reconnect gate (< interval) opens but the CONNECT gate
    // (now - lastConnectRqTimeMs > interval, strict) is still closed.
    cn105_test::TestClock::set(t_connect + 1000);
    hp().reconnectIfConnectionLost();
    EXPECT_TRUE(take_tx().empty());

    // One millisecond later both gates are open: second CONNECT.
    cn105_test::TestClock::set(t_connect + 1001);
    hp().reconnectIfConnectionLost();
    EXPECT_EQ(take_tx(), kConnectFrame);
}

// ════════════════════════════════════════════════════════════════
// BUG-08: wall-clock gaps are held in a signed `long`
// ════════════════════════════════════════════════════════════════

// Pins current buggy behaviour — flip in Phase 4 (TIME-03)
// reconnectIfConnectionLost() and isHeatpumpConnectionActive() store (millis() - timestamp) in a signed
// `long`. Under ILP32 (ESP32, ESP8266 and this -m32 harness) a gap of 2^31 ms or more (24.9 days without any
// reply) turns negative. The decisions are NOT affected: the delta is compared with uint32_t operands
// (update_interval_), which converts it back to unsigned, so such a link is still judged inactive and
// reconnected. This differs from the plan's expectation (link reported active), observed on the unmodified
// component. What the signed `long` does break is the diagnostic: the warning prints the gap as a negative
// number of seconds. Phase 4 (TIME-03) moves every timestamp and gap to uint32_t.
TEST_F(ConnectionLifecycle, Characterize_BUG08_ConnectionActiveSignedOverflow) {
    connect();
    ASSERT_FALSE(HasFatalFailure());
    take_tx();

    const uint32_t last_response = 1000;
    hp().lastResponseMs = last_response;

    // Just below the signed limit: positive gap, inactive.
    cn105_test::TestClock::set(static_cast<uint64_t>(last_response) + 0x7FFFFFFFull);
    EXPECT_FALSE(hp().isHeatpumpConnectionActive());

    // From 2^31 ms the signed delta is negative, yet the link is still inactive (unsigned comparison).
    cn105_test::TestClock::set(static_cast<uint64_t>(last_response) + 0x80000000ull);
    EXPECT_FALSE(hp().isHeatpumpConnectionActive());
    cn105_test::TestClock::set(static_cast<uint64_t>(last_response) + 0xFFFFFFFFull);
    EXPECT_FALSE(hp().isHeatpumpConnectionActive());

    // The reconnect happens, and its warning shows the gap through the signed `long`: -2147483 s instead of
    // 2147483 s (0x80000000 ms = 2147483648 ms wraps to -2147483648 ms, / 1000 = -2147483 s).
    TestLog::clear();
    cn105_test::TestClock::set(static_cast<uint64_t>(last_response) + 0x80000000ull);
    hp().reconnectIfConnectionLost();
    EXPECT_EQ(take_tx(), kConnectFrame);
    EXPECT_TRUE(TestLog::contains("Heatpump has not replied for -2147483 s"));
}

#ifdef USE_ESP32
// ════════════════════════════════════════════════════════════════
// force_low_level_uart_reinit (ESP32 variant: the IDF calls are recorded by the harness stubs)
// ════════════════════════════════════════════════════════════════

namespace {

/// The recorded IDF call as a compact "fn(a0,a1,a2,a3)" string, for readable failure messages.
std::string render(const cn105_test::IdfCall& c) {
    return c.fn + "(" + std::to_string(c.a0) + "," + std::to_string(c.a1) + "," + std::to_string(c.a2) + "," +
           std::to_string(c.a3) + ")";
}

std::vector<std::string> render_all(const std::vector<cn105_test::IdfCall>& calls) {
    std::vector<std::string> out;
    for (const auto& c : calls) out.push_back(render(c));
    return out;
}

}  // namespace

// Pins current buggy behaviour — flip in Phase 5 (LINK-05) after the on-board verification
// These are calls recorded on a host stub, not the real ESP-IDF driver (T-01-26): Phase 5 must check on a
// real board what the driver does before changing this. With no set_uart_port() and no set_tx_rx_pins()
// (both default to -1), force_low_level_uart_reinit() reconfigures UART_NUM_0 and routes it to GPIO1 (tx)
// and GPIO2 (rx), the "Atom S3 yaml" pins of a comment in the production code, whatever board runs it.
TEST_F(ConnectionLifecycle, Characterize_BUG05_LowLevelReinitTargetsUart0Pins1And2) {
    connect();
    ASSERT_FALSE(HasFatalFailure());
    cn105_test::IdfRecorder::clear();
    TestLog::clear();

    // reconnectUART() is the production path of every reconnect (silent heat pump, failed first connection).
    hp().reconnectUART();

    EXPECT_TRUE(TestLog::contains("Forcing low-level UART reinit on port 0 (tx=-1, rx=-1)"));
    // Negative pins: no gpio_reset_pin, no pull-up. Then uart_param_config on port 0 (2400 baud) and
    // uart_set_pin(port 0, tx 1, rx 2, rts -1 = UART_PIN_NO_CHANGE).
    const std::vector<std::string> expected = {"uart_param_config(0,2400,0,0)", "uart_set_pin(0,1,2,-1)"};
    EXPECT_EQ(render_all(cn105_test::IdfRecorder::calls()), expected);
    EXPECT_EQ(take_tx(), kConnectFrame);
}

TEST_F(ConnectionLifecycle, Characterize_LowLevelReinitUnknownPortFallsBackToUart0) {
    connect();
    ASSERT_FALSE(HasFatalFailure());
    hp().set_uart_port(5);  // neither 1 nor 2
    cn105_test::IdfRecorder::clear();

    hp().reconnectUART();

    const std::vector<std::string> expected = {"uart_param_config(0,2400,0,0)", "uart_set_pin(0,1,2,-1)"};
    EXPECT_EQ(render_all(cn105_test::IdfRecorder::calls()), expected);
}

/// UART port 2 on GPIO17 (tx) / GPIO16 (rx), as set by climate.py to_code from the YAML.
class ConnectionLifecycleConfiguredUart : public ConnectionLifecycle {
   protected:
    void before_setup(esphome::CN105Climate& hp) override {
        hp.set_uart_port(2);
        hp.set_tx_rx_pins(17, 16);
    }
};

TEST_F(ConnectionLifecycleConfiguredUart, Characterize_LowLevelReinitUsesConfiguredPortAndPins) {
    connect();
    ASSERT_FALSE(HasFatalFailure());
    cn105_test::IdfRecorder::clear();
    TestLog::clear();

    hp().reconnectUART();

    EXPECT_TRUE(TestLog::contains("Forcing low-level UART reinit on port 2 (tx=17, rx=16)"));
    // Both pins reset, port 2 configured and routed to the configured pins, then the rx pull-up
    // (gpio_set_pull_mode(pin 16, GPIO_PULLUP_ONLY)).
    const auto calls = render_all(cn105_test::IdfRecorder::calls());
    ASSERT_EQ(calls.size(), 5u) << ::testing::PrintToString(calls);
    EXPECT_EQ(calls[0], "gpio_reset_pin(17,0,0,0)");
    EXPECT_EQ(calls[1], "gpio_reset_pin(16,0,0,0)");
    EXPECT_EQ(calls[2], "uart_param_config(2,2400,0,0)");
    EXPECT_EQ(calls[3], "uart_set_pin(2,17,16,-1)");
    EXPECT_EQ(calls[4].rfind("gpio_set_pull_mode(16,", 0), 0u) << calls[4];
}
#endif  // USE_ESP32
