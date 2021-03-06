/*
 * Copyright (c) 2015 Ember
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#pragma once

#include <shared/Realm.h>
#include <spark/Service.h>
#include <spark/ServiceDiscovery.h>
#include <spark/temp/RealmStatus_generated.h>
#include <spark/temp/MessageRoot_generated.h>
#include <logger/Logging.h>
#include <memory>

namespace ember {

class RealmService final : public spark::EventHandler {
	Realm realm_;
	spark::Service& spark_;
	spark::ServiceDiscovery& discovery_;
	log::Logger* logger_;
	
	std::unique_ptr<flatbuffers::FlatBufferBuilder> build_realm_status(const messaging::MessageRoot* root = nullptr) const;
	void broadcast_realm_status() const;
	void send_realm_status(const spark::Link& link, const messaging::MessageRoot* root) const;

public:
	RealmService(Realm realm, spark::Service& spark, spark::ServiceDiscovery& discovery, log::Logger* logger);
	~RealmService();

	void handle_message(const spark::Link& link, const messaging::MessageRoot* root) override;
	void handle_link_event(const spark::Link& link, spark::LinkState event) override;

	void set_realm_online();
	void set_realm_offline();
};

} // ember