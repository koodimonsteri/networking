#include "HTTPServer.hpp"
#include "log.hpp"


HTTPServer::HTTPServer(const std::string& serverAddress, const int serverPort) {
    logf("Creating HTTPServer on: ", serverAddress, ":", serverPort);
    address_ = serverAddress;
    port_ = serverPort;
}


HTTPServer::~HTTPServer() {

}


void HTTPServer::run() {

}


SOCKET HTTPServer::acceptClient(SOCKET listenSocket) {
    return INVALID_SOCKET;
}


void HTTPServer::handleClient(SOCKET clientSocket) {

}


SOCKET HTTPServer::createListenSocket() {
    return INVALID_SOCKET;
}

