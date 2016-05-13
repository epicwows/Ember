/*
 * Copyright (c) 2016 Ember
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#pragma once

#include <shared/smartenum.hpp>

namespace ember {

smart_enum_class(ClientState, int,
	// valid states
	AUTHENTICATING,
	IN_QUEUE,
	CHARACTER_LIST,
	IN_WORLD,

	// error states
	UNEXPECTED_PACKET,
	REQUEST_CLOSE
)

} // ember