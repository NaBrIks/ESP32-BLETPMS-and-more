// SPDX-FileCopyrightText: 2026 Oleg Kozhevnikov
// SPDX-License-Identifier: MIT
#include "settings.hpp"
#include "sensor_config.hpp"
#include "tpms_types.hpp"
#include <algorithm>
#include <cJSON.h>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <deque>
#include <dirent.h>
#include <esp_log.h>
#include <esp_random.h>
#include <esp_spiffs.h>
#include <esp_timer.h>
#include <esp_system.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <mutex>
#include <optional>
#include <set>
#include <sys/stat.h>
#include <unistd.h>

namespace application
{
	namespace
	{
		constexpr size_t settings_limit = 32768, log_limit = 131072, reserve = 1048576;
		const std::string root = "/project/";
		std::mutex shared_mutex, fs_mutex;
		Settings current;
		bool mounted{}, healthy{}, logging = true;
		std::string storage_error = "starting", journal_error;
		uint32_t next_ticket = 1, journal_dropped{};
		uint64_t journal_deadline{}, generation{};
		uint32_t boot_id{};
		unsigned active_slot{};
		struct Job {
			uint32_t id;
			std::string body;
			std::optional<KnownSensor> discovered;
		};
		std::deque<Job> jobs;
		std::deque<std::string> logs;
		std::deque<Value> results;
		Value from_json(const cJSON *p, unsigned depth = 0)
		{
			if(!p || depth > 12)
				return {};

			if(cJSON_IsString(p))
				return p->valuestring;

			if(cJSON_IsBool(p))
				return bool(cJSON_IsTrue(p));

			if(cJSON_IsNumber(p))
				return p->valuedouble;

			if(cJSON_IsArray(p))
			{
				Value::Array a;
				for(auto *c = p->child; c; c = c->next)
					a.push_back(from_json(c, depth + 1));
				return a;
			}

			if(cJSON_IsObject(p))
			{
				Value::Object o;
				for(auto *c = p->child; c; c = c->next)
					o[c->string] = from_json(c, depth + 1);
				return o;
			}

			return {};
		}

		std::string identity(const KnownSensor &k)
		{
			if(k.transport == "ble" && k.protocol == "tomtom")
			{
				char b[16];
				snprintf(b, sizeof(b), "%06lX", (unsigned long)k.code);
				return b;
			}

			return k.identity;
		}

		const Value::Array &array_value(const Value &v)
		{
			static const Value::Array empty;
			auto p = std::get_if<Value::Array>(&v.value);
			return p ? *p : empty;
		}

		uint32_t crc32(const uint8_t *p, size_t n)
		{
			uint32_t c = ~0u;
			while(n--)
			{
				c ^= *p++;
				for(int i = 0; i < 8; ++i)
					c = (c >> 1) ^ (0xedb88320u & uint32_t(-int(c & 1)));
			}
			return ~c;
		}

		void append64(std::vector<uint8_t> &b, uint64_t n)
		{
			for(int i = 0; i < 8; ++i)
				b.push_back(uint8_t(n >> (i * 8)));
		}

		uint64_t read64(const uint8_t *p)
		{
			uint64_t n{};
			for(int i = 0; i < 8; ++i)
				n |= uint64_t(p[i]) << (i * 8);
			return n;
		}

		bool read_slot(unsigned slot, Settings &s, uint64_t &gen,
		    unsigned *version = nullptr)
		{
			FILE *f = fopen((root + "tpms.cfg." + std::to_string(slot)).c_str(), "rb");
			if(!f)
				return false;

			std::vector<uint8_t> b(settings_limit + 33);
			size_t n = fread(b.data(), 1, b.size(), f);
			bool io = ferror(f) == 0;
			fclose(f);
			b.resize(n);
			if(!io || n < 32 || n > settings_limit + 32 ||
			    memcmp(b.data(), "TPMSCFG1", 8) || read64(b.data() + 16) != n - 32 ||
			    read64(b.data() + 24) != crc32(b.data() + 32, n - 32))
				return false;

			Value v;
			std::string e;
			if(!parse_json(std::string(b.begin() + 32, b.end()), v, e) ||
			    !settings_decode(v, s, e))
				return false;

			if(version)
				*version = unsigned(integer_value(member(v, "version")));

			gen = read64(b.data() + 8);
			return gen == uint64_t(s.revision) + 1;
		}

