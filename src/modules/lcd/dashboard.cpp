// SPDX-FileCopyrightText: 2026 Oleg Kozhevnikov
// SPDX-License-Identifier: MIT
#include "dashboard.hpp"
#include "modules/wifi/wifi.hpp"
#include "appearance.hpp"
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <ctime>
#include <esp_log.h>
#include <esp_timer.h>
#include <lvgl_cbor/project_service.hpp>
#include <memory>
#include <mutex>
#if APP_DISPLAY_WIDTH == 800
#include "boards/lcd7/board.hpp"
#endif

extern const uint8_t project_start[] asm ("_binary_project_cbor_start");
extern const uint8_t project_end[] asm ("_binary_project_cbor_end");
extern const uint8_t appearance_start[] asm ("_binary_appearance_cbor_start");
extern const uint8_t appearance_end[] asm ("_binary_appearance_cbor_end");
extern "C" {
LV_FONT_DECLARE(app_text14);
LV_FONT_DECLARE(app_text20);
}
namespace
{
	using namespace lvgl_cbor;
	using application::integer_value;
	using application::member;
	using application::string_value;
	DataRegistry data;
	std::unique_ptr<Appearance> appearance;
	std::vector<Resource> resources = {
		{"montserrat_14", "const lv_font_t *", &app_text14},
		{"montserrat_20", "const lv_font_t *", &app_text20},
		{"montserrat_28", "const lv_font_t *", &lv_font_montserrat_28}};
	std::unique_ptr<ConstProjectSource> source;
	std::unique_ptr<ProjectService> project;
	int wheel_page{}, tom_page{}, bt_page{}, tom_detail_page{}, bt_detail_page{};
	std::string tom_selection, bt_selection;
	std::atomic<int> requested_page{-1};
	std::atomic<bool> capture_requested{};
	std::mutex debug_mutex;
	std::mutex control_mutex;
	std::string requested_control;
	DataValue ui_status;
	std::vector<uint8_t> frame;
	const DataValue::Array &rows(const DataValue &v)
	{
		static const DataValue::Array empty;
		auto p = std::get_if<DataValue::Array>(&member(v, "rows").value);
		return p ? *p : empty;
	}

	void lines(const DataValue &v, const std::string &prefix, std::string &out)
	{
		if(auto p = std::get_if<DataValue::Object>(&v.value))
		{
			for(auto &[k, x] : *p)
				lines(x, prefix.empty() ? k : prefix + "." + k, out);
		}
		else if(auto p = std::get_if<DataValue::Array>(&v.value))
		{
			for(size_t i = 0; i < p->size(); ++i)
				lines((*p)[i], prefix + "[" + std::to_string(i) + "]", out);
		}
		else
			out += prefix + ": " + data_value_json(v) + "\n";
	}

