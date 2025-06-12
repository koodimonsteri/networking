#pragma once

#include <string>
#include <vector>
#include <sstream>
#include <ostream>
#include <unordered_map>
#include "log.hpp"


struct HTTPRequest {

    std::string method;
    std::string path;
    std::string version;
    std::vector<std::pair<std::string, std::string>> headers;
    std::string body;
    std::unordered_map<std::string, std::string> pathParams;
};

struct HTTPResponse {
    int statusCode;
    std::string reasonPhrase;
    std::unordered_map<std::string, std::string> headers;
    std::string body;
};

HTTPRequest parseHTTPRequest(const std::string& rawRequest);

HTTPResponse makeHttpResponse(
    int status,
    std::string_view reason,
    std::unordered_map<std::string,std::string> headers,
    std::string body
);

std::string serializeResponse(const HTTPResponse& res);