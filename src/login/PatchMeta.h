/*
 * Copyright (c) 2015, 2016 Ember
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#pragma once

#include <array>
#include <string>
#include <sstream>
#include <cstdint>

namespace ember {

struct FileMeta {
	std::string name;
	std::array<std::uint8_t, 16> md5;
	std::uint64_t size;
};

struct PatchMeta {
	std::uint32_t id;
	std::uint16_t build_from;
	std::uint16_t build_to;
	enum class Type {
		ROLLUP, PARTIAL
	} type;
	FileMeta file_meta;
};

} // ember