#include <sstream>

#include "Router.hpp"
#include "log.hpp"


Router::Router() {
    prefix = "";
}

Router::Router(const std::string& routePrefix) {
    prefix = routePrefix;
}


void Router::get_(const std::string& path, RouteHandler handler) {
    registerRoute("GET", path, handler);
}

void Router::post_(const std::string& path, RouteHandler handler) {
    registerRoute("POST", path, handler);
}

void Router::patch_(const std::string& path, RouteHandler handler) {
    registerRoute("PATCH", path, handler);
}

void Router::put_(const std::string& path, RouteHandler handler) {
    registerRoute("PUT", path, handler);
}

void Router::delete_(const std::string& path, RouteHandler handler) {
    registerRoute("DELETE", path, handler);
}

bool Router::handle(const HTTPRequest& request, HTTPResponse& response) {
    auto it = routes.find(request.method);
    if (it == routes.end()) return false;
    
    for (const auto& routeEntry : it->second) {
        std::smatch match;
        HTTPRequest requestWithParams = request;
        if (std::regex_match(request.path, match, routeEntry.pathRegex)) {
            for (size_t i = 0; i < routeEntry.paramNames.size(); ++i) {
                requestWithParams.pathParams[routeEntry.paramNames[i]] = match[i + 1];
            }
            routeEntry.handler(requestWithParams, response);
            return true;
        }
    }
    return false;
}

void Router::registerRoute(const std::string& method, const std::string& path, RouteHandler handler) {
    /*
    Static routes were fine with just map<path, func>
    Path params require regex matching
    We are going for fastapi style path params using curly braces: e.g., /customers/{id}
    TODO param typing + validation + exceptions
    */
    std::string pathRegex = "^";
    std::vector<std::string> paramNames;
    
    std::string fullPath = prefix + path;
    if (!fullPath.empty() && fullPath[0] == '/') {
        fullPath.erase(0, 1);
    }

    std::stringstream ss(fullPath);
    std::string segment;
    while(std::getline(ss, segment, '/')) {
        if (segment.empty()) {
            if (ss.peek() == EOF) {
                continue; // allow trailing slashes
            }
            throw std::invalid_argument("Route path contains empty segment: '" + fullPath + "'");
        }
        if (segment.front() == '{' && segment.back() == '}') {
            std::string paramName = segment.substr(1, segment.length() - 2);
            if (paramName.length() == 0) {
                throw std::invalid_argument("Route path contains empty path param: '" + fullPath + "'");
            }
            paramNames.push_back(paramName);
            pathRegex += "/([^/]+)";
        } else {
            pathRegex += "/" + segment;
        }
    }
    logf("[Router] Created ", method," path regex: ", pathRegex);
    std::regex pattern(pathRegex);
    routes[method].push_back(RouteEntry{fullPath, pattern, paramNames, handler});
}