		bool commit(const Settings &s, std::string &error)
		{
			if(!mounted || !healthy)
			{
				error = "storage_unavailable: " + storage_error;
				return false;
			}

			std::string text = lvgl_cbor::data_value_json(settings_value(s));
			if(text.size() > settings_limit)
			{
				error = "settings_size_limit";
				return false;
			}

			size_t total{}, used{};
			if(esp_spiffs_info("project", &total, &used) != ESP_OK ||
			    total - used < reserve + 2 * (text.size() + 32) + 1024)
			{
				error = "shared_fs_reserve";
				return false;
			}

			const unsigned slot = generation ? 1 - active_slot : 0;
			const std::string path = root + "tpms.cfg." + std::to_string(slot);
			// Never adopt/overwrite an unrelated file with our reserved name.
			if(!generation)
			{
				struct stat st {};
				if(stat(path.c_str(), &st) == 0)
				{
					error = "namespace_collision";
					return false;
				}
			}

			std::vector<uint8_t> b{'T', 'P', 'M', 'S', 'C', 'F', 'G', '1'};
			append64(b, generation + 1);
			append64(b, text.size());
			append64(b,
			    crc32(reinterpret_cast<const uint8_t *>(text.data()), text.size()));
			b.insert(b.end(), text.begin(), text.end());
			FILE *f = fopen(path.c_str(), "wb");
			if(!f)
			{
				error = "settings_open";
				return false;
			}

			bool ok = fwrite(b.data(), 1, b.size(), f) == b.size() && fflush(f) == 0;
			if(ok)
			{
				ok = fsync(fileno(f)) == 0;
			}

			if(fclose(f))
				ok = false;

			Settings verified;
			uint64_t gen{};
			if(!ok || !read_slot(slot, verified, gen) || gen != generation + 1 ||
			    lvgl_cbor::data_value_json(settings_value(verified)) != text)
			{
				error = "settings_verify";
				return false;
			}

			generation = gen;
			active_slot = slot;
			return true;
		}

