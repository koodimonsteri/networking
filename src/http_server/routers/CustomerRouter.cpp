#include "Router.hpp"
#include "Routers.hpp"

static void registerGetCustomers(Router& router) {
    router.get_("/", [](const HTTPRequest& req, HTTPResponse& res) {
        res = makeHttpResponse(
            200,
            "OK",
            {{"Content-Type", "text/plain"}},
            "List of customers"
        );
    });
}


static void registerGetCustomerById(Router& router) {
    router.get_("/{id}", [](const HTTPRequest& req, HTTPResponse& res) {
        const std::string& customerId = req.pathParams.at("id");
        res = makeHttpResponse(
            200,
            "OK",
            {{"Content-Type", "text/plain"}},
            "Get customer by id: " + customerId
        );
    });
}


static void registerCreateCustomer(Router& router) {
    router.post_("", [](const HTTPRequest& req, HTTPResponse& res) {
        res = makeHttpResponse(
            201,
            "Created",
            {{"Content-Type", "text/plain"}},
            "Customer created"
        );
    });
}

static void registerPatchCustomer(Router& router) {
    router.patch_("/{id}", [](const HTTPRequest& req, HTTPResponse& res) {
        const std::string& customerId = req.pathParams.at("id");
        res = makeHttpResponse(
            200,
            "OK",
            {{"Content-Type", "text/plain"}},
            "Customer updated: " + customerId
        );
    });
}


static void registerDeleteCustomer(Router& router) {
    router.delete_("/{id}", [](const HTTPRequest& req, HTTPResponse& res) {
        const std::string& customerId = req.pathParams.at("id");
        res = makeHttpResponse(
            200,
            "OK",
            {{"Content-Type", "text/plain"}},
            "Customer deleted: " + customerId
        );
    });
}


std::unique_ptr<Router> createCustomerRouter() {
    auto router = std::make_unique<Router>("/customers");

    registerGetCustomers(*router);
    registerGetCustomerById(*router);
    registerCreateCustomer(*router);
    registerPatchCustomer(*router);
    registerDeleteCustomer(*router);
    
    return router;
}