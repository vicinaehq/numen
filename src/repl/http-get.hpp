#pragma once
#include <expected>
#include <string>

// blocking GET of origin+path ("https://host", "/path"); body on 2xx, error message otherwise
std::expected<std::string, std::string> httpGet(const std::string &origin, const std::string &path);
