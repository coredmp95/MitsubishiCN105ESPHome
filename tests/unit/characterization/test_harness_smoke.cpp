/// test_harness_smoke.cpp — proves the host harness: real CN105Climate on the real ESPHome 2026.10.0 core
/// Role: smoke tests of the harness itself (build flavour, fixture, test clock, platform control API).
/// Deps: fixture.h, test_platform.h, cn105_types.h (CONNECT), cn105_protocol.h (checksum)

#include "fixture.h"

#include <vector>

#include "cn105_protocol.h"

// ════════════════════════════════════════════════════════════════
// Build flavour
// ════════════════════════════════════════════════════════════════

// D-20: ILP32, so timestamp arithmetic behaves as on ESP32/ESP8266.
static_assert(sizeof(unsigned long) == 4, "the harness must be built with -m32");
// Fails if the pure-test stub version.h (ESPHOME_VERSION_CODE 2026.3.0) leaks onto the include path.
static_assert(ESPHOME_VERSION_CODE == VERSION_CODE(2026, 10, 0), "the real ESPHome 2026.10.0 headers must be used");

// ════════════════════════════════════════════════════════════════
// HarnessSmoke — the tracer: handshake on the test clock
// ════════════════════════════════════════════════════════════════

class HarnessSmoke : public cn105_test::Cn105Fixture {};

TEST_F(HarnessSmoke, ConnectFrameWrittenAfterGraceDelay) {
    // conn_bootstrap_delay_ms_ is 10000: nothing may be written before the grace delay elapsed.
    // Observed first-write window: loop at t=500 leaves BOOT, the first write happens at t=10000.
    for (int i = 1; i < 20; i++) {
        step(500);
        ASSERT_TRUE(uart_.tx.empty()) << "unexpected write at t=" << cn105_test::TestClock::now64() << " ms";
    }
    step(500);
    ASSERT_EQ(cn105_test::TestClock::now64(), 10000u);

    const std::vector<uint8_t> expected = {0xFC, 0x5A, 0x01, 0x30, 0x02, 0xCA, 0x01, 0xA8};
    EXPECT_EQ(take_tx(), expected);

    // The expected bytes are the CONNECT constant of cn105_types.h, checksum included.
    EXPECT_EQ(expected, std::vector<uint8_t>(CONNECT, CONNECT + CONNECT_LEN));
    EXPECT_EQ(cn105_protocol::checksum(expected.data(), static_cast<int>(expected.size()) - 1), expected.back());
}
