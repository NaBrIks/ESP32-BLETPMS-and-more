#include "web.hpp"
#include "modules/wifi/wifi.hpp"
#include "modules/diagnostic/diagnostic.hpp"
#include "modules/lcd/dashboard.hpp"
#include "modules/tpms.hpp"
#include <NaWServer/backends/esp_idf_tcp.h>
#include <NaWServer/backends/tcp.h>
#include <NaWServer/backends/tcp_sse.h>
#include <NaWServer/handler_middleware.h>
#include <NaWServer/http_server.h>
#include <NaWServer/route.h>
#include <NaWServer/sse.h>
#include <NaWServer/websocket.h>
#include <array>
#include <cstring>
#include <ctime>
#include <esp_heap_caps.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <mutex>

extern const char index_start[] asm("_binary_index_html_start");
extern const char ble_start[] asm("_binary_ble_html_start");
extern const char css_start[] asm("_binary_css_css_start");

namespace {
using namespace NaWServer;
using Value = lvgl_cbor::DataValue;
// Preserve the previous application's HTTP login. Persistent user settings
// are a later application stage; the library owns no product credentials.
FixedBasicAuthenticator users("1", "1");
AuthMiddleware authenticate(users);
AccessMiddleware authorize;
auto private_http = makeChain(authenticate, authorize);
constexpr auto authenticated_http = AccessPolicy::authenticated();

SendResult static_text(Response &response, const char *text, const char *type) {
  return response.sendImmutable(
      200, type, {reinterpret_cast<const uint8_t *>(text), std::strlen(text)});
}
Route index_page(
    "/index", HttpMethod::Get,
    +[](Response &r) {
      return static_text(r, index_start, "text/html; charset=utf-8");
    },
    private_http, authenticated_http);
Route index_alias(
    "/index2", HttpMethod::Get,
    +[](Response &r) {
      return static_text(r, index_start, "text/html; charset=utf-8");
    },
    private_http, authenticated_http);
Route ble_page(
    "/ble", HttpMethod::Get,
    +[](Response &r) {
      return static_text(r, ble_start, "text/html; charset=utf-8");
    },
    private_http, authenticated_http);
Route home_page(
    "/", HttpMethod::Get,
    +[](Response &r) {
      return static_text(r, ble_start, "text/html; charset=utf-8");
    },
    private_http, authenticated_http);
Route stylesheet(
    "/css.css", HttpMethod::Get,
    +[](Response &r) {
      return static_text(r, css_start, "text/css; charset=utf-8");
    },
    private_http, authenticated_http);
WebSocketEndpoint websocket("/ws", {16384, 2, 4, 32768});
tcp::IdfTransport transport;
TcpOptions options() {
  TcpOptions result;
  result.bind_address = "0.0.0.0";
  result.port = 80;
  result.websocket_tx_block_size = 16384;
  result.websocket_tx_blocks = 4;
  return result;
}
TcpHttpBackend backend(transport, options());
sse::TcpAdapter event_backend(backend);
sse::EventSource events(event_backend);
std::mutex snapshots_mutex, event_tokens_mutex;
std::string snapshot_json = "{\"msgType\":\"snapshot\",\"ready\":false}";
std::vector<uint8_t> snapshot_cbor;
Value research_snapshot, tomtom_snapshot, tpms_snapshot;
std::array<sse::Token, sse::client_capacity> event_tokens;

SendResult get_snapshot(Response &response) {
  std::string owned;
  {
    std::lock_guard lock(snapshots_mutex);
    owned = snapshot_json;
  }
  return response.send(
      200, "application/json",
      {reinterpret_cast<const uint8_t *>(owned.data()), owned.size()});
}

SendResult get_cbor(Response &response) {
  std::vector<uint8_t> owned;
  {
    std::lock_guard lock(snapshots_mutex);
    owned = snapshot_cbor;
  }
  return response.bytes(200, owned.data(), owned.size());
}

SendResult open_events(Request &request, Response &response) {
  const auto opened = events.open(request, response);
  if (opened.active) {
    std::lock_guard lock(event_tokens_mutex);
    event_tokens[opened.token.slot] = opened.token;
  } else if (opened.admission != sse::Admit::Owned)
    return response.text(503, "SSE capacity reached");
  return opened.result;
}

Route state_route("/api/state", HttpMethod::Get, get_snapshot, private_http,
                  authenticated_http);
Route cbor_route("/api/state.cbor", HttpMethod::Get, get_cbor, private_http,
                 authenticated_http);
Route event_route("/events", HttpMethod::Get, open_events, private_http,
                  authenticated_http);
SendResult json(Response &r, const Value &v, unsigned status = 200) {
  auto s = lvgl_cbor::data_value_json(v);
  return r.send(status, "application/json; charset=utf-8",
                {reinterpret_cast<const uint8_t *>(s.data()), s.size()});
}
SendResult get_research(Request &q, Response &r) {
  Value owned;
  {
    std::lock_guard lock(snapshots_mutex);
    owned = q.query("scope").value_or("") == "tom" ? tomtom_snapshot
                                                   : research_snapshot;
  }
  const auto detail = q.query("detail");
  if (detail) {
    const auto *rows =
        std::get_if<Value::Array>(&application::member(owned, "rows").value);
    if (rows)
      for (auto &x : *rows)
        if (application::string_value(application::member(x, "id")) == *detail)
          return json(r, x);
    return json(r, Value::Object{{"error", "observation_evicted"}}, 404);
  }
  auto *object = std::get_if<Value::Object>(&owned.value);
  if (object) {
    Value::Array brief;
    auto *rows = std::get_if<Value::Array>(&(*object)["rows"].value);
    if (rows)
      for (auto &x : *rows) {
        Value::Object row;
        for (auto key : {"id", "address", "address_type", "sid", "name", "rssi",
                         "packets", "last_seen_ms", "status", "classification",
                         "manufacturer_id", "manufacturer", "tpms"})
          row[key] = application::member(x, key);
        brief.push_back(std::move(row));
      }
    (*object)["rows"] = std::move(brief);
  }
  return json(r, owned);
}
SendResult post_settings(Request &q, Response &r) {
  auto b = q.body();
  std::string body(reinterpret_cast<const char *>(b.data()), b.size()), error;
  Value command;
  if (!application::parse_json(body, command, error))
    return json(r, Value::Object{{"error", error}}, 400);
  uint32_t id{};
  if (application::string_value(application::member(command, "op")) ==
      "known.bind") {
    Value proof;
    const auto requested =
        application::string_value(application::member(command, "discoveryId"));
    {
      std::lock_guard lock(snapshots_mutex);
      auto *rows = std::get_if<Value::Array>(
          &application::member(tpms_snapshot, "devices").value);
      if (rows)
        for (auto &row : *rows)
          if (application::string_value(application::member(row, "id")) ==
              requested) {
            proof = row;
            break;
          }
    }
    const auto *observed =
        std::get_if<bool>(&application::member(proof, "observed").value);
    if (!observed || !*observed)
      return json(r, Value::Object{{"error", "discovery_not_observed"}}, 422);
    application::KnownSensor sensor{
        requested,
        application::string_value(application::member(proof, "transport")),
        application::string_value(application::member(proof, "protocol")),
        uint32_t(application::integer_value(application::member(proof, "code"))),
        application::string_value(application::member(proof, "protocolId")),
        application::string_value(application::member(proof, "driver")),
        application::string_value(application::member(proof, "address")) + "/" +
            std::to_string(application::integer_value(
                application::member(proof, "addressType")))};
    id = application::submit_discovered(
        uint32_t(application::integer_value(
            application::member(command, "revision"), -1)),
        std::move(sensor));
  } else
    id = application::submit_command(std::move(body));
  return json(r, Value::Object{{"id", id}, {"queued", id != 0}},
              id ? 202 : 503);
}
SendResult get_tpms(Response &r) {
  Value owned;
  {
    std::lock_guard lock(snapshots_mutex);
    owned = tpms_snapshot;
  }
  return json(r, owned);
}
SendResult search_tpms(Request &q, Response &r) {
  auto b = q.body();
  Value v;
  std::string e;
  if (!application::parse_json(
          std::string(reinterpret_cast<const char *>(b.data()), b.size()), v,
          e))
    return json(r, Value::Object{{"error", e}}, 400);
  auto *active = std::get_if<bool>(&application::member(v, "active").value);
  if (!active)
    return json(r, Value::Object{{"error", "invalid_search_state"}}, 400);
  application::request_tpms_search(*active);
  return json(r, Value::Object{{"queued", true}}, 202);
}
Route tpms_route("/api/tpms", HttpMethod::Get, get_tpms, private_http,
                 authenticated_http);
Route search_route("/api/tpms/search", HttpMethod::Post, search_tpms,
                   private_http, authenticated_http);
SendResult get_operation(Request &q, Response &r) {
  auto id = q.query("id");
  return json(r, application::command_status(
                     id ? strtoul(std::string(*id).c_str(), nullptr, 10) : 0));
}
SendResult get_settings(Response &r) {
  return json(r, application::settings_snapshot());
}
SendResult get_files(Response &r) { return json(r, application::files_list()); }
SendResult get_file(Request &q, Response &r) {
  std::string data;
  size_t total{};
  auto name = q.query("name");
  auto offset = q.query("offset");
  size_t start =
      offset ? strtoul(std::string(*offset).c_str(), nullptr, 10) : 0;
  if (!name || !application::file_read(std::string(*name), start, data, total))
    return json(r, Value::Object{{"error", "file_unavailable"}}, 404);
  // Hex chunks preserve arbitrary binary settings/catalog documents.
  static const char hex[] = "0123456789abcdef";
  std::string bytes;
  bytes.reserve(data.size() * 2);
  for (uint8_t c : data) {
    bytes += hex[c >> 4];
    bytes += hex[c & 15];
  }
  return json(r, Value::Object{{"name", std::string(*name)},
                               {"offset", start},
                               {"total", total},
                               {"hex", bytes}});
}
SendResult post_scan(Request &q, Response &r) {
  Value v;
  std::string e;
  auto b = q.body();
  if (!application::parse_json(
          std::string(reinterpret_cast<const char *>(b.data()), b.size()), v,
          e))
    return json(r, Value::Object{{"error", e}}, 400);
  auto mode = application::string_value(application::member(v, "mode"));
  if (mode != "active" && mode != "passive")
    return json(r, Value::Object{{"error", "invalid_scan_mode"}}, 400);
  application::request_scan_mode(mode == "active");
  return json(r, Value::Object{{"queued", true}}, 202);
}
Route research_route("/api/research", HttpMethod::Get, get_research,
                     private_http, authenticated_http);
Route settings_get("/api/settings", HttpMethod::Get, get_settings, private_http,
                   authenticated_http);
Route settings_post("/api/settings", HttpMethod::Post, post_settings,
                    private_http, authenticated_http);
Route operation_route("/api/operation", HttpMethod::Get, get_operation,
                      private_http, authenticated_http);
Route files_route("/api/files", HttpMethod::Get, get_files, private_http,
                  authenticated_http);
Route file_route("/api/file", HttpMethod::Get, get_file, private_http,
                 authenticated_http);
Route scan_route("/api/scan", HttpMethod::Post, post_scan, private_http,
                 authenticated_http);
SendResult display_get(Request &q, Response &r) {
  auto offset = q.query("offset");
  if (offset)
    return json(
        r, dashboard_frame(strtoul(std::string(*offset).c_str(), nullptr, 10)));
  return json(r, dashboard_status());
}
SendResult display_post(Request &q, Response &r) {
  Value v;
  std::string e;
  auto b = q.body();
  if (!application::parse_json(
          std::string(reinterpret_cast<const char *>(b.data()), b.size()), v,
          e))
    return json(r, Value::Object{{"error", e}}, 400);
  dashboard_request(
      application::integer_value(application::member(v, "page"), -1),
      std::get_if<bool>(&application::member(v, "capture").value) &&
          std::get<bool>(application::member(v, "capture").value));
  auto control=application::string_value(application::member(v,"control"));
  if(!control.empty() && !dashboard_control(control))return json(r,Value::Object{{"error","display_control_unavailable"}},422);
  return json(r, Value::Object{{"queued", true}}, 202);
}
Route display_route("/api/display", HttpMethod::Get, display_get, private_http,
                    authenticated_http);
Route display_command("/api/display", HttpMethod::Post, display_post,
                      private_http, authenticated_http);
SendResult wifi_post(Request &q, Response &r) {
  Value v; std::string error; auto body=q.body();
  if(!application::parse_json(std::string(reinterpret_cast<const char*>(body.data()),body.size()),v,error))
    return json(r,Value::Object{{"error",error}},400);
  const auto enabled=std::get_if<bool>(&application::member(v,"enabled").value);
  const auto seconds=application::integer_value(application::member(v,"seconds"),-1);
  if(!enabled || seconds<0 || seconds>86400)
    return json(r,Value::Object{{"error","wifi_window_range"}},422);
  const bool queued=wifi_request(*enabled,uint32_t(seconds));
  return json(r,Value::Object{{"queued",queued}},queued?202:503);
}
Route wifi_command("/api/wifi",HttpMethod::Post,wifi_post,private_http,authenticated_http);
HandlerBase *const routes[]{
    &wifi_command,
    &home_page,       &index_page,   &index_alias,   &ble_page,
    &stylesheet,      &state_route,  &cbor_route,    &event_route,
    &research_route,  &settings_get, &settings_post, &operation_route,
    &files_route,     &file_route,   &scan_route,    &display_route,
    &display_command, &tpms_route,   &search_route,  &websocket};
HttpServer server(backend, routes);

void send_json(const Value &value) {
  const auto text = lvgl_cbor::data_value_json(value);
  (void)websocket.send(text.c_str());
}
} // namespace

