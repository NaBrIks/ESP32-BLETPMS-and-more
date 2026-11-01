// SPDX-FileCopyrightText: 2026 Oleg Kozhevnikov
// SPDX-License-Identifier: MIT
#include "NaWFstore/NvsProfileStore.hpp"
#if NAWFSTORE_WITH_NVS
#include <nvs.h>
#include <esp_err.h>
#include <algorithm>
#include <limits>

namespace NaWFstore
{
	namespace
	{
		constexpr size_t max_blob = 2048;
		constexpr const char *keys[] = {"cfg0", "cfg1"};

		uint32_t crc32(const std::vector<uint8_t> &bytes, size_t length)
		{
			uint32_t crc = 0xffffffff;
			for(size_t i = 0; i < length; ++i)
			{
				crc ^= bytes[i];
				for(unsigned bit = 0; bit < 8; ++bit)
					crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
			}
			return ~crc;
		}

		void number(std::vector<uint8_t> &out, uint64_t value, unsigned count)
		{
			for(unsigned i = 0; i < count; ++i)
				out.push_back(uint8_t(value >> (8 * i)));
		}

		void text(std::vector<uint8_t> &out, const std::string &value)
		{
			number(out, value.size(), 1);
			out.insert(out.end(), value.begin(), value.end());
		}

		std::vector<uint8_t> encode(const Config &config, uint64_t generation)
		{
			std::vector<uint8_t> out{'N', 'W', 'F', '1'};
			number(out, generation, 8);
			number(out, config.startup_enabled, 1);
			number(out, config.startup_window_seconds, 4);
			number(out, config.profiles.size(), 1);
			for(const auto &p : config.profiles)
			{
				number(out, p.id, 4);
				number(out, uint16_t(p.priority), 2);
				number(out, p.enabled, 1);
				text(out, p.ssid);
				text(out, p.password);
			}
			number(out, crc32(out, out.size()), 4);
			return out;
		}

		class Reader
		{
		public:
			explicit Reader(const std::vector<uint8_t> &bytes)
			  : bytes_(bytes)
			{
			}

			uint64_t number(unsigned count)
			{
				if(position_ + count > bytes_.size()) { good = false; return 0; }

				uint64_t result = 0;
				for(unsigned i = 0; i < count; ++i)
					result |= uint64_t(bytes_[position_++]) << (8 * i);
				return result;
			}

			std::string text()
			{
				const auto count = size_t(number(1));
				if(!good || position_ + count > bytes_.size()) { good = false; return {}; }

				std::string result(bytes_.begin() + position_, bytes_.begin() + position_ + count);
				position_ += count;
				return result;
			}

			size_t position() const
			{
				return position_;
			}

			bool good{true};
		private:
			const std::vector<uint8_t> &bytes_;
			size_t position_{};
		};

		bool decode(const std::vector<uint8_t> &bytes, Config &config, uint64_t &generation)
		{
			if(bytes.size() < 22 || bytes.size() > max_blob || bytes[0] != 'N' || bytes[1] != 'W' ||
			    bytes[2] != 'F' || bytes[3] != '1') return false;

			Reader reader(bytes);
			(void)reader.number(4);
			generation = reader.number(8);
			const auto enabled = reader.number(1);
			Config result;
			result.startup_enabled = enabled != 0;
			result.startup_window_seconds = uint32_t(reader.number(4));
			const auto count = reader.number(1);
			if(count > NAWFSTORE_MAX_PROFILES || enabled > 1 || !generation) return false;

			for(size_t i = 0; i < count; ++i)
			{
				Profile p;
				p.id = uint32_t(reader.number(4));
				const auto priority = uint16_t(reader.number(2));
				p.priority = int16_t(priority < 0x8000 ? int(priority) : int(priority) - 65536);
				const auto on = reader.number(1);
				if(on > 1) return false;

				p.enabled = on != 0;
				p.ssid = reader.text();
				p.password = reader.text();
				result.profiles.push_back(p);
			}
			if(!reader.good || reader.position() + 4 != bytes.size()) return false;

			const auto crc = uint32_t(reader.number(4));
			if(crc != crc32(bytes, bytes.size() - 4)) return false;

			std::string error;
			if(!validate(result, error)) return false;

			config = result;
			return true;
		}

		esp_err_t read_blob(nvs_handle_t handle, const char *key, std::vector<uint8_t> &bytes)
		{
			size_t size = 0;
			auto error = nvs_get_blob(handle, key, nullptr, &size);
			if(error != ESP_OK) return error;

			if(size > max_blob) return ESP_ERR_NVS_INVALID_LENGTH;

			bytes.resize(size);
			return nvs_get_blob(handle, key, bytes.data(), &size);
		}

	}

	NvsProfileStore::NvsProfileStore(const char *name_space)
	  : namespace_(name_space ? name_space : "nawfstore")
	{
	}

	LoadResult NvsProfileStore::load(Config &config, std::string &error)
	{
		loaded_ = false;
		nvs_handle_t handle;
		const auto opened = nvs_open(namespace_.c_str(), NVS_READONLY, &handle);
		if(opened == ESP_ERR_NVS_NOT_FOUND)
		{
			generation_ = 0;
			active_slot_ = -1;
			loaded_ = true;
			error.clear();
			return LoadResult::missing;
		}

		if(opened != ESP_OK)
		{
			error = esp_err_to_name(opened);
			return LoadResult::error;
		}

		bool present = false, valid = false;
		uint64_t best = 0;
		int best_slot = -1;
		Config selected;
		for(int slot = 0; slot < 2; ++slot)
		{
			std::vector<uint8_t> bytes;
			const auto result = read_blob(handle, keys[slot], bytes);
			if(result == ESP_ERR_NVS_NOT_FOUND) continue;

			present = true;
			Config candidate;
			uint64_t generation = 0;
			if(result == ESP_OK && decode(bytes, candidate, generation) && generation > best)
			{
				valid = true;
				best = generation;
				best_slot = slot;
				selected = candidate;
			}
		}
		nvs_close(handle);
		if(present && !valid)
		{
			error = "nvs_configuration_invalid";
			return LoadResult::error;
		}

		generation_ = best;
		active_slot_ = best_slot;
		loaded_ = true;
		error.clear();
		if(valid) config = selected;

		return valid ? LoadResult::loaded : LoadResult::missing;
	}

	bool NvsProfileStore::save(const Config &config, std::string &error)
	{
		if(!loaded_ || generation_ == std::numeric_limits<uint64_t>::max())
		{
			error = "nvs_store_not_writable";
			return false;
		}

		if(!validate(config, error)) return false;

		const int slot = active_slot_ == 0 ? 1 : 0;
		const auto generation = generation_ + 1;
		const auto bytes = encode(config, generation);
		nvs_handle_t handle;
		auto result = nvs_open(namespace_.c_str(), NVS_READWRITE, &handle);
		if(result != ESP_OK)
		{
			error = esp_err_to_name(result);
			return false;
		}

		result = nvs_set_blob(handle, keys[slot], bytes.data(), bytes.size());
		if(result == ESP_OK) result = nvs_commit(handle);

		std::vector<uint8_t> readback;
		if(result == ESP_OK) result = read_blob(handle, keys[slot], readback);

		nvs_close(handle);
		if(result != ESP_OK || bytes != readback)
		{
			error = result != ESP_OK ? esp_err_to_name(result) : "nvs_verify_failed";
			return false;
		}

		generation_ = generation;
		active_slot_ = slot;
		error.clear();
		return true;
	}

}
#endif