		bool apply(Settings &s, const Value &v, std::string &e,
		    const KnownSensor *discovered = nullptr)
		{
			auto fail = [&](const char *why) {
					e = why;
					return false;
				};
			if(integer_value(member(v, "revision"), -1) != s.revision)
				return fail("revision_conflict");

			const auto op = string_value(member(v, "op"));
			if(op == "radio.put")
			{
				const auto seconds = integer_value(member(v, "wifiWindowSeconds"), -1);
				const auto interval = integer_value(member(v, "bleIntervalMs"), -1);
				const auto window = integer_value(member(v, "bleWindowMs"), -1);
				const auto wifi_enabled = std::get_if<bool>(&member(v, "wifiEnabled").value);
				if(seconds < 0 || seconds > 86400 || !wifi_enabled || interval < 5 || interval > 10000 || window < 5 ||
				    window > interval || interval % 5 || window % 5) return fail("radio_settings_invalid");

				s.wifi_window_seconds = uint32_t(seconds);
				s.ble_interval_ms = uint32_t(interval); s.ble_window_ms = uint32_t(window);
				s.wifi_enabled = *wifi_enabled;
			}
			else if(op == "appearance.put")
			{
				const auto theme = string_value(member(v, "theme"));
				if(theme != "light" && theme != "cream" && theme != "emerald" && theme != "dark" && theme != "night")
					return fail("appearance_invalid");

				s.native_theme = theme;
			}
			else if(op == "known.put")
				return fail("immutable_sensor_identity");
			else if(op == "known.bind")
			{
				if(!discovered)
					return fail("discovery_proof_required");

				const auto *driver = tpms_driver(discovered->driver);
				if(!driver || transport_name(driver->transport) != discovered->transport ||
				    protocol_name(driver->protocol) != discovered->protocol)
					return fail("driver_unavailable");

				auto found = std::find_if(s.known.begin(), s.known.end(), [&](auto &k) {
					return k.id == discovered->id || (k.transport == discovered->transport &&
					       k.protocol == discovered->protocol &&
					       k.identity == discovered->identity);
				});
				if(found == s.known.end())
					s.known.push_back(*discovered);
				else if(found->transport != discovered->transport ||
				    found->protocol != discovered->protocol ||
				    found->code != discovered->code)
					return fail("identity_conflict");
				else if(found->transport_identity.empty())
					found->transport_identity = discovered->transport_identity;
			}
			else if(op == "known.tags")
			{
				const auto id = string_value(member(v, "id"));
				auto found = std::find_if(s.known.begin(), s.known.end(),
				        [&](auto &k) {
					return k.id == id;
				});
				if(found == s.known.end())
					return fail("known_sensor_missing");

				const auto *tags = std::get_if<Value::Array>(&member(v, "tags").value);
				if(!tags || tags->size() > 8)
					return fail("sensor_tags_invalid");

				found->tags.clear();
				for(auto &tag : *tags)
				{
					const auto *text = std::get_if<std::string>(&tag.value);
					if(!text)
						return fail("sensor_tags_invalid");

					found->tags.push_back(*text);
				}
			}
			else if(op == "known.remove")
			{
				auto id = string_value(member(v, "id"));
				for(auto &c : s.garage)
					for(auto &a : c.axles)
						for(auto &w : a.wheels)
							if(w.sensor == id)
								return fail("sensor_assigned");

				std::erase_if(s.known, [&](auto &k) {
					return k.id == id;
				});
			}
			else if(op == "vehicle.put")
			{
				Settings fragment;
				Value::Object tmp{
					{"version", 2},
					{"revision", s.revision},
					{"active", ""},
					{"known", array_value(member(settings_value(s), "known"))},
					{"garage", Value::Array{member(v, "vehicle")}}};
				if(!settings_decode(tmp, fragment, e))
					return false;

				auto car = fragment.garage.front();
				auto found = std::find_if(s.garage.begin(), s.garage.end(),
				        [&](auto &x) {
					return x.id == car.id;
				});
				if(found == s.garage.end())
					s.garage.push_back(car);
				else
					*found = car;

				const auto *make_default = std::get_if<bool>(&member(v, "makeDefault").value);
				if(s.active.empty() || (make_default && *make_default))
					s.active = car.id;
			}
			else if(op == "vehicle.remove")
			{
				auto id = string_value(member(v, "id"));
				std::erase_if(s.garage, [&](auto &c) {
					return c.id == id;
				});
				if(s.active == id)
					s.active = s.garage.empty() ? "" : s.garage.front().id;
			}
			else if(op == "vehicle.select")
				s.active = string_value(member(v, "id"));
			else
				return fail("unknown_operation");

			++s.revision;
			Settings checked;
			if(!settings_decode(settings_value(s), checked, e))
				return false;

			s = std::move(checked);
			return true;
		}

