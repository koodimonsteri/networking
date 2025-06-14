#include "HTTPParser.hpp"


HTTPRequest parseHTTPRequest(const std::string& rawRequest){
    HTTPRequest req;

    std::istringstream stream(rawRequest);
    std::string line;

    // HTTP request line
    if (std::getline(stream, line)) {
        std::istringstream lineStream(line);
        lineStream >> req.method >> req.path >> req.version;
        logf("[Parser] HTTP request line: ", req.method, ", ", req.path, ", ", req.version);
    }

    // Optional headers
    while(std::getline(stream, line) && line != "\r") {
        auto colon = line.find(':');
        if (colon != std::string::npos) {
            std::string key = line.substr(0, colon);
            std::string value = line.substr(colon + 1);

            key.erase(key.find_last_not_of(" \r\n") + 1);
            value.erase(0, value.find_first_not_of(" "));
            value.erase(value.find_last_not_of(" \r\n") + 1);
            req.headers.emplace_back(key, value);
        }
    }
    std::stringstream ss;
    for (const auto& [key, value] : req.headers) {
        ss << key << ": " << value << "\n";
    }
    //logf("[Parser] Headers:\n", ss.str());

    // Body
    std::getline(stream, req.body, '\0');
    //logf("[Parser] Body: ", req.body);

    return req;
}


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