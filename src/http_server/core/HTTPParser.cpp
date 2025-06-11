#include "HTTPParser.hpp"

HTTPResponse makeHttpResponse(
    int status,
    std::string_view reason,
    std::unordered_map<std::string,std::string> headers,
    std::string body) {
    return HTTPResponse{status, std::string(reason), std::move(headers), std::move(body)};
}

std::string serializeResponse(const HTTPResponse& res) {
    std::ostringstream ss;
    ss << "HTTP/1.1 " << res.statusCode << " " << res.reasonPhrase << "\r\n";
    for (const auto& [key, val] : res.headers) {
        ss << key << ": " << val << "\r\n";
    }
    ss << "Content-Length: " << res.body.size() << "\r\n";
    ss << "\r\n" << res.body;
    return ss.str();
}