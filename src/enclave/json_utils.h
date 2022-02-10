#pragma once

#ifdef ENCLAVED
#include <libc_mock/libcpp_mock.h>
#include <json/json.hpp>
#else
#include <json/json-17.hpp>
#endif
using json = nlohmann::json;

json parse_json(const std::string &input);
std::vector<double> get_double_list(const json &j);