	void publish_research(const char *id, const DataValue &v, int &page,
	    std::string &selection, uint64_t now)
	{
#if APP_DISPLAY_WIDTH == 800
		constexpr size_t count = 6;
#else
		constexpr size_t count = 2;
#endif
		auto &r = rows(v);
		int pages = std::max(1, int((r.size() + count - 1) / count));
		page = (page % pages + pages) % pages;
		DataValue::Array visible;
		for(size_t n = 0; n < count; ++n)
		{
			size_t i = size_t(page) * count + n;
			std::string key, text = "--";
			if(i < r.size())
			{
				const auto &x = r[i];
				key = string_value(member(x, "id"));
				text = string_value(member(x, "address")) + " / " +
				    std::to_string(integer_value(member(x, "address_type"))) + " / " +
				    std::to_string(integer_value(member(x, "sid"))) + "   " +
				    std::to_string(integer_value(member(x, "rssi"))) + " dBm   " +
				    std::to_string(integer_value(member(x, "packets"))) + "   " +
				    std::to_string(
					(now - uint64_t(integer_value(member(x, "last_seen_ms")))) /
					1000) +
				    " s   " +
				    string_value(member(member(x, "classification"), "type"));
			}

			DataValue::Object row{{"id", key}, {"line", text}, {"address", "--"},
				{"rssi", "--"}, {"packets", "--"}, {"age", "--"},
				{"kind", "--"}};
			if(i < r.size())
			{
				auto &x = r[i];
				row["address"] =
				    string_value(member(x, "address")) + " / " +
				    std::to_string(integer_value(member(x, "address_type"))) + " / " +
				    std::to_string(integer_value(member(x, "sid")));
				row["rssi"] = std::to_string(integer_value(member(x, "rssi"))) + " dBm";
				row["packets"] = std::to_string(integer_value(member(x, "packets")));
				row["age"] =
				    std::to_string(
					(now - uint64_t(integer_value(member(x, "last_seen_ms")))) /
					1000) +
				    " s";
				row["kind"] = string_value(member(member(x, "classification"), "type"));
			}

			visible.push_back(std::move(row));
		}
		std::string detail = "Select an advertisement";
		if(!selection.empty())
		{
			detail = "Selected report was evicted";
			for(auto &x : r)
				if(string_value(member(x, "id")) == selection)
				{
					detail.clear();
					lines(x, "", detail);
					break;
				}
		}

		// Keep every field reachable within the generic 4 KiB text-source limit.
		std::vector<std::string> chunks;
		for(size_t start = 0; start < detail.size();)
		{
			size_t end = std::min(detail.size(), start + 3600);
			while(end < detail.size() && (uint8_t(detail[end]) & 0xc0) == 0x80)
				--end;
			chunks.push_back(detail.substr(start, end - start));
			start = end;
		}
		if(chunks.empty())
			chunks.push_back("");

		auto &detail_page =
		    std::string(id) == "tom" ? tom_detail_page : bt_detail_page;
		detail_page = (detail_page % int(chunks.size()) + int(chunks.size())) %
		    int(chunks.size());
		detail = "Details " + std::to_string(detail_page + 1) + "/" +
		    std::to_string(chunks.size()) + "\n" + chunks[size_t(detail_page)];
		std::string error;
		(void)data.publish(std::string(id) + ".rows", visible, error);
		(void)data.publish(std::string(id) + ".detail", detail, error);
		(void)data.publish(std::string(id) + ".status",
		    std::to_string(page + 1) + "/" + std::to_string(pages) +
		    "  " + std::to_string(r.size()) + " seen  " +
		    string_value(member(v, "scanMode")),
		    error);
	}

} // namespace
void dashboard_init()
{
	std::string e;
	try{
		appearance = std::make_unique<Appearance>();
		auto styles = appearance->resources(); resources.insert(resources.end(), styles.begin(), styles.end());
		appearance->load({appearance_start, size_t(appearance_end - appearance_start), {}}, "cream", resources.data(),
		    resources.size());
		appearance->install_popup_theme(lv_display_get_default(), &app_text14, 2);
	}catch(const std::exception &error){ESP_LOGE("APP_UI", "Appearance: %s", error.what()); appearance.reset(); return;}
	data.add_source({"ui.scheme", DataSchema::object({{"id", DataSchema::text(12)}, {"title", DataSchema::text(64)}})},
	    e);
	data.add_action({"ui.select_scheme", DataSchema::text(12), [](const DataValue &v, std::string &){
						 auto settings = application::settings_snapshot();
						 return application::submit_command(data_value_json(DataValue::Object{{"op", "appearance.put"},
									{"revision", member(settings, "revision")}, {"theme", v}})) != 0;
					 }}, e);
	auto tpms =
	    DataSchema::object({{"address", DataSchema::text(16)},
				{"bar", DataSchema::number(0, 100)},
				{"temperature", DataSchema::number(-100, 200)},
				{"age", DataSchema::integer(0)}});
	for(unsigned i = 1; i <= 4; ++i)
	{
		data.add_source({"tpms." + std::to_string(i), tpms}, e);
		data.add_source({"wheel." + std::to_string(i), DataSchema::text(256)}, e);
	}
	data.add_source(
		{"receiver", DataSchema::object({{"network", DataSchema::text(48)},
			 {"bluetooth", DataSchema::text(64)},
			 {"reports", DataSchema::text(64)},
			 {"vehicle", DataSchema::text(128)},
			 {"page", DataSchema::text(64)},
			 {"clock", DataSchema::text(8)}})},
		e);
	data.add_source({"wifi", DataSchema::text(80)}, e);
	data.add_source({"ble.control", DataSchema::text(80)}, e);
	data.add_source({"ble.interval", DataSchema::text(16)}, e);
	data.add_source({"ble.window", DataSchema::text(16)}, e);
	for(const auto key:{"ble.interval", "ble.window"})
		data.add_action({key, DataSchema::integer(-5, 5), [key](const DataValue &v, std::string &){
							 const auto settings = application::settings_snapshot();
							 auto interval = integer_value(member(settings, "bleIntervalMs"), 80);
							 auto window = integer_value(member(settings, "bleWindowMs"), 80);
							 const auto delta = integer_value(v);
							 if(std::string(key) == "ble.interval")
							 {
								 interval = std::clamp<int64_t>(interval + delta, 5, 10000);
								 window = std::min(window, interval);
							 }
							 else window = std::clamp<int64_t>(window + delta, 5, interval);

							 if(interval == integer_value(member(settings,
							 "bleIntervalMs")) && window == integer_value(member(settings, "bleWindowMs"))) return true;

							 return application::submit_command(data_value_json(DataValue::Object{{"op", "radio.put"},
										{"revision", member(settings, "revision")},
										{"wifiEnabled", member(settings, "wifiEnabled")},
										{"wifiWindowSeconds", member(settings, "wifiWindowSeconds")},
										{"bleIntervalMs", interval}, {"bleWindowMs", window}})) != 0;
						 }}, e);
	data.add_action({"wifi.open", DataSchema::integer(0, 86400), [](const DataValue &v, std::string &){
						 return wifi_request(true, uint32_t(integer_value(v)));
					 }}, e);
	data.add_action({"wifi.close", DataSchema::integer(0, 0), [](const DataValue &, std::string &){
						 return wifi_request(false, 0);
					 }}, e);
	data.add_action({"wheels.page", DataSchema::integer(-1, 1),
	                 [](const DataValue &v, std::string &) {
						 wheel_page += integer_value(v);
						 return true;
					 }},
	    e);
	for(auto id : {"tom", "bt"})
	{
		data.add_source({std::string(id) + ".rows",
		                 DataSchema::array(
							 DataSchema::object({{"id", DataSchema::text(80)},
								 {"line", DataSchema::text(256)},
								 {"address", DataSchema::text(80)},
								 {"rssi", DataSchema::text(32)},
								 {"packets", DataSchema::text(32)},
								 {"age", DataSchema::text(32)},
								 {"kind", DataSchema::text(64)}}),
							 6)},
		    e);
		data.add_source({std::string(id) + ".detail", DataSchema::text(4096)}, e);
		data.add_source({std::string(id) + ".status", DataSchema::text(96)}, e);
		data.add_action(
			{std::string(id) + ".select", DataSchema::text(80),
			 [id](const DataValue &v, std::string &) {
				 (std::string(id) == "tom" ? tom_selection : bt_selection) =
				 string_value(v);
				 (std::string(id) == "tom" ? tom_detail_page : bt_detail_page) = 0;
				 return true;
			 }},
			e);
		data.add_action(
			{std::string(id) + ".detailpage", DataSchema::integer(-1, 1),
			 [id](const DataValue &v, std::string &) {
				 (std::string(id) == "tom" ? tom_detail_page : bt_detail_page) +=
				 integer_value(v);
				 return true;
			 }},
			e);
		data.add_action({std::string(id) + ".page", DataSchema::integer(-1, 1),
		                 [id](const DataValue &v, std::string &) {
							 (std::string(id) == "tom" ? tom_page : bt_page) +=
							 integer_value(v);
							 return true;
						 }},
		    e);
	}
	source = std::make_unique<ConstProjectSource>(project_start,
	        project_end - project_start);
	project = std::make_unique<ProjectService>(
		*source, nullptr, ProjectPolicy{false, false, false}, ProjectLimits{},
		resources.data(), resources.size(), nullptr,
		std::vector<ProjectStorageInfo>{}, &data);
	if(!project->begin(e))
	{
		ESP_LOGE("APP_UI", "Project: %s", e.c_str());
		return;
	}

	ScreenReply reply;
#if APP_DISPLAY_WIDTH == 800
	const char *screen = "shell";
#else
	const char *screen = "shell-small";
#endif
	if(!project->load_screen(screen, reply))
		ESP_LOGE("APP_UI", "Screen: %s", reply.error);
	else
		ESP_LOGI("APP_UI", "CBOR shell loaded: %s, %u nodes", screen, reply.nodes);
}

