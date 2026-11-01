// SPDX-FileCopyrightText: 2026 Oleg Kozhevnikov
// SPDX-License-Identifier: MIT
#include <NaWFstore/Console.hpp>
#if NAWFSTORE_WITH_CONSOLE
#include <algorithm>
#include <sstream>
#include <utility>
namespace NaWFstore
{
	Console::Console(Handler handler, Output output)
	  : handler_(std::move(handler)), output_(std::move(output))
	{
	}

	void Console::feed(char character)
	{
		if(character == '\n' && last_cr_) { last_cr_ = false; return; }

		last_cr_ = character == '\r';
		if(last_cr_) character = '\n';

		if(character == '\b' || character == 127)
		{
			if(!line_.empty()) line_.pop_back();

			return;
		}

		if(character != '\n')
		{
			if(line_.size() < 256) line_ += character;
			else overflow_ = true;

			return;
		}

		if(overflow_)
		{
			std::fill(line_.begin(), line_.end(), '\0');
			line_.clear(); stage_ = 0; overflow_ = false;
			output_("Input too long\n");
			return;
		}

		if(stage_ == 1)
		{
			ssid_ = line_; line_.clear(); stage_ = 2;
			output_("Password (input hidden, empty = open network):\n");
			return;
		}

		if(stage_ == 2)
		{
			std::vector<std::string> arguments{"put", edit_id_, ssid_, line_};
			output_(handler_(arguments) + "\n");
			std::fill(line_.begin(), line_.end(), '\0'); line_.clear();
			std::fill(arguments.back().begin(), arguments.back().end(), '\0');
			ssid_.clear(); stage_ = 0;
			return;
		}

		std::istringstream input(line_);
		std::vector<std::string> arguments;
		std::string token;
		while(input >> token)
			arguments.push_back(token);
		line_.clear();
		if(arguments.empty() || arguments[0] != "wifi") return;

		arguments.erase(arguments.begin());
		if(!arguments.empty() && (arguments[0] == "add" || arguments[0] == "edit"))
		{
			edit_id_ = arguments[0] == "edit" && arguments.size() > 1 ? arguments[1] : "0";
			stage_ = 1; output_("SSID:\n");
			return;
		}

		output_(handler_(arguments) + "\n");
	}

}
#endif
