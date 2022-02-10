#include <json_utils.h>
#ifdef NATIVE                                                                   
#include <iostream>                                                             
#endif

//------------------------------------------------------------------------------
json parse_json(const std::string &input) {
    json ret;
    try {
        ret = json::parse(input);
    } catch (const std::exception &e) {
        std::cerr << "Malformed json (" << e.what() << ")" << std::endl;
    }
    return ret;
}

//------------------------------------------------------------------------------
std::vector<double> get_double_list(const json &j) {
    std::vector<double> ret;
    for (auto it = j.begin(); it != j.end(); ++it) {
        ret.push_back(it->get<double>());
    }
    return ret;
}

//------------------------------------------------------------------------------