void dashboard_update(const application::State &state)
{
	std::string e;
	if(appearance)
	{
		appearance->select(state.settings.native_theme);
		for(const auto &choice:appearance->choices())
			if(choice.id == appearance->selected())
				data.publish("ui.scheme", DataValue::Object{{"id", choice.id}, {"title", choice.title}}, e);
	}

	auto wireless = wifi_status();
	const bool enabled = std::get<bool>(member(wireless, "enabled").value);
	const bool online = std::get<bool>(member(wireless, "connected").value);
	const auto remaining = integer_value(member(wireless, "remainingSeconds"));
	data.publish("wifi", std::string("Wi-Fi ") + (enabled ? (online ? "online" : "searching") : "off") +
	    (enabled && remaining >= 0 ? " / " + std::to_string(remaining) + " s" : ""), e);
	data.publish("ble.control", std::string("BLE: ") + state.scan + " / " +
	    std::to_string(integer_value(member(state.ble, "windowMs"))) + " / " +
	    std::to_string(integer_value(member(state.ble, "intervalMs"))) + " ms", e);
	data.publish("ble.interval", std::to_string(state.settings.ble_interval_ms), e);
	data.publish("ble.window", std::to_string(state.settings.ble_window_ms), e);
	const uint64_t now = esp_timer_get_time() / 1000;
	auto wheels = application::active_wheels(state);
	int pages = std::max(1, int((wheels.size() + 3) / 4));
	wheel_page = (wheel_page % pages + pages) % pages;
	for(size_t i = 0; i < 4; ++i)
	{
		const size_t index = size_t(wheel_page) * 4 + i;
		const std::string name = "tpms." + std::to_string(i + 1);
		std::string title = "--";
		if(index >= wheels.size())
		{
			data.invalidate(name, e);
			data.publish("wheel." + std::to_string(i + 1), title, e);
			continue;
		}

		const auto &s = wheels[index];
		char address[16];
		snprintf(address, sizeof(address), "%06lX", (unsigned long)s.code);
		// IDs remain readable even with the Latin-only small MCU font.
		title =
		    std::to_string(index + 1) + " / " +
		    (!s.identity.empty() ? s.identity
		                     : (s.code ? std::string(address) : "unassigned"));
#if APP_DISPLAY_WIDTH == 800
		title =
		    s.position + "\n" +
		    (!s.identity.empty() ? s.identity
		                     : (s.code ? std::string(address) : "unassigned"));
#endif
		data.publish("wheel." + std::to_string(i + 1), title, e);
		if(!s.present)
		{
			data.invalidate(name, e);
			continue;
		}

		data.publish(
			name,
			DataValue::Object{
			{"address", address},
			{"bar", s.reading.pressure().bars()},
			{"temperature", s.reading.temperature().celsius},
			{"age", (now >= s.last_seen_ms ? now - s.last_seen_ms : 0) / 1000}},
			e, DataState::Known);
	}
	char clock[9] = "--:--:--";
	time_t t = time(nullptr);
	if(t > 1700000000)
	{
		tm local{};
		localtime_r(&t, &local);
		strftime(clock, sizeof(clock), "%H:%M:%S", &local);
	}

	std::string vehicle = "Garage";
	for(auto &c : state.settings.garage)
		if(c.id == state.settings.active)
			vehicle = c.name;

	// Montserrat build is Latin: neutral title; actual localized vehicle name on
	// website.
	data.publish(
		"receiver",
		DataValue::Object{
		{"vehicle", vehicle},
		{"network", "IP: " + state.ip},
		{"bluetooth", "BLE: " + state.scan + " / " + state.scan_mode},
		{"reports", "Reports: " + std::to_string(state.reports) + " / lost " +
		 std::to_string(state.dropped)},
		{"page", "Wheels " + std::to_string(wheel_page + 1) + " / " +
		 std::to_string(pages)},
		{"clock", clock}},
		e);
	publish_research("tom", state.tomtom, tom_page, tom_selection, now);
	publish_research("bt", state.research, bt_page, bt_selection, now);
	data.flush();
}

