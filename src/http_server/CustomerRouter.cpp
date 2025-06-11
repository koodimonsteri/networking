#include "Router.hpp"
#include "Routers.hpp"

static void registerGetCustomers(Router& router) {
    router.get_("/customers", [](const HTTPRequest& req, HTTPResponse& res) {
        res = makeHttpResponse(
            200,
            "OK",
            {{"Content-Type", "text/plain"}},
            "List of customers"
        );
    });
}


static void registerCreateCustomer(Router& router) {
    router.post_("/customers", [](const HTTPRequest& req, HTTPResponse& res) {
        res = makeHttpResponse(
            201,
            "Created",
            {{"Content-Type", "text/plain"}},
            "Customer created"
        );
    });
}


std::unique_ptr<Router> createCustomerRouter() {
    auto router = std::make_unique<Router>();

    registerGetCustomers(*router);
    registerCreateCustomer(*router);

    return router;
}