#pragma once

#include <memory>
#include "Router.hpp"

std::unique_ptr<Router> createCustomerRouter();
//std::unique_ptr<Router> createProductRouter();
//std::unique_ptr<Router> createAuthRouter();