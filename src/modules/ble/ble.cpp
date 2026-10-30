#include "ble.hpp"
#include "modules/tpms.hpp"
#include "modules/wifi/wifi.hpp"
#include <NaBTools/devices/samsung/smarttag/smarttag.hpp>
#include <NaBTools/profiles/heart_rate/hrm.hpp>
#include <NaBTools/research/research.hpp>
#include <NaBTools_espidf.hpp>
#include <algorithm>
#include <atomic>
#include <cbor.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <memory>
#include <ctime>

namespace application {
using namespace NaBTools::runtime;
static std::unique_ptr<BtEngine> engine;
static ResearchModule *research;
static std::atomic<int> wanted_mode{-1};
static EspIdfBackend *radio_backend{};
static uint32_t configured_interval{},configured_window{};
static uint64_t next_snapshot{}, next_counters{};
static std::string hex(Bytes bytes) {
  static const char digits[] = "0123456789ABCDEF";
  std::string s;
  s.reserve(bytes.size() * 2);
  for (auto b : bytes) {
    s += digits[b >> 4];
    s += digits[b & 15];
  }
  return s;
}
// Adapter of the library's complete CBOR observation. Byte strings are hex text
// for DataRegistry/JSON; no advertising parser is reimplemented here.
static bool value_from_cbor(CborValue &it, Value &out, unsigned depth = 0) {
  if (depth > 12)
    return false;
  if (cbor_value_is_text_string(&it) || cbor_value_is_byte_string(&it)) {
    size_t n{};
    if (cbor_value_calculate_string_length(&it, &n) != CborNoError || n > 4096)
      return false;
    if (cbor_value_is_text_string(&it)) {
      std::string s(n + 1, '\0');
      size_t len = n + 1;
      if (cbor_value_copy_text_string(&it, s.data(), &len, nullptr) !=
          CborNoError)
        return false;
      s.resize(n);
      out = s;
    } else {
      std::vector<uint8_t> b(n);
      size_t len = n;
      if (cbor_value_copy_byte_string(&it, b.data(), &len, nullptr) !=
          CborNoError)
        return false;
      out = hex(b);
    }
  } else if (cbor_value_is_integer(&it)) {
    int64_t n{};
    if (cbor_value_get_int64_checked(&it, &n) != CborNoError)
      return false;
    out = n;
  } else if (cbor_value_is_boolean(&it)) {
    bool b{};
    cbor_value_get_boolean(&it, &b);
    out = b;
  } else if (cbor_value_is_null(&it))
    out = {};
  else if (cbor_value_is_array(&it) || cbor_value_is_map(&it)) {
    CborValue child;
    if (cbor_value_enter_container(&it, &child) != CborNoError)
      return false;
    if (cbor_value_is_array(&it)) {
      Value::Array a;
      while (!cbor_value_at_end(&child)) {
        Value v;
        if (a.size() > 256 || !value_from_cbor(child, v, depth + 1))
          return false;
        a.push_back(std::move(v));
      }
      out = std::move(a);
    } else {
      Value::Object o;
      while (!cbor_value_at_end(&child)) {
        Value k, v;
        if (o.size() > 64 || !value_from_cbor(child, k, depth + 1) ||
            !value_from_cbor(child, v, depth + 1))
          return false;
        o[string_value(k)] = std::move(v);
      }
      out = std::move(o);
    }
    return cbor_value_leave_container(&it, &child) == CborNoError;
  } else
    return false;
  return cbor_value_advance(&it) == CborNoError;
}
static Value observation_value(const ObservationRow &r) {
  static std::array<uint8_t, 8192> buffer;
  CborWriter w(buffer);
  ResearchModule::write_observation(*engine, w, r);
  auto bytes = w.result();
  Value result;
  CborParser parser;
  CborValue it;
  if (!w.good() || bytes.empty() ||
      cbor_parser_init(bytes.data(), bytes.size(), 0, &parser, &it) !=
          CborNoError ||
      !value_from_cbor(it, result))
    return Value::Object{{"error", "observation_encoding_limit"}};
  auto &o = std::get<Value::Object>(result.value);
  o["id"] = r.packet.address.text() + "/" +
            std::to_string(r.packet.address.type) + "/" +
            std::to_string(r.packet.sid);
  o["legacy"] = r.packet.legacy;
  o["complete"] = r.packet.complete;
  if (auto p = TomTomPacket::parse(r.packet.view())) {
    const uint32_t code = printed_sensor_code(p->sensor_address());
    char id[16];
    snprintf(id, sizeof(id), "%06lX", (unsigned long)code);
    o["tpms"] = Value::Object{{"code", code},
                              {"sticker", id},
                              {"rawAddress", p->sensor_address()},
                              {"sensorNumber", p->sensor_number()},
                              {"pressurePa", p->pressure_pa()},
                              {"temperatureCentiC", p->temperature_centi_c()},
                              {"battery", p->battery()},
                              {"alarm", p->alarm()},
                              {"manufacturerRaw", hex(p->raw())}};
  }
  return result;
}
class TomTomResearch final : public IModule {
  State &state_;

public:
  explicit TomTomResearch(State &s) : state_(s) {}
  ResearchModule collector;
  uint64_t candidates{}, parsed{}, rejected{};
  std::array<uint64_t, ResearchModule::capacity> logged_at{};
  std::array<std::string, ResearchModule::capacity> logged_id{};
  const char *key() const override { return "application.tomtom-research"; }
  void observe(BtEngine &e, AdvertisementView v, AdStatus status) override {
    const auto m = field(v.payload, 0xff);
    const bool manufacturer =
        m && m->size() >= 2 && (*m)[0] == 0 && (*m)[1] == 1;
    const bool known = collector.find(v.address, v.sid) != nullptr;
    const bool address_hint=v.address.bytes[1]==0xea && v.address.bytes[2]==0xca;
    if (!manufacturer && !known && !address_hint && !TomTomPacket::parse(v))
      return;
    ++candidates;
    if(auto packet=TomTomPacket::parse(v)){
      ++parsed;
      auto code=printed_sensor_code(packet->sensor_address());
      for(auto &sensor:state_.sensors)if(sensor.code==code){++sensor.advertisements;sensor.last_rssi=v.rssi;}
    }else ++rejected;
    const auto *previous = collector.find(v.address, v.sid);
    const bool first = previous == nullptr;
    collector.observe(e, v, status);
#if APP_TPMS_BLE_TOMTOM
    if (auto parsed = TomTomPacket::parse(v))
      observe_tomtom(state_, v, *parsed);
#endif
    auto *row = collector.find(v.address, v.sid);
    if (!row)
      return;
    size_t slot = size_t(row - collector.rows().data());
    auto key = v.address.text() + "/" + std::to_string(v.address.type) + "/" +
               std::to_string(v.sid);
    if (first || logged_id[slot] != key ||
        v.time_ms - logged_at[slot] >= 1000) {
      logged_at[slot] = v.time_ms;
      logged_id[slot] = key;
      const auto packet = TomTomPacket::parse(v);
      Value::Object log{
          {"event", "tomtom-advertisement"},
          {"timeMs", v.time_ms},
          {"address", v.address.text()},
          {"addressType", v.address.type},
          {"sid", v.sid},
          {"rssi", v.rssi},
          {"legacy", v.legacy},
          {"complete", v.complete},
          {"status", name(status)},
          {"raw", hex(v.payload)},
          {"parse", packet ? "accepted" : "signature_or_length_rejected"}};
      if (packet) {
        auto code = printed_sensor_code(packet->sensor_address());
        log["code"] = code;
        log["rawCode"] = packet->sensor_address();
        log["registered"] =
            std::any_of(state_.sensors.begin(), state_.sensors.end(),
                        [&](auto &s) { return s.code == code; });
      }
      if (v.radio)
        log["radio"] =
            Value::Object{{"properties", v.radio->event_properties},
                          {"primaryPhy", v.radio->primary_phy},
                          {"secondaryPhy", v.radio->secondary_phy},
                          {"dataStatus", v.radio->data_status},
                          {"scanResponse", v.radio->scan_response()}};
      journal(lvgl_cbor::data_value_json(log));
    }
  }
};
static TomTomResearch *tomtom_research;
#if APP_TPMS_BLE_TOMTOM
static Delivery receive_tpms(void *context, const MessageInfo &info,
                             const TpmsReading &reading) {
  auto &state = *static_cast<State *>(context);
  const auto code = printed_sensor_code(reading.sensor_address);
  auto found = std::find_if(state.sensors.begin(), state.sensors.end(),
                            [&](auto &s) { return s.code == code; });
  if (found == state.sensors.end())
    return Delivery::dropped;
  const bool log = !found->present || info.time_ms - found->journal_ms >= 1000;
  ++found->readings;
  found->present = true;
  found->reading = reading;
  found->last_seen_ms = info.time_ms;
  if (log) {
    found->journal_ms = info.time_ms;
    journal(lvgl_cbor::data_value_json(
        Value::Object{{"event", "reading-delivered"},
                      {"timeMs", info.time_ms},
                      {"code", code},
                      {"pressurePa", reading.pressure_pa},
                      {"temperatureCentiC", reading.temperature_centi_c}}));
  }
  return Delivery::accepted;
}
class ConfiguredTpmsModule final : public IModule {
  State &state_;
  TpmsActions actions_;

public:
  ConfiguredTpmsModule(State &s, TpmsActions a) : state_(s), actions_(a) {}
  const char *key() const override { return "application.configured-tpms"; }
  std::optional<Detection> recognize(AdvertisementView v) const override {
    if (TomTomPacket::parse(v))
      return Detection{"TomTom TPMS", "manufacturer signature", true,
                       DeviceKind::tire_pressure_sensor};
    return {};
  }
  void observe(BtEngine &e, AdvertisementView v, AdStatus validity) override {
    if (validity != AdStatus::complete)
      return;
    auto packet = TomTomPacket::parse(v);
    if (!packet)
      return;
    auto code = printed_sensor_code(packet->sensor_address());
    if (std::none_of(state_.sensors.begin(), state_.sensors.end(),
                     [&](auto &s) { return s.code == code; }))
      return;
    if (!e.find(v.address, "TomTom TPMS").valid()) {
      auto id =
          e.add_device(std::make_unique<TomTomDevice>(v.address, actions_));
      journal(
          lvgl_cbor::data_value_json(Value::Object{{"event", "admission"},
                                                   {"timeMs", v.time_ms},
                                                   {"code", code},
                                                   {"accepted", id.valid()}}));
    }
  }
};
#endif
void request_scan_mode(bool active) { wanted_mode.store(active ? 1 : 0); }
void sync_settings(State &state) {
  if (!settings_copy(state.settings))
    return;
  std::vector<Sensor> sensors;
  for (auto &k : state.settings.known) {
    if (k.transport != "ble" || k.protocol != "tomtom")
      continue;
    auto previous =
        std::find_if(state.sensors.begin(), state.sensors.end(),
                     [&](auto &s) { return s.id == k.id && s.code == k.code; });
    Sensor s;
    if (previous != state.sensors.end())
      s = *previous;
    s.id = k.id;
    s.code = k.code;
    s.identity = k.identity;
    s.transport = k.transport;
    s.protocol = k.protocol;
    s.position = k.id;
    sensors.push_back(std::move(s));
  }
  state.sensors = std::move(sensors);
}
std::vector<Sensor> active_wheels(const State &state) {
  std::vector<Sensor> wheels;
  for (auto &c : state.settings.garage)
    if (c.id == state.settings.active)
      for (auto &a : c.axles)
        for (auto &w : a.wheels) {
          Sensor s;
          auto found = std::find_if(state.sensors.begin(), state.sensors.end(),
                                    [&](auto &x) { return x.id == w.sensor; });
          if (found != state.sensors.end())
            s = *found;
          else
            for (auto &known : state.settings.known)
              if (known.id == w.sensor) {
                s.code = known.code;
                s.identity = known.identity;
                s.transport = known.transport;
                s.protocol = known.protocol;
                break;
              }
          s.id = w.sensor;
          s.position = a.name + " / " + w.name;
          wheels.push_back(std::move(s));
        }
  return wheels;
}
void start_bluetooth(State &state) {
  // Request a continuous 80 ms scan window. Wi-Fi coexistence still owns RF arbitration.
  auto backend=std::make_unique<EspIdfBackend>(EspIdfScanTiming{
      uint16_t(state.settings.ble_interval_ms*8/5),uint16_t(state.settings.ble_window_ms*8/5)});
  radio_backend=backend.get();
  engine = std::make_unique<BtEngine>(std::move(backend));
  journal(lvgl_cbor::data_value_json(Value::Object{{"event","ble-scan-configuration"},
      {"timeMs",esp_timer_get_time()/1000},{"intervalMs",state.settings.ble_interval_ms},{"windowMs",state.settings.ble_window_ms},
      {"duplicates",false},{"wifi",wifi_status()}}));
  // Recognizers enrich research only; their observe() never auto-admits
  // devices.
  engine->add_module(std::make_unique<SmartTagModule>());
  engine->add_module(std::make_unique<HeartRateModule>());
  auto all = std::make_unique<ResearchModule>();
  research = all.get();
  engine->add_module(std::move(all));
  auto filtered = std::make_unique<TomTomResearch>(state);
  tomtom_research = filtered.get();
  engine->add_module(std::move(filtered));
#if APP_TPMS_BLE_TOMTOM
  TpmsActions actions;
  actions.reading = {&state, receive_tpms};
  auto result = engine->add_module(
      std::make_unique<ConfiguredTpmsModule>(state, actions));
#else
  auto result = Status::ok;
#endif
  if (result == Status::ok)
    result = engine->start();
  ESP_LOGI("APP_BT", "NaBTools start: %s", name(result));
  state.scan = name(result);
}
void poll_bluetooth(State &state) {
  if (!engine)
    return;
  if(configured_interval!=state.settings.ble_interval_ms || configured_window!=state.settings.ble_window_ms){
    const auto result=radio_backend->set_scan_timing({uint16_t(state.settings.ble_interval_ms*8/5),uint16_t(state.settings.ble_window_ms*8/5)});
    configured_interval=state.settings.ble_interval_ms;configured_window=state.settings.ble_window_ms;
    journal(lvgl_cbor::data_value_json(Value::Object{{"event","ble-scan-timing-requested"},
      {"timeMs",esp_timer_get_time()/1000},{"intervalMs",configured_interval},{"windowMs",configured_window},
      {"result",name(result)},{"wifi",wifi_status()}}));
  }
  int mode = wanted_mode.exchange(-1);
  if (mode >= 0)
    (void)engine->set_scan_mode(mode ? ScanMode::active : ScanMode::passive);
  poll_tpms_search(state);
  engine->poll();
  auto backend = engine->backend();
  const auto actual_timing=radio_backend->scan_timing();
  static uint16_t logged_interval{},logged_window{};
  if(actual_timing.valid() && (logged_interval!=actual_timing.interval || logged_window!=actual_timing.window)){
    logged_interval=actual_timing.interval;logged_window=actual_timing.window;
    journal(lvgl_cbor::data_value_json(Value::Object{{"event","ble-scan-timing-applied"},
      {"timeMs",esp_timer_get_time()/1000},{"intervalMs",actual_timing.interval*5/8},
      {"windowMs",actual_timing.window*5/8},{"wifi",wifi_status()}}));
  }
  state.reports = backend.reports;
  state.dropped = backend.dropped;
  state.scan = name(backend.state);
  state.scan_mode = name(backend.scan_mode);
  uint64_t now = esp_timer_get_time() / 1000;
  if (now >= next_snapshot) {
    const auto timing=radio_backend->scan_timing();
    state.ble=Value::Object{{"enabled",true},{"state",state.scan},
      {"intervalMs",timing.interval*5/8},{"windowMs",timing.window*5/8},
      {"requestedIntervalMs",configured_interval},{"requestedWindowMs",configured_window},
      {"pending",timing.interval*5/8!=configured_interval || timing.window*5/8!=configured_window},
      {"continuous",timing.valid() && timing.interval==timing.window}};
    auto snapshot = [&](const ResearchModule &m) {
      Value::Array rows;
      for (auto &r : m.rows())
        rows.push_back(observation_value(r));
      auto counters = engine->counters();
      return Value(Value::Object{
          {"rows", std::move(rows)},
          {"evicted", m.evicted()},
          {"observed", counters.observed},
          {"malformed", counters.malformed},
          {"fragments", counters.fragments},
          {"scanMode", name(backend.scan_mode)},
          {"requestedScanMode", name(backend.requested_scan_mode)},
          {"scanModePending", backend.scan_mode_pending}});
    };
    state.research = snapshot(*research);
    state.tomtom = snapshot(tomtom_research->collector);
    next_snapshot = now + 1000;
  }
  if (now >= next_counters) {
    auto c = engine->counters();
    Value::Array sensors;
    static size_t journal_sensor_cursor{};
    const auto count=std::min<size_t>(8,state.sensors.size());
    if(!state.sensors.empty())journal_sensor_cursor%=state.sensors.size();
    const auto sensor_offset=journal_sensor_cursor;
    for(size_t i=0;i<count;++i){const auto &sensor=state.sensors[(sensor_offset+i)%state.sensors.size()];sensors.push_back(Value::Object{
      {"code",sensor.code},{"advertisements",sensor.advertisements},{"readings",sensor.readings},
      {"lastSeenMs",sensor.last_seen_ms},{"rssi",sensor.last_rssi}});}
    journal_sensor_cursor+=count;
    journal(lvgl_cbor::data_value_json(
        Value::Object{{"event", "counters"},
                      {"timeMs", now},
                      {"unixSeconds",int64_t(time(nullptr))},
                      {"reports", backend.reports},
                      {"dropped", backend.dropped},
                      {"observed", c.observed},
                      {"malformed", c.malformed},
                      {"fragments", c.fragments},
                      {"scan", name(backend.state)},
                      {"scanMode", name(backend.scan_mode)},
                      {"backendError",backend.error},
                      {"wifi",wifi_status()},
                      {"scanIntervalMs",actual_timing.interval*5/8},{"scanWindowMs",actual_timing.window*5/8},
                      {"tomtomCandidates",tomtom_research->candidates},
                      {"tomtomParsed",tomtom_research->parsed},
                      {"tomtomRejected",tomtom_research->rejected},
                      {"sensors",sensors},
                      {"sensorOffset",sensor_offset},{"sensorCount",state.sensors.size()},
                      {"storage",storage_status()},
                      {"evicted", research->evicted()}}));
    next_counters = now + 10000;
  }
}
lvgl_cbor::DataValue tpms_message(const Sensor &sensor) {
  auto &r = sensor.reading;
  Value::Object o{{"msgType", "tpms"},
                  {"msgSource", sensor.transport},
                  {"identity", sensor.identity},
                  {"protocol", sensor.protocol},
                  {"id", sensor.code},
                  {"knownId", sensor.id},
                  {"present", sensor.present},
                  {"position", sensor.position},
                  {"sensorAddress", sensor.code},
                  {"lastSeenMs", sensor.last_seen_ms}};
  o["advertisements"]=sensor.advertisements;
  o["readings"]=sensor.readings;
  o["rssi"]=sensor.last_rssi;
  if (sensor.present) {
    o["sensorAddressRaw"] = r.sensor_address;
    o["sensorNumber"] = r.sensor_number;
    o["pressureRaw"] = r.pressure_pa;
    o["temperatureRaw"] = r.temperature_centi_c;
    o["batteryPercentage"] = r.battery_percentage;
    o["alarmFlag"] = r.alarm;
    o["pressure_Bar"] = r.pressure().bars();
    o["temperatureC"] = r.temperature().celsius;
  }
  return o;
}
} // namespace application