		void write_logs(std::deque<std::string> &batch)
		{
			if(batch.empty())
				return;

			auto failed = [&](const char *why) {
					std::lock_guard l(shared_mutex);
					journal_dropped += batch.size();
					journal_error = why;
				};
			if(!mounted)
			{
				failed("filesystem_unavailable");
				return;
			}

			size_t bytes{};
			for(auto &line : batch)
				bytes += line.size() + 1;
			size_t total{}, used{};
			if(esp_spiffs_info("project", &total, &used) != ESP_OK ||
			    total - used < reserve + 2 * bytes + 1024)
			{
				failed("shared_fs_reserve");
				return;
			}

			struct stat st {};
			const auto path = root + "tpms.diag.0";
			if(stat(path.c_str(), &st) == 0 && size_t(st.st_size) + bytes > log_limit)
			{
				if(remove((root + "tpms.diag.1").c_str()) && errno != ENOENT)
				{
					failed("journal_remove");
					return;
				}

				if(rename(path.c_str(), (root + "tpms.diag.1").c_str()))
				{
					failed("journal_rotate");
					return;
				}
			}

			FILE *f = fopen(path.c_str(), "ab");
			if(!f)
			{
				failed("journal_open");
				return;
			}

			bool ok = true;
			for(auto &line : batch)
				if(fwrite(line.data(), 1, line.size(), f) != line.size() ||
				    fputc('\n', f) == EOF)
				{
					ok = false;
					break;
				}

			if(fflush(f) || fsync(fileno(f)))
				ok = false;

			if(fclose(f))
				ok = false;

			if(!ok)
				failed("journal_write");
		}

		void worker(void *)
		{
			for(;;)
			{
				Job job{};
				std::deque<std::string> batch;
				{
					std::lock_guard l(shared_mutex);
					if(!jobs.empty())
					{
						job = std::move(jobs.front());
						jobs.pop_front();
					}

					batch.swap(logs);
				}
				if(job.id)
				{
					Settings next;
					{
						std::lock_guard l(shared_mutex);
						next = current;
					}
					Value command;
					std::string error;
					bool ok = parse_json(job.body, command, error) &&
					    apply(next, command, error,
					        job.discovered ? &*job.discovered : nullptr);
					if(ok)
					{
						std::lock_guard l(fs_mutex);
						ok = commit(next, error);
					}

					{
						std::lock_guard l(shared_mutex);
						if(ok)
							current = std::move(next);

						results.push_back(Value::Object{{"id", job.id},
								{"done", true},
								{"ok", ok},
								{"error", error},
								{"revision", current.revision}});
						while(results.size() > 16)
							results.pop_front();
					}
				}

				{
					std::lock_guard l(fs_mutex);
					write_logs(batch);
				}
				vTaskDelay(pdMS_TO_TICKS(100));
			}
		}

	} // namespace
	const Value &member(const Value &v, const std::string &key)
	{
		static const Value nil;
		auto p = std::get_if<Value::Object>(&v.value);
		if(!p)
			return nil;

		auto i = p->find(key);
		return i == p->end() ? nil : i->second;
	}

	std::string string_value(const Value &v)
	{
		auto p = std::get_if<std::string>(&v.value);
		return p ? *p : std::string{};
	}

	int64_t integer_value(const Value &v, int64_t fallback)
	{
		if(auto p = std::get_if<int64_t>(&v.value))
			return *p;

		if(auto p = std::get_if<double>(&v.value);
		    p &&std::isfinite(*p) && *p >= -9007199254740991.0 &&
		    *p <= 9007199254740991.0 && std::trunc(*p) == *p)
			return int64_t(*p);

		return fallback;
	}

	bool parse_json(const std::string &s, Value &v, std::string &e)
	{
		if(s.empty() || s.size() > settings_limit)
		{
			e = "json_size";
			return false;
		}

		// Bound nesting before cJSON's recursive parser.
		int depth = 0;
		bool quoted = false, escaped = false;
		for(char c : s)
		{
			if(quoted)
			{
				if(escaped)
					escaped = false;
				else if(c == '\\')
					escaped = true;
				else if(c == '"')
					quoted = false;
			}
			else if(c == '"')
				quoted = true;
			else if(c == '[' || c == '{')
			{
				if(++depth > 12)
				{
					e = "json_depth";
					return false;
				}
			}
			else if(c == ']' || c == '}')
				--depth;
		}
		const char *end{};
		auto *p = cJSON_ParseWithLengthOpts(s.c_str(), s.size() + 1, &end, true);
		if(!p)
		{
			e = "invalid_json";
			return false;
		}

		v = from_json(p);
		cJSON_Delete(p);
		return true;
	}

