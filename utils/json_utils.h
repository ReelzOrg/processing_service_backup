#pragma once
#include <json/json.h>
#include <sstream>
#include <string>

namespace utils {

inline Json::Value parseJsonPayload(const std::string& payload) {
    Json::Value root;
    Json::CharReaderBuilder builder;
    std::string errs;
    std::istringstream s(payload);

    if (!Json::parseFromStream(builder, s, &root, &errs)) {
        throw std::runtime_error("JSON parse error: " + errs);
    }

    return root;
}

}