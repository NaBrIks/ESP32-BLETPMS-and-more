// SPDX-FileCopyrightText: 2026 Oleg Kozhevnikov
// SPDX-License-Identifier: MIT
#pragma once
#include "NaWFstore.hpp"
#include <functional>
#if NAWFSTORE_WITH_CONSOLE
namespace NaWFstore
{
	class Console
	{
	public:
		using Handler = std::function<std::string (const std::vector<std::string> &)>;
		using Output = std::function<void (const std::string &)>;
		Console(Handler handler, Output output);
		void feed(char character);
	private:
		Handler handler_;
		Output output_;
		std::string line_, ssid_, edit_id_;
		unsigned stage_{};
		bool overflow_{};
		bool last_cr_{};
	};
}
#endif
