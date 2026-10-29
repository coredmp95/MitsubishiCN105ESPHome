/// captured_frames_1b.h — home of the real CN105 captures supplied by the maintainer for PR 1b
/// Role: every constant added here is a frame the maintainer recorded on a real unit, stored WITHOUT its
///       trailing checksum byte (Cn105Fixture::feed() and with_checksum() append it) and preceded by a comment
///       naming the capture file or log, the date and the scenario. Nothing synthetic is stored here: the two
///       D-23 synthetic frames (0x61 ACK, 0x7B installer reply) stay inline in test_hp_writings.cpp, labelled
///       SYNTHETIC, until a real capture replaces them.
/// Deps: <cstdint>, <vector>
///
/// State at the end of plan 01-12: the maintainer chose "existing frames only" (decision recorded in
/// 01-12-SUMMARY.md), so this namespace is intentionally empty. The scenarios still wanted are listed under
/// "Wanted captures" in tests/unit/README.md (capture tooling: examples/sniffer/cn105-sniffer.yaml,
/// tools/cn105_capture.py, docs/sniffing.md):
///   1. 0x42 HVAC-options reply (air purifier / night mode / circulator, each option toggled once)
///   2. 0x20 and 0x22 functions replies in installer mode
///   3. 0x61 ACK after a 0x41 SET (mode change) and after the 0x41/0x07 remote-temperature packet
///   4. 0x09 reply while the unit defrosts (DEFROST sub-mode)
///   5. 0x7B installer-mode connect reply to a 0x5B CONNECT (or proof that the unit ignores it)
///   6. Lossnay handshake reply to CONNECT 0x5A with profile 0x34 (FC 7A 01 34 ...)
/// Example of an entry once a capture exists:
///   // capture: logs/2026-11-02-defrost.txt, 14:31:07, unit defrosting after 40 min of HEAT at -3 C outside
///   inline const std::vector<uint8_t> POWER_DEFROST = {0xFC, 0x62, 0x01, 0x30, 0x10, 0x09, ...};

#pragma once

#include <cstdint>
#include <vector>

namespace cn105_test {
namespace captured_frames {

// (empty on purpose — see the header comment)

}  // namespace captured_frames
}  // namespace cn105_test
