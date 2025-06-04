#include "HTTPServer.hpp"
#include <string>


int main(int argc, char* argv[]) {

    std::string address = "127.0.0.1";
    int port = 8080;

    if (argc >= 2) address = argv[1];
    if (argc >= 3) port = std::stoi(argv[2]);

    HTTPServer server(address, port);
    server.run();

    return 0;
}