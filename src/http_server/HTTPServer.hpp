#pragma once

#include <atomic>
#include <stdexcept>
#include <winsock2.h>


struct WinSockGuard {

    WinSockGuard() { 
        if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
            throw std::runtime_error("WSAStartup failed");
        }
    }
    ~WinSockGuard() { WSACleanup(); }
    WSAData wsaData;

};


class HTTPServer {

    public:
        HTTPServer(const std::string& serverAddress, const int serverPort);
        ~HTTPServer();

        void run();
    private:

        SOCKET acceptClient(SOCKET listenSocket);
        void handleClient(SOCKET clientSocket);
        SOCKET createListenSocket();

        std::string address_;
        int port_;
        SOCKET listenSocket_;

        std::atomic<bool> running = false;
        WinSockGuard winSockGuard;
};