esp_err_t start_web_server() {
  // The concrete transport has an explicit lifecycle after esp_netif init.
  // HttpServer owns HTTP dispatch, not the application's socket platform.
  if (transport.start().status != NaWServer::tcp::Status::Ok)
    return ESP_FAIL;
  websocket.onConnect([](NaWServer::WebSocketClient *client) {
    std::string owned;
    {
      std::lock_guard lock(snapshots_mutex);
      owned = snapshot_json;
    }
    (void)client->send(owned.c_str());
  });
  websocket.onMessage([](NaWServer::WebSocketClient *client,
                         const uint8_t *bytes, size_t size, bool binary) {
    // Preserve the old diagnostic echo without turning arbitrary input
    // into application configuration. Binary replies remain binary.
    if (binary)
      (void)client->send_binary(bytes, size);
    else {
      std::string owned(reinterpret_cast<const char *>(bytes), size);
      (void)client->send(owned.c_str());
    }
  });
  if (!events.start()) {
    ESP_LOGE("APP_WEB", "SSE startup failed; internal free=%u largest=%u",
             unsigned(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)),
             unsigned(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL)));
    return ESP_FAIL;
  }
  const auto started = server.begin();
  const auto memory = backend.bufferMemoryStats();
  ESP_LOGI("APP_WEB",
           "HTTP start=%u; buffer limit=%u peak=%u rejections=%u allocation "
           "failures=%u",
           unsigned(started), unsigned(memory.limit_bytes),
           unsigned(memory.peak_bytes), unsigned(memory.budget_rejections),
           unsigned(memory.allocation_failures));
  if (started == NaWServer::HttpError::Ok)
    return ESP_OK;
  (void)events.stop(1000);
  return ESP_FAIL;
}

