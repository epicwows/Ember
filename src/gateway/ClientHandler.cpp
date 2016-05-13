/*
 * Copyright (c) 2016 Ember
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include "ClientHandler.h"
#include "ClientConnection.h"
#include <spark/SafeBinaryStream.h>
#include <spark/temp/Account_generated.h>
#include <botan/bigint.h>
#include <botan/sha160.h>

#include "temp.h" // todo

#undef ERROR // temp

namespace em = ember::messaging;

namespace ember {

void ClientHandler::handle_packet(protocol::ClientHeader header, spark::Buffer& buffer) {
	header_ = &header;

	// handle ping & keep-alive as special cases
	switch(header.opcode) {
		case protocol::ClientOpcodes::CMSG_PING:
			handle_ping(buffer);
			return;
		case protocol::ClientOpcodes::CMSG_KEEP_ALIVE: // no response required
			return;
	}

	switch(state_) {
		case ClientStates::AUTHENTICATING:
			handle_authentication(buffer);
			break;
		case ClientStates::CHARACTER_LIST:
			handle_character_list(buffer);
			break;
		case ClientStates::IN_WORLD:
			handle_in_world(buffer);
			break;
		default:
			// unexpected packet?
			break;
	}
}

bool ClientHandler::packet_deserialise(protocol::Packet& packet, spark::Buffer& buffer) {
	spark::SafeBinaryStream stream(buffer);

	if(packet.read_from_stream(stream) != protocol::Packet::State::DONE) {
		LOG_DEBUG_FILTER(logger_, LF_NETWORK)
			<< "Parsing of " << protocol::to_string(header_->opcode)
			<< " failed" << LOG_ASYNC;

		//if(close_on_failure) {
			connection_.close_session();
		//}
		
		return false;
	}

	return true;
}

void ClientHandler::handle_ping(spark::Buffer& buffer) {
	LOG_TRACE_FILTER(logger_, LF_NETWORK) << __func__ << LOG_ASYNC;

	protocol::CMSG_PING packet;

	if(!packet_deserialise(packet, buffer)) {
		return;
	}

	auto response = std::make_shared<protocol::SMSG_PONG>();
	connection_.latency(packet.latency);
	response->sequence_id = packet.sequence_id;
	connection_.send(protocol::ServerOpcodes::SMSG_PONG, response);
}

void ClientHandler::send_auth_challenge() {
	auto packet = std::make_shared<protocol::SMSG_AUTH_CHALLENGE>();
	packet->seed = auth_seed_ = 600; // todo, obviously

	connection_.send(protocol::ServerOpcodes::SMSG_AUTH_CHALLENGE, packet);
	state_ = ClientStates::AUTHENTICATING;
}

void ClientHandler::prove_session(Botan::BigInt key, const protocol::CMSG_AUTH_SESSION& packet) {
	Botan::SecureVector<Botan::byte> k_bytes = Botan::BigInt::encode(key);
	std::uint32_t unknown = 0;

	Botan::SHA_160 hasher;
	hasher.update(packet.username);
	hasher.update((Botan::byte*)&unknown, sizeof(unknown));
	hasher.update((Botan::byte*)&packet.seed, sizeof(packet.seed));
	hasher.update((Botan::byte*)&auth_seed_, sizeof(auth_seed_));
	hasher.update(k_bytes);
	Botan::SecureVector<Botan::byte> calc_hash = hasher.final();

	if(calc_hash != packet.digest) {
		send_auth_fail(protocol::ResultCode::AUTH_BAD_SERVER_PROOF);
		return;
	}

	connection_.set_authenticated(key);
	account_name_ = packet.username;

	auto auth_success = [this]() {
		state_ = ClientStates::CHARACTER_LIST;
		send_auth_success();
		// send_addon_data();
	};

	/* 
	 * Note: MaNGOS claims you need a full auth packet for the initial AUTH_WAIT_QUEUE
	 * but that doesn't seem to be true - if this bugs out, check that out
	 */
	if(false) {
		state_ = ClientStates::IN_QUEUE;

		queue_service_temp->enqueue(connection_.shared_from_this(), [this, auth_success, packet]() {
			LOG_DEBUG(logger_) << packet.username << " removed from queue" << LOG_ASYNC;
			auth_success();
		});

		LOG_DEBUG(logger_) << packet.username << " added to queue" << LOG_ASYNC;
		return;
	}

	auth_success();
}