	Value settings_value(const Settings &s)
	{
		Value::Array known, cars;
		for(auto &k : s.known)
		{
			Value::Array tags;
			for(auto &tag : k.tags)
				tags.push_back(tag);
			known.push_back(Value::Object{{"id", k.id},
					{"driver", k.driver},
					{"transportIdentity", k.transport_identity},
					{"protocolId", identity(k)},
					{"transport", k.transport},
					{"protocol", k.protocol},
					{"code", k.code},
					{"identity", identity(k)},
					{"tags", tags}});
		}
		for(auto &c : s.garage)
		{
			Value::Array axles;
			for(auto &a : c.axles)
			{
				Value::Array wheels;
				for(auto &w : a.wheels)
					wheels.push_back(Value::Object{{"name", w.name}, {"sensor", w.sensor}});
				axles.push_back(Value::Object{{"name", a.name}, {"wheels", wheels}});
			}
			cars.push_back(
				Value::Object{{"id", c.id}, {"name", c.name}, {"axles", axles}});
		}
		return Value::Object{{"version", 2},
			{"revision", s.revision},
			{"wifiWindowSeconds", s.wifi_window_seconds},
			{"bleIntervalMs", s.ble_interval_ms}, {"bleWindowMs", s.ble_window_ms},
			{"nativeTheme", s.native_theme}, {"wifiEnabled", s.wifi_enabled},
			{"active", s.active},
			{"known", known},
			{"garage", cars}};
	}