void dashboard_request(int page, bool capture)
{
	if(page >= 0 && page < 4)
		requested_page.store(page);

	if(capture)
		capture_requested.store(true);
}

application::Value dashboard_status()
{
	std::lock_guard l(debug_mutex);
	return ui_status;
}

bool dashboard_control(const std::string &id)
{
	if(id != "scheme-light" && id != "scheme-cream" && id != "scheme-emerald" && id != "scheme-dark" &&
	    id != "scheme-night" && id != "wifi-open" && id != "wifi-close" && id != "interval-minus" &&
	    id != "interval-plus" && id != "window-minus" && id != "window-plus") return false;

	std::lock_guard lock(control_mutex);
	if(!requested_control.empty()) return false;

	requested_control = id; return true;
}

application::Value dashboard_frame(size_t offset)
{
	std::lock_guard l(debug_mutex);
	static const char hex[] = "0123456789abcdef";
	std::string s;
	size_t end = std::min(frame.size(), offset + 2048);
	for(size_t i = offset; i < end; ++i)
	{
		s += hex[frame[i] >> 4];
		s += hex[frame[i] & 15];
	}
	return DataValue::Object{
		{"hex", s}, {"total", frame.size()}, {"width", 800}, {"height", 480}};
}

void dashboard_poll()
{
	int page = requested_page.exchange(-1);
	if(project && page >= 0)
	{
		auto *button = project->find(("nav-" + std::to_string(page)).c_str());
		if(button)
			lv_obj_send_event(button, LV_EVENT_CLICKED, nullptr);
	}

	std::string control;
	{std::lock_guard lock(control_mutex); control.swap(requested_control);}
	if(project && !control.empty())
	{
		for(const auto &view:project->status().views)
			if(view.selected == "radio" || view.selected == "radio-small")
				if(auto *button = project->find(view.page_instance, control.c_str()))
					lv_obj_send_event(button, LV_EVENT_CLICKED, nullptr);
	}

	lv_timer_handler();
	static uint64_t next_status{};
	const uint64_t now = esp_timer_get_time() / 1000;
	if(project && now >= next_status)
	{
		next_status = now + 500;
		auto status = project->status();
		DataValue::Array tabs;
		for(int i = 0; i < 4; ++i)
			if(auto *tab = project->find(("nav-" + std::to_string(i)).c_str()))
			{
				lv_area_t bounds; lv_obj_get_coords(tab, &bounds);
				tabs.push_back(DataValue::Object{{"bounds",
						DataValue::Array{bounds.x1, bounds.y1, bounds.x2, bounds.y2}},
						{"radius", lv_obj_get_style_radius(tab, LV_PART_MAIN)},
						{"background", lv_color_to_u32(lv_obj_get_style_bg_color(tab, LV_PART_MAIN)) & 0xffffff}});
			}

		DataValue::Array views;
		for(auto &v : status.views)
			views.push_back(DataValue::Object{
				{"node", v.node}, {"selected", v.selected}, {"error", v.error}});
		DataValue::Object paint;
		auto inspect = [&](const char *id, lv_obj_t *node){
				if(!node) return;

				lv_area_t bounds; lv_obj_get_coords(node, &bounds);
				paint[id] = DataValue::Object{
					{"bounds", DataValue::Array{bounds.x1, bounds.y1, bounds.x2, bounds.y2}},
					{"background", lv_color_to_u32(lv_obj_get_style_bg_color(node, LV_PART_MAIN)) & 0xffffff},
					{"opacity", lv_obj_get_style_bg_opa(node, LV_PART_MAIN)},
					{"text", lv_color_to_u32(lv_obj_get_style_text_color(node, LV_PART_MAIN)) & 0xffffff},
					{"border", lv_obj_get_style_border_width(node, LV_PART_MAIN)},
					{"outline", lv_obj_get_style_outline_width(node, LV_PART_MAIN)},
					{"checked", bool(lv_obj_get_state(node) & LV_STATE_CHECKED)}};
			};
		for(const char *id:{"nav-0", "nav-1", "nav-2", "nav-3"})
			inspect(id, project->find(id));
		for(const auto &view:status.views)
			for(const char *id:{"pressure-0", "pressure-unit-0", "temp-0", "age-0", "tom-row-0", "bt-row-0",
			                    "scheme-night", "scheme-cream"})
				inspect(id, project->find(view.page_instance, id));
		std::lock_guard l(debug_mutex);
		ui_status = DataValue::Object{{"screen", status.displayed_screen},
			{"scheme", appearance ? appearance->selected() : ""},
			{"canvasColor",
			 project->root() ? (lv_color_to_u32(lv_obj_get_style_bg_color(project->root(),
			 LV_PART_MAIN)) & 0xffffff) : 0},
			{"tabs", tabs},
			{"paint", paint},
			{"views", views},
			{"registry", data.snapshot_json()}};
#if APP_DISPLAY_WIDTH == 800
		const auto refresh = display_refresh_stats();
		std::get<DataValue::Object>(ui_status.value)["driver"] = DataValue::Object{
			{"mode", "direct"}, {"refreshes", refresh.refreshes}, {"areas", refresh.areas},
			{"pixels", refresh.pixels}, {"scanoutFrames", display_frame_count()},
			{"bounceRows", display_bounce_rows}, {"core", refresh.core}};
#endif
	}

#if APP_DISPLAY_WIDTH == 800
	if(capture_requested.exchange(false))
	{
		lv_refr_now(nullptr);
		std::vector<uint8_t> pixels;
		pixels.reserve(800 * 480 * 2);
		for(int y = 0; y < 480; ++y)
			for(int x = 0; x < 800; ++x)
			{
				auto c = display_pixel(x, y);
				pixels.push_back(c & 255);
				pixels.push_back(c >> 8);
			}
		std::lock_guard l(debug_mutex);
		frame = std::move(pixels);
	}

#endif
}