void ClientHandler::send_auth_success() {
	auto response = std::make_shared<protocol::SMSG_AUTH_RESPONSE>();
	response->result = protocol::ResultCode::AUTH_OK;
	connection_.send(protocol::ServerOpcodes::SMSG_AUTH_RESPONSE, response);
}

void ClientHandler::send_auth_fail(protocol::ResultCode result) {
	LOG_TRACE_FILTER(logger_, LF_NETWORK) << __func__ << LOG_ASYNC;

	// not convinced that this packet is correct
	auto response = std::make_shared<protocol::SMSG_AUTH_RESPONSE>();
	response->result = result;
	connection_.send(protocol::ServerOpcodes::SMSG_AUTH_RESPONSE, response);
	connection_.close_session();
}

void ClientHandler::handle_authentication(spark::Buffer& buffer) {
	LOG_TRACE_FILTER(logger_, LF_NETWORK) << __func__ << LOG_ASYNC;

	// prevent the client from sending another authentication message
	// while we're waiting on a remote service to send the key
	state_ = ClientStates::AUTHENTICATING_REMOTE_WAIT;

	if(header_->opcode != protocol::ClientOpcodes::CMSG_AUTH_SESSION) {
		LOG_DEBUG_FILTER(logger_, LF_NETWORK)
			<< "Expected CMSG_AUTH_CHALLENGE, dropping "
			<< connection_.remote_address() << LOG_ASYNC;
		connection_.close_session();
		return;
	}

	protocol::CMSG_AUTH_SESSION packet;

	if(!packet_deserialise(packet, buffer)) {
		return;
	}

	// todo - check game build
	fetch_session_key(packet);
}

void ClientHandler::fetch_session_key(const protocol::CMSG_AUTH_SESSION& packet) {
	LOG_TRACE_FILTER(logger_, LF_NETWORK) << __func__ << LOG_ASYNC;
	LOG_DEBUG(logger_) << "Received session proof from " << packet.username << LOG_ASYNC;

	auto self = connection_.shared_from_this();

	acct_serv->locate_session(packet.username,
		[this, self, packet](em::account::Status remote_res, Botan::BigInt key) {
			connection_.socket().get_io_service().post([this, self, packet, remote_res, key]() {
				LOG_DEBUG_FILTER(logger_, LF_NETWORK)
					<< "Account server returned " << em::account::EnumNameStatus(remote_res)
					<< " for " << packet.username << LOG_ASYNC;

				if(remote_res != em::account::Status::OK) {
					protocol::ResultCode result;

					switch(remote_res) {
						case em::account::Status::ALREADY_LOGGED_IN:
							result = protocol::ResultCode::AUTH_ALREADY_ONLINE;
							break;
						case em::account::Status::SESSION_NOT_FOUND:
							result = protocol::ResultCode::AUTH_UNKNOWN_ACCOUNT;
							break;
						default:
							LOG_ERROR_FILTER(logger_, LF_NETWORK) << "Received "
								<< em::account::EnumNameStatus(remote_res)
								<< " from account server" << LOG_ASYNC;
							result = protocol::ResultCode::AUTH_SYSTEM_ERROR;
					}

					send_auth_fail(result);
				} else {
					prove_session(key, packet);
				}
			});
	});
}

void ClientHandler::send_character_list_fail() {
	LOG_TRACE_FILTER(logger_, LF_NETWORK) << __func__ << LOG_ASYNC;

	auto response = std::make_shared<protocol::SMSG_CHAR_CREATE>();
	response->result = protocol::ResultCode::AUTH_UNAVAILABLE;
	connection_.send(protocol::ServerOpcodes::SMSG_CHAR_CREATE, response);
}

void ClientHandler::send_character_list(std::vector<Character> characters) {
	LOG_TRACE_FILTER(logger_, LF_NETWORK) << __func__ << LOG_ASYNC;

	auto response = std::make_shared<protocol::SMSG_CHAR_ENUM>();
	response->characters = characters;
	connection_.send(protocol::ServerOpcodes::SMSG_CHAR_ENUM, response);
}

