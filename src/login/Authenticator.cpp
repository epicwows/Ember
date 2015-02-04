/*
 * Copyright (c) 2015 Ember
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include "Authenticator.h"
#include <srp6/Server.h>
#include <srp6/Client.h>
#include <shared/database/daos/UserDAO.h>
#include <algorithm>

namespace ember {

auto Authenticator::verify_client_version(const GameVersion& version) -> PATCH_STATE {
	if(std::find(versions_.begin(), versions_.end(), version) != versions_.end()) {
		return PATCH_STATE::OK;
	}

	//Figure out whether any of the allowed client versions are newer than the client.
	//If so, there's a chance that it can be patched.
	for(auto v : versions_) {
		if(v > version) {
			return PATCH_STATE::TOO_OLD;
		}
	}

	return PATCH_STATE::TOO_NEW;
}

auto Authenticator::check_account(const std::string& username) -> ACCOUNT_STATUS try {
	user_ = users_.user(username);

	if(!user_) {
		return ACCOUNT_STATUS::NOT_FOUND;
	}

	auth_ = std::make_unique<srp6::Server>(gen_, user_->verifier());

	return ACCOUNT_STATUS::OK;
} catch(std::exception& e) {
	std::cout << e.what() << std::endl; //<------------------------------------------------------------------------------------------------------------------------------------------------------ LOGGER
	return ACCOUNT_STATUS::DAL_ERROR;
}

auto Authenticator::challenge_reply() -> ChallengeResponse {
	return {auth_->public_ephemeral(), Botan::BigInt(user_->salt()), gen_};
}

auto Authenticator::proof_check(protocol::ClientLoginProof* proof) -> LoginResult {
	//Usernames aren't required to be uppercase in the DB but the client requires it for calculations
	std::string user_upper(user_->username());
	std::transform(user_upper.begin(), user_upper.end(), user_upper.begin(), ::toupper);

	//Need to reverse the order thanks to Botan's big-endian buffers
	std::reverse(std::begin(proof->A), std::end(proof->A));
	std::reverse(std::begin(proof->M1), std::end(proof->M1));

	Botan::BigInt A(Botan::BigInt::decode(proof->A, sizeof(proof->A)));
	Botan::BigInt M1(Botan::BigInt::decode(proof->M1, sizeof(proof->M1)));
	srp6::SessionKey key(auth_->session_key(A, true, srp6::COMPLIANCE::GAME));

	Botan::BigInt B = auth_->public_ephemeral();
	Botan::BigInt M1_S = srp6::generate_client_proof(user_upper, key, gen_.prime(), gen_.generator(),
	                                                 A, B, Botan::BigInt(user_->salt()));

	auto res = protocol::RESULT::FAIL_INCORRECT_PASSWORD;
	
	if(M1 == M1_S) {
		if(user_->banned()) {
			res = protocol::RESULT::FAIL_BANNED;
		} else if(user_->suspended()) {
			res = protocol::RESULT::FAIL_SUSPENDED;
		/*} else if(time) {
			res = protocol::RESULT::FAIL_NO_TIME;*/
		/*} else if(parental_controls) {
			res = protocol::RESULT::FAIL_PARENTAL_CONTROLS;*/
		} else {
			res = protocol::RESULT::SUCCESS;
		}
	}

	return {res, auth_->generate_proof(key, M1)};
}

void Authenticator::set_logged_in(const std::string& ip) {
	users_.record_last_login(*user_, ip);
}

} //ember