#pragma once

#include <atomic>
#include <stdexcept>
#include <thread>
#include <vector>
#include <winsock2.h>
#include <windows.h>
#include <mswsock.h>

#include "Router.hpp"


struct WinSockGuard {

    WinSockGuard() { 
        if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
            throw std::runtime_error("WSAStartup failed");
        }
    }
    ~WinSockGuard() { WSACleanup(); }
    WSAData wsaData;
};


enum class IOType {
    RECV,
    SEND,
    ACCEPT
};

struct Connection;

struct IOContext {
    OVERLAPPED overlapped;
    IOType state = IOType::RECV;
    Connection* connection = nullptr;

    IOContext(IOType ioType) : state(ioType) {
        ZeroMemory(&overlapped, sizeof(overlapped));
    }

    virtual ~IOContext() = default;
};


struct AcceptContext : public IOContext {
    SOCKET socket = INVALID_SOCKET;
    char acceptBuffer[(sizeof(sockaddr_in) + 16) * 2];

    AcceptContext() : IOContext(IOType::ACCEPT) {}
    ~AcceptContext() {
        if (socket != INVALID_SOCKET) {
            closesocket(socket);
        }
    }
};


struct Connection {
    SOCKET socket = INVALID_SOCKET;
    IOContext* recvContext = nullptr;
    IOContext* sendContext = nullptr;

    static constexpr int BUFFER_SIZE = 4096;
    std::vector<char> recvBuffer;
    std::vector<char> sendBuffer;
    size_t sendOffset = 0;

    Connection(SOCKET s) : socket(s), recvBuffer(BUFFER_SIZE), sendBuffer(BUFFER_SIZE) {
        recvContext = new IOContext(IOType::RECV);
        sendContext = new IOContext(IOType::SEND);
        recvContext->connection = this;
        sendContext->connection = this;
    }

    ~Connection() {
        if (socket != INVALID_SOCKET) {
            closesocket(socket);
        }
        if (recvContext != nullptr) {
            delete recvContext;
        }
        if (sendContext != nullptr) {
            delete sendContext;
        }
    }
};


/*
Fully asynchronous multithreaded HTTP server using IOCP
Previous async servers weren't fully async due to accept()
Here we use AcceptEx instead, making this fully async.



*/

class HTTPServer {

    public:
        HTTPServer(const std::string& serverAddress, const u_short serverPort);
        ~HTTPServer();

        void run();
        void shutdown();

        void includeRouter(std::unique_ptr<Router> router);

        static void signalHandler(int signal);

    private:
        SOCKET createListenSocket();
        
        // IOCP stuff
        void initIOCP();
        void initExtensions();
        bool postAccept(std::string threadStr);
        bool postRecv(Connection* conn, std::string threadStr);
        bool postSend(Connection* conn, std::string threadStr);
        void handleAccept(AcceptContext* context, std::string threadStr);
        void handleRecv(IOContext* context, DWORD bytesTransferred, std::string threadStr);
        void handleSend(IOContext* context, DWORD bytesTransferred, std::string threadStr);
        HANDLE iocpHandle_;
        LPFN_ACCEPTEX lpfnAcceptEx = nullptr;  // AcceptEx func reference

        void workerThread();
        void handleRequest(const HTTPRequest& req, HTTPResponse& res);

        std::string address_;
        u_short port_;
        SOCKET listenSocket_;

        std::vector<std::unique_ptr<Router>> routers;

        const int n_threads = 2; // maybe as args?
        std::vector<std::thread> workerThreads; 

        std::atomic<bool> running = false;
        std::atomic<bool> shutdownCalled = false;

        WinSockGuard winSockGuard;

        // Workaround for signal handler
        static HTTPServer* instance_;
};