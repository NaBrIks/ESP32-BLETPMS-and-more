// SPDX-FileCopyrightText: 2026 Oleg Kozhevnikov
// SPDX-License-Identifier: MIT
#pragma once
#include "NaWFstore.hpp"
#if NAWFSTORE_WITH_NVS
namespace NaWFstore
{
	class NvsProfileStore final : public IProfileStore
	{
	public:
		explicit NvsProfileStore(const char *name_space = "nawfstore");
		LoadResult load(Config &config, std::string &error) override;
		bool save(const Config &config, std::string &error) override;
	private:
		std::string namespace_;
		uint64_t generation_{};
		int active_slot_{-1};
		bool loaded_{};
	};
}
#endif
