#pragma once

#include <atomic>
#include <stdexcept>
#include <thread>
#include <vector>
#include <winsock2.h>
#include <windows.h>
#include <mswsock.h>


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


struct IOContext {
    SOCKET socket;
    OVERLAPPED overlapped;
    WSABUF wsaBuf;

    static constexpr int BUFFER_SIZE = 4096;
    char buffer[BUFFER_SIZE];

    std::vector<char> sendBuffer;
    size_t sendOffset = 0;

    IOType state = IOType::RECV;

    IOContext(IOType ioType) : state(ioType) {
        ZeroMemory(&overlapped, sizeof(overlapped));
        wsaBuf.buf = buffer;
        wsaBuf.len = BUFFER_SIZE;
        socket = INVALID_SOCKET;
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
        static void signalHandler(int signal);
    private:

        SOCKET createListenSocket();
        void initIOCP();
        void initExtensions();

        // IOCP stuff
        bool postAccept();
        bool postRecv(IOContext* context);
        void handleAccept(IOContext* context);
        void handleRecv(IOContext* context);
        void handleSend(IOContext* context);

        void workerThread();

        std::string address_;
        u_short port_;

        SOCKET listenSocket_;
        HANDLE iocpHandle_;
        LPFN_ACCEPTEX lpfnAcceptEx = nullptr;

        const int n_threads = 2; // maybe as args?
        std::vector<std::thread> workerThreads; 
        std::atomic<bool> running = false;
        std::atomic<bool> shutdownCalled = false;
        WinSockGuard winSockGuard;

        // Workaround for signal handler
        static HTTPServer* instance_;
};