#pragma once

#include <string>
#include <unordered_map>
#include <functional>

#include "HTTPParser.hpp"


class Router {

    public:
        using RouteHandler = std::function<void(const HTTPRequest&, HTTPResponse&)>;

        virtual ~Router() = default;

        void get_(const std::string& path, RouteHandler handler) {
            registerRoute("GET", path, handler);
        }
        void post_(const std::string& path, RouteHandler handler) {
            registerRoute("POST", path, handler);
        }
        void patch_(const std::string& path, RouteHandler handler) {
            registerRoute("PATCH", path, handler);
        }
        void put_(const std::string& path, RouteHandler handler) {
            registerRoute("PUT", path, handler);
        }
        void delete_(const std::string& path, RouteHandler handler) {
            registerRoute("DELETE", path, handler);
        }

        bool handle(const HTTPRequest& request, HTTPResponse& response) {
            const std::string key = routeKey(request.method, request.path);
            auto it = routes.find(key);
            if (it != routes.end()) {
                it->second(request, response);
                return true;
            }
            return false;
        }

    private:

        void registerRoute(const std::string& method, const std::string& path, RouteHandler handler) {
            routes[routeKey(method, path)] = handler;
        }
        std::string routeKey(const std::string& method, const std::string& path) const {
            return method + ":" + path;
        }

        std::unordered_map<std::string, RouteHandler> routes;
};