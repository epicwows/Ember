/*
 * Copyright (c) 2014 Ember
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include "Parser.h"
#include "Validator.h"
#include <rapidxml_utils.hpp>

namespace rxml = rapidxml;

namespace ember { namespace dbc {

types::Key Parser::parse_field_key(rxml::xml_node<>* property) {
	types::Key key;
	auto attr = property->first_attribute("ignore-type-mismatch");

	if(attr) {
		if(strcmp(attr->value(), "true") == 0 || strcmp(attr->value(), "1") == 0) {
			key.ignore_type_mismatch = true;
		} else {
			throw exception(std::string(attr->value() + std::string(" is not a valid attribute"
			                " value for ignore-type-mismatch")));
		}
	}

	for(rxml::xml_node<>* node = property->first_node(); node != 0; node = node->next_sibling()) {
		if(strcmp(node->name(), "type") == 0) {
			key.type = node->value();
		} else if(strcmp(node->name(), "parent") == 0) {
			key.parent = node->value();
		} else {
			throw exception(std::string("Unexpected element in <key>: ") + node->name());
		}
	}

	return key;
}

void Parser::parse_enum_options(std::vector<std::pair<std::string, std::string>>& key,
                                rxml::xml_node<>* property) {
	for(rxml::xml_node<>* node = property->first_node(); node; node = node->next_sibling()) {
		std::pair<std::string, std::string> kv;

		if(strcmp(node->name(), "option") != 0) {
			throw exception(std::string("Unexpected node in <options>: ") + node->name());
		}

		for(rxml::xml_attribute<>* attr = node->first_attribute(); attr; attr = attr->next_attribute()) {
			if(strcmp(attr->name(), "name") == 0) {
				kv.first = attr->value();
			} else if(strcmp(attr->name(), "value") == 0) {
				kv.second = attr->value();
			} else {
				throw exception(std::string("Unexpected attribute in <enum>: ") + attr->name());
			}
		}

		key.push_back(kv);
	}
}

void Parser::parse_enum_node(types::Enum& type, UniqueCheck& check, rxml::xml_node<>* node) {
	if(strcmp(node->name(), "name") == 0) {
		assign_unique(type.name, check.name, node);
		return;
	} else if(strcmp(node->name(), "type") == 0) {
		assign_unique(type.underlying_type, check.type, node);
		return;
	} else if(strcmp(node->name(), "alias") == 0) {
		assign_unique(type.alias, check.alias, node);
		return;
	} else if(strcmp(node->name(), "options") == 0) {
		if(check.options) {
			throw exception("Multiple definitions of <options> not allowed");
		}

		parse_enum_options(type.options, node);
		check.options = true;
		return;
	}

	throw exception(std::string("Unexpected node in <enum>: ") + node->name());
}

void Parser::parse_struct_node(types::Struct& type, UniqueCheck& check, rxml::xml_node<>* node) {
	if(strcmp(node->name(), "name") == 0) {
		assign_unique(type.name, check.name, node);
		return;
	} else if(strcmp(node->name(), "alias") == 0) {
		assign_unique(type.alias, check.alias, node);
		return;
	} else if(strcmp(node->name(), "field") == 0) {
		type.fields.emplace_back(parse_field(node));
		return;
	}

	throw exception(std::string("Unexpected node in <struct>: ") + node->name());
}

void Parser::assign_unique(std::string& type, bool& exists, rxml::xml_node<>* node) {
	if(exists) {
		throw exception(std::string("Multiple definitions of: ") + node->name());
	}

	type = node->value();
	exists = true;
}

void Parser::parse_field_node(types::Field& field, UniqueCheck& check, 
                              rxml::xml_node<>* node) {
	if(strcmp(node->name(), "name") == 0) {
		assign_unique(field.name, check.name, node);
		return;
	} else if(strcmp(node->name(), "type") == 0) {
		assign_unique(field.underlying_type, check.type, node);
		return;
	} else if(strcmp(node->name(), "key") == 0) {
		field.keys.emplace_back(parse_field_key(node));
		return;
	}

	throw exception(std::string("Unknown node found in <field>: ") + node->name());
}

types::Field Parser::parse_field(rxml::xml_node<>* root) {
	types::Field field;
	UniqueCheck check{};

	auto attr = root->first_attribute("comment");

	if(attr) {
		field.comment = attr->value();
	}

	for(rxml::xml_node<>* node = root->first_node(); node; node = node->next_sibling()) {
		parse_field_node(field, check, node);
	}

	if(!check.type || !check.name) {
		throw exception("A <field> must have at least <name> and <type> nodes");
	}

	return field;
}

types::Enum Parser::parse_enum(rxml::xml_node<>* root) {
	types::Enum parsed;
	UniqueCheck check{};

	auto attr = root->first_attribute("comment");

	if(attr) {
		parsed.comment = attr->value();
	}

	for(rxml::xml_node<>* node = root; node; node = node->next_sibling()) {
		parse_enum_node(parsed, check, node);
	}

	if(!check.type || !check.name) {
		throw exception("An <enum> must have at least <name> and <type> nodes");
	}

	return parsed;
}

types::Struct Parser::parse_struct(rxml::xml_node<>* root, int depth) {
	if(depth > MAX_PARSE_DEPTH) {
		throw exception("Struct nesting is too deep");
	}

	types::Struct parsed;
	UniqueCheck check{};

	auto attr = root->first_attribute("comment");

	if(attr) {
		parsed.comment = attr->value();
	}

	for(rxml::xml_node<>* node = root; node; node = node->next_sibling()) {
		if(strcmp(node->name(), "struct") == 0) {
			parsed.children.emplace_back(
				std::make_unique<types::Struct>(parse_struct(node->first_node(), depth + 1))
			);
		} else if(strcmp(node->name(), "enum") == 0) {
			parsed.children.emplace_back(
				std::make_unique<types::Enum>(parse_enum(node->first_node()))
			);
		} else {
			parse_struct_node(parsed, check, node);
		}
	}

	if(!check.name) {
		throw exception("A <struct> must have at least a <name> node");
	}

	return parsed;
}

types::Definition Parser::parse_doc_root(rxml::xml_node<>* parent) {
	types::Definition definition;

	for(rxml::xml_node<>* node = parent; node; node = node->next_sibling()) {
		if(strcmp(node->name(), "struct") == 0) {
			definition.emplace_back(
				std::make_unique<types::Struct>(parse_struct(node->first_node()))
			);
		} else if(strcmp(node->name(), "enum") == 0) {
			definition.emplace_back(
				std::make_unique<types::Enum>(parse_enum(node->first_node()))
			);
		}
	}

	return definition;
}

types::Definition Parser::parse_file(const std::string& path) {
	rxml::file<> definition(path.c_str());
    rxml::xml_document<> doc;
    doc.parse<0>(definition.data());

	auto root = doc.first_node();

	if(!root) {
		throw exception("File appears to be empty");
	}

	return parse_doc_root(root);
}

types::Definition Parser::parse(const std::string& path) try {
	return parse_file(path);
} catch(std::exception& e) {
	throw parse_error(path, e.what());
} catch(...) {
	throw parse_error(path, "Unknown exception type");
}

std::vector<types::Definition> Parser::parse(const std::vector<std::string>& paths) {
	std::vector<types::Definition> defs;

	for(auto& path : paths) {
		try {
			defs.emplace_back(parse_file(path));
		} catch(std::exception& e) {
			throw parse_error(path, e.what());
		} catch(...) {
			throw parse_error(path, "Unknown exception type");
		}
	}

	Validator validator(defs);
	validator.validate();

	return defs;
}

}} //dbc, ember