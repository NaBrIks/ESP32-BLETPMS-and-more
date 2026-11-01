// SPDX-FileCopyrightText: 2026 Oleg Kozhevnikov
// SPDX-License-Identifier: MIT
#include "NaWFstore/Console.hpp"
#include <cassert>
#include <string>
#include <vector>
using namespace NaWFstore;

int main()
{
	std::string output;
	std::vector<std::vector<std::string> > calls;
	Console console([&](const std::vector<std::string> &args) {
	    calls.push_back(args);
	    return "ok";
		}, [&](const std::string &text) {
	    output += text;
		});
	auto feed = [&](const std::string &text) {
			for(char c : text)
				console.feed(c);
		};
	feed("wifi add\r\nHome network\r\nsecret123\r\n");
	assert(calls.size() == 1);
	assert(calls.back() == (std::vector<std::string>{"put", "0", "Home network", "secret123"}));
	assert(output.find("secret123") == std::string::npos);
	feed("wifi status\n");
	assert(calls.size() == 2);
	assert(calls.back() == (std::vector<std::string>{"status"}));
	feed("wifi add\nOpen network\n\n");
	assert(calls.size() == 3);
	assert(calls.back().back().empty());
	feed("wifi add\nLong input\n" + std::string(257, 'x') + "\n");
	assert(calls.size() == 3);
	assert(output.find("Input too long") != std::string::npos);
	feed("wifi status\r\n");
	assert(calls.size() == 4);
	feed("wifi edit 7\nSomewhere\npasswordX\b1\n");
	assert(calls.back() == (std::vector<std::string>{"put", "7", "Somewhere", "password1"}));
	return 0;
}
