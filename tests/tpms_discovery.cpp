#include "../src/modules/tpms.hpp"
#include <algorithm>
#include <cassert>
#include <iostream>

using namespace application;
using namespace NaBTools::runtime;
int64_t clock_us = 1000000;
int64_t esp_timer_get_time() { return clock_us; }

Advertisement packet(uint8_t suffix, uint32_t pa, uint64_t ms) {
  Advertisement p;
  p.address.bytes = {0x02, 0, 0, 0, 0, suffix};
  p.time_ms = ms;
  uint8_t bytes[]{2,1,5,19,0xff,0,1,0x80,0xea,0xca,0xaa,0xbb,suffix,
                  0,0,0,0,0,0,0,0,91,1};
  p.size = sizeof(bytes);
  std::copy(std::begin(bytes), std::end(bytes), p.data.begin());
  for (unsigned i=0; i<4; ++i) p.data[13+i] = uint8_t(pa >> (8*i));
  return p;
}
void deliver(State& s, Advertisement p) {
  auto parsed = TomTomPacket::parse(p.view());
  assert(parsed);
  observe_tomtom(s, p.view(), *parsed);
}
int main() {
  State s;
  deliver(s, packet(1, 230000, 900));
#if APP_TPMS_BLE_TOMTOM
  assert(s.tpms_observations.size() == 1);
  const auto stable = s.tpms_observations[0].discovery_id;
  auto adv = packet(1, 234000, 1001);
  adv.sid = 2;
  request_tpms_search(true);
  poll_tpms_search(s);
  deliver(s, adv);
  assert(s.tpms_observations.size()==1 && s.tpms_observations[0].discovery_id==stable);
  assert(!s.tpms_observations[0].probe.changed);
  deliver(s, packet(1, 240000, 1002));
  assert(s.tpms_observations[0].probe.changed);
  assert(s.tpms_observations[0].probe.baseline_pa==230000);
  s.settings.known.push_back({"legacy-system-id","ble","tomtom",0xAABB01,
                              "AABB01","tomtom-ble",""});
  const auto catalog=tpms_catalog(s);
  const auto& obj=std::get<Value::Object>(catalog.value);
  const auto& devices=std::get<Value::Array>(obj.at("devices").value);
  assert(devices.size()==1);
  const auto& known=std::get<Value::Object>(devices.front().value);
  assert(std::get<std::string>(known.at("id").value)=="legacy-system-id");
  assert(std::get<bool>(known.at("observed").value));
  assert(std::get<bool>(known.at("changed").value));
  for (unsigned i=2; i<=100; ++i) deliver(s, packet(uint8_t(i),230000,2000+i));
  assert(s.tpms_observations.size()==96);
  assert(std::any_of(s.tpms_observations.begin(),s.tpms_observations.end(),
                    [&](auto& o){return o.discovery_id==stable;}));
  request_tpms_search(false);poll_tpms_search(s);
  assert(!s.tpms_search);
  assert(std::none_of(s.tpms_observations.begin(),s.tpms_observations.end(),
                     [](auto& o){return o.probe.changed;}));
  clock_us=4000000;
  request_tpms_search(true);poll_tpms_search(s);
  deliver(s,packet(1,0,3999)); // received before the new search began
  assert(!s.tpms_observations[0].probe.changed);
#else
  assert(s.tpms_observations.empty());
  const auto catalog=tpms_catalog(s);
  assert(std::get<Value::Array>(std::get<Value::Object>(catalog.value).at("drivers").value).empty());
#endif
  std::cout << "Parsed discovery, stable identity, bounded retention and search passed\n";
}
