#include "../src/modules/tpms_types.hpp"
#include <cassert>
#include <iostream>

int main() {
  using namespace application;
  PressureProbe p;
  p.sample(230000, 10);
  assert(p.initialized && !p.changed && p.baseline_pa == 230000);
  p.sample(234000, 20);
  p.sample(238000, 30);
  assert(!p.changed);
  p.sample(240000, 40); // cumulative change, inclusive 0.10 bar boundary
  assert(p.changed && p.delta_pa == 10000 && p.peak_delta_pa == 10000);
  p.sample(230000, 50);
  assert(p.changed && p.delta_pa == 0 && p.peak_delta_pa == 10000);
  p.sample(0, 49); // out-of-order and duplicate reports cannot change the probe
  p.sample(0, 50);
  assert(p.delta_pa == 0 && p.last_ms == 50);
  p.reset();
  assert(!p.initialized && !p.changed);
  p.sample(230000, 60);
  p.sample(220001, 70);
  assert(!p.changed);
  p.sample(220000, 80);
  assert(p.changed && p.delta_pa == -10000);
  p.sample(210000, 90);
  assert(p.peak_delta_pa == -20000);
  p.reset();
  p.sample(0, 100);
  p.sample(UINT32_MAX, 101);
  assert(p.changed && p.delta_pa == int64_t(UINT32_MAX));
#if APP_TPMS_BLE_TOMTOM
  assert(tpms_drivers.size() == 1 && tpms_driver("tomtom-ble"));
  assert(tpms_driver("tomtom-ble")->transport == TpmsTransport::ble);
  assert(transport_name(tpms_driver("tomtom-ble")->transport) == "ble");
#else
  assert(tpms_drivers.empty() && !tpms_driver("tomtom-ble"));
#endif
  assert(!tpms_driver("uncompiled-driver"));
  std::cout << "TPMS probe and compiled capabilities passed\n";
}
