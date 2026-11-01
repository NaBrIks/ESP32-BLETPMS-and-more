// SPDX-FileCopyrightText: 2026 Oleg Kozhevnikov
// SPDX-License-Identifier: MIT
#include "NaWFstore/NvsProfileStore.hpp"
#include "nvs.h"
#include <cassert>
#include <cstring>
#include <map>
#include <vector>
using namespace NaWFstore;
static std::map<std::string, std::vector<unsigned char> > blobs;
static bool exists = false;
static bool fail_write = false;
static bool fail_commit = false;
static bool corrupt_readback = false;

esp_err_t nvs_open(const char *, int mode, nvs_handle_t *handle)
{
	if(!exists && mode == NVS_READONLY) return ESP_ERR_NVS_NOT_FOUND;

	exists = true;
	*handle = 1;
	return ESP_OK;
}

void nvs_close(nvs_handle_t)
{
}

esp_err_t nvs_get_blob(nvs_handle_t, const char *key, void *out, size_t *size)
{
	const auto found = blobs.find(key);
	if(found == blobs.end()) return ESP_ERR_NVS_NOT_FOUND;

	if(!out) { *size = found->second.size(); return ESP_OK; }

	if(*size < found->second.size()) return ESP_ERR_NVS_INVALID_LENGTH;

	*size = found->second.size();
	std::memcpy(out, found->second.data(), *size);
	if(corrupt_readback && *size) static_cast<unsigned char *>(out)[0] ^= 1;

	return ESP_OK;
}

esp_err_t nvs_set_blob(nvs_handle_t, const char *key, const void *bytes, size_t size)
{
	if(fail_write) return ESP_FAIL;

	const auto *begin = static_cast<const unsigned char *>(bytes);
	blobs[key] = std::vector<unsigned char>(begin, begin + size);
	return ESP_OK;
}

esp_err_t nvs_commit(nvs_handle_t)
{
	return fail_commit ? ESP_FAIL : ESP_OK;
}

int main()
{
	std::string error;
	Config config;
	config.profiles = {{1, "home", "password", 7, true}};
	NvsProfileStore store;
	Config loaded;
	assert(store.load(loaded, error) == LoadResult::missing);
	assert(store.save(config, error));
	const auto first = blobs.at("cfg0");
	assert(first.size() < 2048);
	config.startup_enabled = false;
	assert(store.save(config, error));
	assert(blobs.at("cfg0") == first);
	NvsProfileStore reboot;
	assert(reboot.load(loaded, error) == LoadResult::loaded);
	assert(!loaded.startup_enabled);
	assert(loaded.profiles[0].ssid == "home");
	assert(loaded.profiles[0].password == "password");
	// Newest slot corruption must fall back to the previous valid generation.
	blobs["cfg1"].back() ^= 1;
	NvsProfileStore fallback;
	assert(fallback.load(loaded, error) == LoadResult::loaded);
	assert(loaded.startup_enabled);
	blobs["cfg0"].back() ^= 1;
	const auto before = blobs;
	NvsProfileStore broken;
	assert(broken.load(loaded, error) == LoadResult::error);
	assert(!broken.save(config, error));
	assert(blobs == before);
	// Empty saved configuration survives reboot and is a loaded generation.
	blobs.clear();
	exists = false;
	NvsProfileStore empty;
	assert(empty.load(loaded, error) == LoadResult::missing);
	assert(empty.save(Config{}, error));
	NvsProfileStore empty_reboot;
	assert(empty_reboot.load(loaded, error) == LoadResult::loaded);
	assert(loaded.profiles.empty());
	fail_write = true;
	assert(!empty_reboot.save(config, error));
	assert(blobs.size() == 1);
	fail_write = false;
	const auto valid_empty = blobs.at("cfg0");
	fail_commit = true;
	assert(!empty_reboot.save(config, error));
	assert(blobs.at("cfg0") == valid_empty);
	fail_commit = false;
	corrupt_readback = true;
	assert(!empty_reboot.save(config, error));
	assert(blobs.at("cfg0") == valid_empty);
	corrupt_readback = false;
	return 0;
}