	bool settings_decode(const Value &v, Settings &s, std::string &e)
	{
		s = {};
		auto fail = [&](const char *why) {
				e = why;
				return false;
			};
		if(integer_value(member(v, "version")) != 1 &&
		    integer_value(member(v, "version")) != 2)
			return fail("settings_version");

		auto rev = integer_value(member(v, "revision"), -1);
		if(rev < 0 || rev > 0xfffffffe)
			return fail("settings_revision");

		s.revision = uint32_t(rev);
		for(auto [key, target]:{std::pair{"wifiEnabled", &s.wifi_enabled}})
		{
			const auto &value = member(v, key);
			if(!std::holds_alternative<std::monostate>(value.value))
			{
				auto enabled = std::get_if<bool>(&value.value);
				if(!enabled) return fail("radio_settings_invalid");

				*target = *enabled;
			}
		}
		for(auto [key, target]:{std::pair{"bleIntervalMs", &s.ble_interval_ms},
		                        std::pair{"bleWindowMs", &s.ble_window_ms}})
		{
			if(!std::holds_alternative<std::monostate>(member(v, key).value))
			{
				const auto ms = integer_value(member(v, key), -1);
				if(ms < 5 || ms > 10000 || ms % 5) return fail("radio_settings_invalid");

				*target = uint32_t(ms);
			}
		}
		if(s.ble_window_ms > s.ble_interval_ms) return fail("radio_settings_invalid");

		if(!std::holds_alternative<std::monostate>(member(v, "nativeTheme").value))
		{
			s.native_theme = string_value(member(v, "nativeTheme"));
			if(s.native_theme != "light" && s.native_theme != "cream" && s.native_theme != "emerald" &&
			    s.native_theme != "dark" && s.native_theme != "night") return fail("appearance_invalid");
		}

		if(!std::holds_alternative<std::monostate>(member(v, "wifiWindowSeconds").value))
		{
			auto seconds = integer_value(member(v, "wifiWindowSeconds"), -1);
			if(seconds < 0 || seconds > 86400)  return fail("wifi_window_range");

			s.wifi_window_seconds = uint32_t(seconds);
		}

		s.active = string_value(member(v, "active"));
		auto valid = [](const std::string &t, size_t max) {
				return !t.empty() && t.size() <= max &&
				       std::none_of(t.begin(), t.end(),
				           [](unsigned char c) {
				return c < 32;
			});
			};
		std::set<std::string> ids, physical;
		const auto &known = array_value(member(v, "known")),
		&cars = array_value(member(v, "garage"));
		if(!std::holds_alternative<Value::Array>(member(v, "known").value) ||
		    !std::holds_alternative<Value::Array>(member(v, "garage").value))
			return fail("settings_arrays");

		if(known.size() > 64 || cars.size() > 16)
			return fail("settings_capacity");

		for(auto &x : known)
		{
			KnownSensor k{string_value(member(x, "id")),
				          string_value(member(x, "transport")),
				          string_value(member(x, "protocol")),
				          uint32_t(integer_value(member(x, "code"))),
				          string_value(member(x, "identity")),
				          string_value(member(x, "driver")),
				          string_value(member(x, "transportIdentity"))};
			if(k.driver.empty() && k.transport == "ble" && k.protocol == "tomtom")
				k.driver = "tomtom-ble";

			if(k.driver.empty())
				k.driver = k.protocol + "-" + k.transport;

			if(!std::holds_alternative<std::monostate>(member(x, "tags").value))
			{
				const auto *tags = std::get_if<Value::Array>(&member(x, "tags").value);
				if(!tags || tags->size() > 8)  return fail("sensor_tags_invalid");

				std::set<std::string> unique_tags;
				for(auto &tag : *tags)
				{
					const auto *text = std::get_if<std::string>(&tag.value);
					if(!text || !valid(*text, 64) || !unique_tags.insert(*text).second)
						return fail("sensor_tags_invalid");

					k.tags.push_back(*text);
				}
			}

			auto raw_code =
			    std::holds_alternative<std::monostate>(member(x, "code").value)
			? 0
			: integer_value(member(x, "code"), -1);
			if(raw_code < 0 || raw_code > 0xffffff)
				return fail("known_sensor_code");

			k.identity = identity(k);
			if(!valid(k.id, 48) || !valid(k.transport, 16) || !valid(k.protocol, 32) ||
			    !valid(k.driver, 48) || k.transport_identity.size() > 96 ||
			    !ids.insert(k.id).second || !valid(k.identity, 96) ||
			    ((k.transport == "ble" && k.protocol == "tomtom") && !k.code) ||
			    !physical.insert(k.transport + ":" + k.protocol + ":" + k.identity)
			    .second)
				return fail("known_sensor_invalid");

			s.known.push_back(k);
		}
		std::set<std::string> carids;
		size_t count = 0;
		for(auto &x : cars)
		{
			Vehicle c{
				string_value(member(x, "id")), string_value(member(x, "name")), {}};
			if(!valid(c.id, 48) || !valid(c.name, 96) || !carids.insert(c.id).second)
				return fail("vehicle_invalid");

			const auto &axles = array_value(member(x, "axles"));
			if(axles.empty() || axles.size() > 16)
				return fail("axles_capacity");

			std::set<std::string> assigned;
			for(auto &y : axles)
			{
				Axle a{string_value(member(y, "name")), {}};
				const auto &wheels = array_value(member(y, "wheels"));
				if(!valid(a.name, 64) || wheels.empty() || wheels.size() > 8)
					return fail("wheels_capacity");

				for(auto &z : wheels)
				{
					Wheel w{string_value(member(z, "name")),
						    string_value(member(z, "sensor"))};
					if(!valid(w.name, 64) ||
					    (!w.sensor.empty() &&
					    (!ids.count(w.sensor) || !assigned.insert(w.sensor).second)))
						return fail("wheel_assignment_invalid");

					a.wheels.push_back(w);
					if(++count > 128)
						return fail("total_wheels_capacity");
				}
				c.axles.push_back(a);
			}
			s.garage.push_back(c);
		}
		if(!s.active.empty() && !carids.count(s.active))
			return fail("active_vehicle_missing");

		return true;
	}

