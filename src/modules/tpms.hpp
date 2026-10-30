// SPDX-FileCopyrightText: 2026 Oleg Kozhevnikov
// SPDX-License-Identifier: MIT
#pragma once
#include "application.hpp"
#include "tpms_types.hpp"
namespace application
{
	void observe_tomtom(State &, const NaBTools::runtime::AdvertisementView &,
	    const NaBTools::runtime::TomTomPacket &);
	void poll_tpms_search(State &);
	void request_tpms_search(bool active);
	Value tpms_catalog(const State &);
} // namespace application