void ClientHandler::handle_char_enum(spark::Buffer& buffer) {
	LOG_TRACE_FILTER(logger_, LF_NETWORK) << __func__ << LOG_ASYNC;

	auto self = connection_.shared_from_this();

	char_serv_temp->retrieve_characters(account_name_,
		[this, self](em::character::Status status, std::vector<Character> characters) {
			if(status == em::character::Status::OK) {
				send_character_list(characters);
			} else {
				send_character_list_fail();
			}
	});
}

void ClientHandler::send_character_delete() {
	auto response = std::make_shared<protocol::SMSG_CHAR_CREATE>();
	response->result = protocol::ResultCode::CHAR_DELETE_SUCCESS;
	connection_.send(protocol::ServerOpcodes::SMSG_CHAR_DELETE, response);
}

void ClientHandler::send_character_create() {
	auto response = std::make_shared<protocol::SMSG_CHAR_CREATE>();
	response->result = protocol::ResultCode::CHAR_CREATE_SUCCESS;
	connection_.send(protocol::ServerOpcodes::SMSG_CHAR_CREATE, response);
}

void ClientHandler::handle_char_create(spark::Buffer& buffer) {
	LOG_TRACE_FILTER(logger_, LF_NETWORK) << __func__ << LOG_ASYNC;

	protocol::CMSG_CHAR_CREATE packet;

	if(!packet_deserialise(packet, buffer)) {
		return;
	}

	auto self = connection_.shared_from_this();

	char_serv_temp->create_character(account_name_, *packet.character,
		[this, self](em::character::Status status) {
			//if(status == em::character::Status::OK) {
				send_character_create();
			//}
	});
}

void ClientHandler::handle_char_delete(spark::Buffer& buffer) {
	LOG_TRACE_FILTER(logger_, LF_NETWORK) << __func__ << LOG_ASYNC;

	protocol::CMSG_CHAR_DELETE packet;

	if(!packet_deserialise(packet, buffer)) {
		return;
	}

	auto self = connection_.shared_from_this();

	char_serv_temp->delete_character(account_name_, packet.id,
		[this, self](em::character::Status status) {
			if(status == em::character::Status::OK) {
				send_character_delete();
			}
		}
	);
}

void ClientHandler::handle_login(spark::Buffer& buffer) {
	LOG_TRACE_FILTER(logger_, LF_NETWORK) << __func__ << LOG_ASYNC;

	protocol::CMSG_PLAYER_LOGIN packet;

	if(!packet_deserialise(packet, buffer)) {
		return;
	}

	/*auto response = std::make_shared<protocol::SMSG_CHARACTER_LOGIN_FAILED>();
	response->reason = 1;
	connection_.send(protocol::ServerOpcodes::SMSG_CHARACTER_LOGIN_FAILED, response);*/
}

void ClientHandler::handle_character_list(spark::Buffer& buffer) {
	switch(header_->opcode) {
		case protocol::ClientOpcodes::CMSG_CHAR_ENUM:
			handle_char_enum(buffer);
			break;
		case protocol::ClientOpcodes::CMSG_CHAR_CREATE:
			handle_char_create(buffer);
			break;
		case protocol::ClientOpcodes::CMSG_CHAR_DELETE:
			handle_char_delete(buffer);
			break;
		case protocol::ClientOpcodes::CMSG_PLAYER_LOGIN:
			handle_login(buffer);
			break;
		/*case protocol::ClientOpcodes::CMSG_CHAR_RENAME:
			handle_char_rename(buffer);
			break;*/
		// case enter world
	}
}

void ClientHandler::handle_in_world(spark::Buffer& buffer) {
	LOG_TRACE_FILTER(logger_, LF_NETWORK) << __func__ << LOG_ASYNC;

}

void ClientHandler::start() {
	send_auth_challenge();
}

ClientHandler::~ClientHandler() {
	switch(state_) {
		case ClientStates::CHARACTER_LIST:
		case ClientStates::IN_WORLD:
			queue_service_temp->decrement();
			break;
		case ClientStates::IN_QUEUE:
			queue_service_temp->dequeue(connection_.shared_from_this());
			break;
	}
}

} // ember