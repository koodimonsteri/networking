#include <atomic>
#include <csignal>
#include <iostream>
#include <string>
#include <sstream>
#include <winsock2.h>
#include <thread>
#include "SafeQueue.hpp"

#pragma comment(lib, "ws2_32.lib")

constexpr int LISTEN_PORT = 8080;
const char* const LISTEN_ADDR = "127.0.0.1";
constexpr int BUFFER_SIZE = 1024;

constexpr int MAX_WORKER_THREADS = 2;

std::mutex logMutex;

template <typename... Args>
void logf(Args&&... args) {
    std::lock_guard<std::mutex> lock(logMutex);
    (std::cout << ... << std::forward<Args>(args)) << std::endl;
}

template <typename... Args>
void logcerr(Args&&... args) {
    std::lock_guard<std::mutex> lock(logMutex);
    (std::cerr << ... << std::forward<Args>(args)) << std::endl;
}


struct WinSockGuard {
    WinSockGuard() { WSAStartup(MAKEWORD(2, 2), &wsaData); }
    ~WinSockGuard() { WSACleanup(); }
    WSADATA wsaData;
};


struct SocketTask {
    SOCKET socket;
    sockaddr_in clientAddr;

    SocketTask() : socket(INVALID_SOCKET) {}
    SocketTask(SOCKET s, sockaddr_in addr) : socket(s), clientAddr(addr) {}
};

std::atomic_bool running = true;

void signalHandler(int signal) {
    logf("\nCaught signal ", signal, ", exiting..");
    running = false;
}


void workerThread(SafeQueue<SocketTask>& taskQueue) {
    std::thread::id tid = std::this_thread::get_id();

    std::ostringstream oss;
    oss << "[Thread " << std::this_thread::get_id() << "] ";
    std::string threadStr = oss.str();

    logf("Started worker ", threadStr);
    while (running) {
        auto optionalTask = taskQueue.pop();
        
        if (!optionalTask.has_value()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }
        SocketTask task = std::move(optionalTask.value());
        SOCKET clientSocket = task.socket;
        sockaddr_in clientAddr = task.clientAddr;

        char* clientIp = inet_ntoa(clientAddr.sin_addr);
        int clientPort = ntohs(clientAddr.sin_port);
        logf(threadStr, " Handling client ", clientIp, ":", clientPort);
        char buffer[BUFFER_SIZE];

        while(true) {
            int bytesReceived = recv(clientSocket, buffer, BUFFER_SIZE, 0);

            if (bytesReceived < 0) {
                logcerr(threadStr, " recv() failed with error: ", WSAGetLastError());
                break;
            }

            if (bytesReceived == 0) {
                logf(threadStr, " Client disconnected.");
                break;
            }

            std::string receivedData(buffer, bytesReceived);
            logf(threadStr, " Received ", bytesReceived, " bytes: \"", receivedData, "\"");

            int bytesSent = send(clientSocket, buffer, bytesReceived, 0);

            if (bytesSent < 0) {
                logcerr(threadStr, " send() failed with error", WSAGetLastError());
                break;
            }

        }
        closesocket(clientSocket);
    }

}


int main() {
    /*
    - Multithreaded echo server -
    Main thread initializes worker threads 
    Then accepts new connections and pushes those to task queue.

    Worker threads keep polling the task queue
    and handle new echo tasks when received.
    */
    logf("Running multithreaded echo server!");
    
    std::signal(SIGINT, signalHandler);
    WinSockGuard winSockGuard;

    SOCKET listenSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listenSocket == INVALID_SOCKET) {
        logcerr("Failed to create socket");
        return 1;
    }
    
    sockaddr_in serverAddr{};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(LISTEN_PORT);
    serverAddr.sin_addr.s_addr = inet_addr(LISTEN_ADDR);

    if (bind(listenSocket, (sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
        logcerr("bind() failed with error: ", WSAGetLastError());
        closesocket(listenSocket);
        return 1;
    }
    
    if (listen(listenSocket, SOMAXCONN) == SOCKET_ERROR) {
        logcerr("listen() failed with error: ", WSAGetLastError());
        closesocket(listenSocket);
        return 1;
    }
    logf("Multithreaded echo server listening on ", LISTEN_ADDR, ":", LISTEN_PORT);

    SafeQueue<SocketTask> echoQueue;
    std::vector<std::thread> workerThreads;

    for (int i = 0; i < MAX_WORKER_THREADS; i++) {
        workerThreads.emplace_back(workerThread, std::ref(echoQueue));
    }

    while (running) {

        sockaddr_in clientAddr;
        int clientAddrLen = sizeof(clientAddr);

        // Lets not block accept so we can exit gracefully
        u_long nonBlocking = 1;
        ioctlsocket(listenSocket, FIONBIO, &nonBlocking);

        SOCKET clientSocket = accept(listenSocket, (sockaddr*)&clientAddr, &clientAddrLen);
        if (clientSocket == INVALID_SOCKET) {
            int errorCode = WSAGetLastError();
            if (errorCode == WSAEWOULDBLOCK) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                continue;
            } else {
                logcerr("[Main] accept() failed with error: ", errorCode);
                break;
            }
        }
        
        char* clientIp = inet_ntoa(clientAddr.sin_addr);
        int clientPort = ntohs(clientAddr.sin_port);
        logf("New client connected from ", clientIp, ":", clientPort);
        
        u_long blockingMode = 0;
        ioctlsocket(clientSocket, FIONBIO, &blockingMode);
        echoQueue.push(SocketTask(clientSocket, clientAddr));
    }

    logf("Waiting for threads to finish...");
    for (auto& t : workerThreads) {
        t.join();
    }

    closesocket(listenSocket);
    logf("Multithreaded echo server shut down gracefully.");
    return 0;
}