	Settings default_settings()
	{
		Settings s;
		s.active = "vehicle-1";
		Vehicle c{"vehicle-1", "Лето", {{"Передняя", {}}, {"Задняя", {}}}};
		for(size_t i = 0; i < configured_sensors.size(); ++i)
		{
			auto &k = configured_sensors[i];
			if(!k.code)
				continue;

			char code[16];
			snprintf(code, sizeof(code), "%06lX", (unsigned long)k.code);
			std::string id = std::string("ble-tomtom-") + code;
			s.known.push_back({id, "ble", "tomtom", k.code, code, "tomtom-ble", ""});
			c.axles[i / 2].wheels.push_back({k.label, id});
		}
		if(s.known.empty())
		{
			c.axles = {Axle{"Ось 1", {{"Колесо 1", ""}}}};
		}

		s.garage.push_back(c);
		return s;
	}

	void storage_start()
	{
		boot_id = esp_random();
		current = default_settings();
		esp_vfs_spiffs_conf_t cfg{};
		cfg.base_path = "/project";
		cfg.partition_label = "project";
		cfg.max_files = 8;
		cfg.format_if_mount_failed = false;
		auto result = esp_vfs_spiffs_register(&cfg);
		mounted = result == ESP_OK;
		healthy = mounted;
		storage_error = mounted ? "" : esp_err_to_name(result);
		if(mounted)
		{
			bool any = false;
			uint64_t best{};
			unsigned selected_version{};
			for(unsigned i = 0; i < 2; ++i)
			{
				struct stat st {};
				any |= stat((root + "tpms.cfg." + std::to_string(i)).c_str(), &st) == 0;
				Settings candidate;
				uint64_t gen{};
				unsigned version{};
				if(read_slot(i, candidate, gen, &version) && gen > best)
				{
					best = gen;
					current = std::move(candidate);
					active_slot = i;
					selected_version = version;
				}
			}
			generation = best;
			if(any && !best)
			{
				healthy = false;
				storage_error = "both_settings_slots_invalid";
			}

			if(best && selected_version < 2)
			{
				Settings migrated = current;
				++migrated.revision;
				std::string error;
				if(commit(migrated, error))
					current = std::move(migrated);
				else
				{
					healthy = false;
					storage_error = "settings_migration: " + error;
				}
			}

			if(!any)
			{
				std::string e;
				if(!commit(current, e))
				{
					healthy = false;
					storage_error = e;
				}
			}
		}

		journal_deadline =
		    uint64_t(esp_timer_get_time() / 1000) + 6 * 60 * 60 * 1000ULL;
		if(xTaskCreate(worker, "app_storage", 8192, nullptr, 2, nullptr) != pdPASS)
		{
			healthy = false;
			storage_error = "worker_allocation";
		}

		ESP_LOGI("APP_FS",
		    "shared FS mount=%s settings=%s revision=%lu (no formatting)",
		    mounted ? "ok" : "failed", healthy ? "ok" : storage_error.c_str(),
		    (unsigned long)current.revision);
		journal(lvgl_cbor::data_value_json(
				Value::Object{{"event", "boot"},
					{"us", esp_timer_get_time()},
					{"diagnosticsVersion", 2}, {"resetReason", int(esp_reset_reason())},
					{"wifiEnabled", current.wifi_enabled}, {"wifiWindowSeconds", current.wifi_window_seconds},
					{"bleIntervalMs", current.ble_interval_ms}, {"bleWindowMs", current.ble_window_ms},
					{"settingsRevision", current.revision}}));
	}

	bool settings_copy(Settings &s)
	{
		std::lock_guard l(shared_mutex);
		if(s.revision == current.revision && !s.garage.empty())
			return false;

		s = current;
		return true;
	}

