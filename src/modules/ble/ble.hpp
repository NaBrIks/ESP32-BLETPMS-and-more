// SPDX-FileCopyrightText: 2026 Oleg Kozhevnikov
// SPDX-License-Identifier: MIT
#pragma once
#include "modules/application.hpp"

namespace application
{
	void start_bluetooth(State &state);
	void poll_bluetooth(State &state);
}