void publish_web(const application::State &state) {
  using Value = lvgl_cbor::DataValue;
  Value::Array sensors;
  for (const auto &sensor : application::active_wheels(state))
    sensors.push_back(application::tpms_message(sensor));
  auto diagnostic = task_diagnostics();
  Value snapshot =
      Value::Object{{"msgType", "snapshot"},
                    {"ready", true},
                    {"uptimeMs", uint64_t(esp_timer_get_time() / 1000)},
                    {"ip", state.ip},
                    {"scan", state.scan},
                    {"reports", state.reports},
                    {"dropped", state.dropped},
                    {"sensorSet", state.settings.active},
                    {"settingsRevision", state.settings.revision},
                    {"storage", application::storage_status()},
                    {"wifi", state.wifi},
                    {"ble",state.ble},
                    {"bleScanTiming",state.ble},
                    {"scanMode", state.scan_mode},
                    {"unixSeconds", int64_t(time(nullptr))},
                    {"mainTick", state.main_tick},
                    {"webTick", state.web_tick},
                    {"sensors", sensors},
                    {"diagnostic", diagnostic}};
  {
    std::lock_guard lock(snapshots_mutex);
    snapshot_json = lvgl_cbor::data_value_json(snapshot);
    snapshot_cbor = lvgl_cbor::encode_data_value(snapshot);
    research_snapshot = state.research;
    tomtom_snapshot = state.tomtom;
    tpms_snapshot = application::tpms_catalog(state);
  }
  if (websocket.count()) {
    // One complete frame prevents a diagnostic/sensor burst from overflowing
    // the bounded transport queue. Existing message fields remain nested.
    if (lvgl_cbor::data_value_json(snapshot).size() < 16000)
      send_json(snapshot);
    else
      send_json(Value::Object{{"msgType", "refresh"}});
  }
  const auto tick = lvgl_cbor::data_value_json(
      Value::Object{{"msgType", "webTick"}, {"id", state.web_tick}});
  NaWServer::sse::Frame frame;
  if (NaWServer::sse::encode({tick, {}, {}, {}}, frame) ==
      NaWServer::sse::EncodeResult::Ok) {
    std::lock_guard lock(event_tokens_mutex);
    for (auto &token : event_tokens) {
      if (token.slot >= NaWServer::sse::client_capacity)
        continue;
      const auto result = events.publish(token, frame);
      if (result != NaWServer::sse::Publish::Queued &&
          result != NaWServer::sse::Publish::Busy) {
        (void)events.close(token);
        token = {};
      }
    }
  }
}