	Value settings_snapshot()
	{
		std::lock_guard l(shared_mutex);
		return settings_value(current);
	}

	Value storage_status()
	{
		std::lock_guard l(shared_mutex);
		return Value::Object{
			{"mounted", mounted},
			{"writable", healthy},
			{"error", storage_error},
			{"logging",
			 logging &&uint64_t(esp_timer_get_time() / 1000) < journal_deadline},
			{"journalDropped", journal_dropped},
			{"journalError", journal_error},
			{"journalFileBytes", log_limit},
			{"journalFiles", 2},
			{"namespace", "tpms."}};
	}

	uint32_t submit_command(std::string body)
	{
		std::lock_guard l(shared_mutex);
		if(jobs.size() >= 4 || body.size() > 4096)
			return 0;

		uint32_t id = next_ticket++;
		jobs.push_back({id, std::move(body), {}});
		return id;
	}

	uint32_t submit_discovered(uint32_t revision, KnownSensor sensor)
	{
		std::lock_guard l(shared_mutex);
		if(jobs.size() >= 4)
			return 0;

		uint32_t id = next_ticket++;
		auto body = lvgl_cbor::data_value_json(
			Value::Object{{"op", "known.bind"}, {"revision", revision}});
		jobs.push_back({id, std::move(body), std::move(sensor)});
		return id;
	}

	Value command_status(uint32_t id)
	{
		std::lock_guard l(shared_mutex);
		for(auto &r : results)
			if(integer_value(member(r, "id")) == id)
				return r;

		return Value::Object{{"id", id}, {"done", false}};
	}

	void journal(std::string line)
	{
		if(line.size() > 3072)
			return;

		std::lock_guard l(shared_mutex);
		if(!logging || uint64_t(esp_timer_get_time() / 1000) > journal_deadline)
			return;

		if(logs.size() >= 32)
		{
			++journal_dropped;
			return;
		}

		logs.push_back("{\"boot\":" + std::to_string(boot_id) +
		    ",\"record\":" + line + "}");
	}

	Value files_list()
	{
		std::lock_guard l(fs_mutex);
		size_t total{}, used{};
		Value::Array entries;
		if(mounted)
		{
			esp_spiffs_info("project", &total, &used);
			DIR *d = opendir("/project");
			if(d)
			{
				while(auto *entry = readdir(d))
				{
					if(entries.size() >= 256)
						break;

					std::string name = entry->d_name;
					struct stat st {};
					if(stat((root + name).c_str(), &st))
						continue;

					entries.push_back(Value::Object{
						{"name", name},
						{"bytes", int64_t(st.st_size)},
						{"owner", name.starts_with("tpms.") ? "TPMS" : "other"}});
				}
				closedir(d);
			}
		}

		return Value::Object{{"mounted", mounted},
			{"total", total},
			{"used", used},
			{"reserve", reserve},
			{"files", entries}};
	}

	bool file_read(const std::string &name, size_t offset, std::string &out,
	    size_t &total)
	{
		if(name.empty() || name.size() > 63 ||
		    name.find("..") != std::string::npos || name.front() == '/' ||
		    name.find('\\') != std::string::npos)
			return false;

		std::lock_guard l(fs_mutex);
		if(!mounted)
			return false;

		FILE *f = fopen((root + name).c_str(), "rb");
		if(!f)
			return false;

		fseek(f, 0, SEEK_END);
		long n = ftell(f);
		if(n < 0 || offset > size_t(n))
		{
			fclose(f);
			return false;
		}

		total = n;
		fseek(f, offset, SEEK_SET);
		out.resize(std::min<size_t>(4096, total - offset));
		size_t read = fread(out.data(), 1, out.size(), f);
		bool ok = read == out.size() && !ferror(f);
		fclose(f);
		return ok;
	}

} // namespace application
