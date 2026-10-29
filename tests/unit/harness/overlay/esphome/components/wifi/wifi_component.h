/// wifi_component.h — minimal Wi-Fi stub for the host harness
/// Role: lets componentEntries.cpp reach `wifi::global_wifi_component->is_connected()`.
///       The object is created, toggled and removed through cn105_test::TestWifi.
/// Deps: none
#pragma once
namespace esphome {
namespace wifi {
class WiFiComponent {
 public:
  bool is_connected() const { return connected_; }
  bool connected_ = false;
};
extern WiFiComponent *global_wifi_component;
}  // namespace wifi
}  // namespace esphome
