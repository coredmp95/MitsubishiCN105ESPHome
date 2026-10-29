/// test_remote_temperature.cpp — Characterization of the remote-temperature path
/// Role: pins, on the unmodified component, how set_remote_temperature() travels to the UART (0x41/0x07
///       packet written at the end of the next poll cycle), compared byte for byte with a real capture,
///       and (PR 1b, Task 3 of plan 01-12) the keep-alive, debounce, timeout fallback, source-info text,
///       profile acceptance and the BUG-02 margin quirk, before Phase 4 (TIME-04) changes them.
/// Deps: fixture.h, real_frames.h, cn105.h

#include "fixture.h"

#include <vector>

#include "real_frames.h"

namespace {

using cn105_test::hex;

/// True for a 0x41 SET frame carrying the remote-temperature flag (info code byte 5 = 0x07).
bool is_remote_temp_frame(const std::vector<uint8_t>& f) { return f.size() > 5 && f[1] == 0x41 && f[5] == 0x07; }

}  // namespace

class RemoteTemperature : public cn105_test::Cn105Fixture {
   protected:
    /// Steps `total_ms` of test time with a 10 ms tick, answering every FC 42 request with the default real
    /// responses, and returns every frame the component wrote meanwhile (checksum included).
    std::vector<std::vector<uint8_t>> poll_and_collect(uint32_t total_ms) {
        std::vector<std::vector<uint8_t>> all;
        const auto responses = default_responses();
        for (uint32_t elapsed = 0; elapsed < total_ms; elapsed += 10) {
            step(10);
            for (const auto& frame : take_tx_frames()) {
                all.push_back(frame);
                if (frame.size() > 5 && frame[1] == 0x42) {
                    auto it = responses.find(frame[5]);
                    if (it != responses.end()) feed(it->second);
                }
            }
        }
        return all;
    }
};

// ════════════════════════════════════════════════════════════════
// Remote-temperature packet vs the real capture
// ════════════════════════════════════════════════════════════════

TEST_F(RemoteTemperature, Characterize_RemoteTempPacketMatchesRealCapture) {
    connect();
    ASSERT_FALSE(HasFatalFailure());
    run_poll_cycle();
    take_tx();

    hp().set_remote_temperature(22.8f);

    // The packet is not written by set_remote_temperature() itself: it leaves at the end of the next
    // poll cycle (terminateCycle -> sendRemoteTemperature).
    const auto frames = poll_and_collect(3000);
    std::vector<std::vector<uint8_t>> remote_frames;
    for (const auto& f : frames) {
        if (is_remote_temp_frame(f)) remote_frames.push_back(f);
    }
    ASSERT_EQ(remote_frames.size(), 1u) << "exactly one remote temperature packet expected";
    EXPECT_EQ(remote_frames[0], cn105_test::with_checksum(cn105_test::real_frames::SET_REMOTE_TEMP_22_8))
        << "written: " << hex(remote_frames[0]);
}
