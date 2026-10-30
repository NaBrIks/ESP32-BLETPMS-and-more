#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <string_view>

#ifndef APP_TPMS_BLE_TOMTOM
#define APP_TPMS_BLE_TOMTOM 1
#endif
namespace application {
enum class TpmsTransport { ble };
enum class TpmsProtocol { tomtom };
inline constexpr std::string_view transport_name(TpmsTransport t) {
  switch (t) {
  case TpmsTransport::ble: return "ble";
  }
  return {};
}
inline constexpr std::string_view protocol_name(TpmsProtocol p) {
  switch (p) {
  case TpmsProtocol::tomtom: return "tomtom";
  }
  return {};
}
struct TpmsDriver {
  std::string_view id;
  TpmsTransport transport;
  TpmsProtocol protocol;
  std::string_view label;
};
#if APP_TPMS_BLE_TOMTOM
inline constexpr std::array<TpmsDriver, 1> tpms_drivers{
    {{"tomtom-ble", TpmsTransport::ble, TpmsProtocol::tomtom, "TomTom BLE"}}};
#else
inline constexpr std::array<TpmsDriver, 0> tpms_drivers{};
#endif
inline const TpmsDriver *tpms_driver(std::string_view id) {
  for (const auto &d : tpms_drivers)
    if (d.id == id)
      return &d;
  return nullptr;
}
// A search session compares integer Pa with its starting measurement, not with
// the previous packet. Small cumulative changes count; the marker is latched.
struct PressureProbe {
  bool initialized{}, changed{};
  uint32_t baseline_pa{};
  int64_t delta_pa{}, peak_delta_pa{};
  uint64_t last_ms{};
  void reset() { *this = {}; }
  void sample(uint32_t pa, uint64_t ms) {
    if (initialized && ms <= last_ms)
      return;
    last_ms = ms;
    if (!initialized) {
      initialized = true;
      baseline_pa = pa;
      return;
    }
    delta_pa = int64_t(pa) - int64_t(baseline_pa);
    const auto magnitude = [](int64_t n) { return n < 0 ? -n : n; };
    if (magnitude(delta_pa) > magnitude(peak_delta_pa))
      peak_delta_pa = delta_pa;
    if (magnitude(delta_pa) >= 10000)
      changed = true;
  }
};
struct TpmsObservation {
  std::string discovery_id, address;
  uint8_t address_type{};
  uint32_t code{}, pressure_pa{};
  int32_t temperature_centi_c{};
  uint8_t battery{}, sensor_number{};
  bool alarm{};
  uint64_t last_ms{};
  PressureProbe probe;
};

} // namespace application
