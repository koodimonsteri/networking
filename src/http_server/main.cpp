#include "HTTPServer.hpp"
#include <string>


int main(int argc, char* argv[]) {

    std::string address = "127.0.0.1";
    u_short port = 8080;

    if (argc >= 2) address = argv[1];
    if (argc >= 3) {
        int portNum = std::stoi(argv[2]);
        if (portNum < 0 || portNum > 65535) {
            throw std::out_of_range("Port must be between 0 and 65535");
        }
        port = static_cast<u_short>(portNum);
    }

    HTTPServer server(address, port);
    server.run();

    return 0;
}