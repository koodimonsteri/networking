#pragma once

#include <string>
#include <unordered_map>
#include <functional>
#include <regex>

#include "HTTPParser.hpp"


class Router {
    public:
        Router();
        Router(const std::string& routePrefix);

        using RouteHandler = std::function<void(const HTTPRequest&, HTTPResponse&)>;
        
        void get_(const std::string& path, RouteHandler handler);
        void post_(const std::string& path, RouteHandler handler);
        void patch_(const std::string& path, RouteHandler handler);
        void put_(const std::string& path, RouteHandler handler);
        void delete_(const std::string& path, RouteHandler handler);

        bool handle(const HTTPRequest& request, HTTPResponse& response);

    private:

        struct RouteEntry {
            std::string path;
            std::regex pathRegex;
            std::vector<std::string> paramNames;
            RouteHandler handler;
        };

        void registerRoute(const std::string& method, const std::string& path, RouteHandler handler);
        
        std::string prefix;
        std::unordered_map<std::string, std::vector<RouteEntry>> routes;
};