/*
 * Copyright (c) 2016 Ember
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#pragma once

#include "ClientContext.h"
#include "../Events.h"
#include <memory>

namespace ember { namespace authentication {

void auth_success(ClientContext* ctx, const protocol::CMSG_AUTH_SESSION& packet);

void enter(ClientContext* ctx);
void handle_packet(ClientContext* ctx);
void handle_event(ClientContext* ctx, const Event* event);
void exit(ClientContext* ctx);

}} // authentication, ember