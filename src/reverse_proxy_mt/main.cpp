#include <iostream>
#include <winsock2.h>
#include <csignal>
#include <atomic>
#include <string>
#include <sstream>
#include <mutex>
#include <thread>
#include "SafeQueue.hpp"

#pragma comment(lib, "ws2_32.lib")

constexpr const char* PROXY_ADDR = "127.0.0.1";
constexpr int PROXY_PORT = 9000;

constexpr const char* BACKEND_ADDR = "127.0.0.1";
constexpr int BACKEND_PORT = 8080;

constexpr int BUFFER_SIZE = 4096;

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

struct ProxyTask {
    SOCKET socket;
    sockaddr_in clientAddr;

    ProxyTask() : socket(INVALID_SOCKET) {}
    ProxyTask(SOCKET s, sockaddr_in addr) : socket(s), clientAddr(addr) {}
};


std::atomic_bool running = true;

void signalHandler(int signal) {
    logf("\nCaught signal ", signal, ", exiting..");
    running = false;
}

struct WinSockGuard {
    WinSockGuard() { WSAStartup(MAKEWORD(2, 2), &wsaData); }
    ~WinSockGuard() { WSACleanup(); }
    WSADATA wsaData;
};


bool relayData(SOCKET from, SOCKET to, const char* directionLabel) {
    char buffer[BUFFER_SIZE];
    int bytesReceived = recv(from, buffer, BUFFER_SIZE, 0);

    if (bytesReceived < 0) {
        logcerr("recv() from ", directionLabel, " failed with error: ", WSAGetLastError());
        return false;
    }

    if (bytesReceived == 0) {
        logf(directionLabel, " disconnected.");
        return false;
    }

    std::string receivedData(buffer, bytesReceived);
    logf("Received ", bytesReceived, " bytes from ", directionLabel, ": \"", receivedData, "\"");

    int bytesSent = send(to, buffer, bytesReceived, 0);
    if (bytesSent < 0) {
        logcerr("send() to ", directionLabel, " failed with error: ", WSAGetLastError());
        return false;
    }

    return true;
}


void workerThread(SafeQueue<ProxyTask>& proxyQueue) {
    std::ostringstream oss;
    oss << "[Thread " << std::this_thread::get_id() << "] ";
    std::string threadStr = oss.str();

    logf("Started proxy worker ", threadStr);

    while (running) {
        
        auto optionalTask = proxyQueue.pop();

        if (!optionalTask.has_value()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }
        ProxyTask proxyTask = std::move(optionalTask.value());
        SOCKET clientSocket = proxyTask.socket;
        sockaddr_in clientAddr = proxyTask.clientAddr;

        char* clientIp = inet_ntoa(clientAddr.sin_addr);
        int clientPort = ntohs(clientAddr.sin_port);
        logf(threadStr, " Handling client ", clientIp, ":", clientPort);
        
        SOCKET backendSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        sockaddr_in backendAddr{};
        backendAddr.sin_family = AF_INET;
        backendAddr.sin_port = htons(BACKEND_PORT);
        backendAddr.sin_addr.s_addr = inet_addr(BACKEND_ADDR);

        if (connect(backendSocket, (sockaddr*)&backendAddr, sizeof(backendAddr)) == SOCKET_ERROR) {
            logcerr("connect() to backend server failed with error: ", WSAGetLastError());
            closesocket(clientSocket);
            continue;
        }

        logf("Connected to backend!");

        while (true) {
            if(!relayData(clientSocket, backendSocket, "client")) break;
            if(!relayData(backendSocket, clientSocket, "backend")) break;
        }

        logf("Closing connections.");
        closesocket(clientSocket);
        closesocket(backendSocket);
    }

}


int main() {
    // Most of this is same as echo server, but, instead of just receiving and sending back,
    // we relay the data to the backend and back to client
    
    logf("Running multithreaded reverse proxy");

    std::signal(SIGINT, signalHandler);
    WinSockGuard winSockGuard;

    SOCKET listenSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listenSocket == INVALID_SOCKET) {
        logcerr("Failed to create socket");
        return 1;
    }
    
    sockaddr_in proxyAddr{};
    proxyAddr.sin_family = AF_INET;
    proxyAddr.sin_port = htons(PROXY_PORT);
    proxyAddr.sin_addr.s_addr = inet_addr(PROXY_ADDR);

    if (bind(listenSocket, (sockaddr*)&proxyAddr, sizeof(proxyAddr)) == SOCKET_ERROR) {
        logcerr("bind() failed with error: ", WSAGetLastError());
        closesocket(listenSocket);
        return 1;
    }
    
    if (listen(listenSocket, SOMAXCONN) == SOCKET_ERROR) {
        logcerr("listen() failed with error: ", WSAGetLastError());
        closesocket(listenSocket);
        return 1;
    }

    logf("Reverse proxy listening on ", PROXY_ADDR, ":", PROXY_PORT, ", forwarding to ", BACKEND_ADDR, ":", BACKEND_PORT);

    SafeQueue<ProxyTask> proxyQueue;
    std::vector<std::thread> workerThreads;

    for (int i = 0; i < MAX_WORKER_THREADS; i++) {
        workerThreads.emplace_back(workerThread, std::ref(proxyQueue));
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
        
        // ProxyTasks can block
        u_long blockingMode = 0;
        ioctlsocket(clientSocket, FIONBIO, &blockingMode);
        proxyQueue.push(ProxyTask(clientSocket, clientAddr));
        
    }

    logf("Waiting for threads to finish...");
    for (auto& t : workerThreads) {
        t.join();
    }

    closesocket(listenSocket);
    logf("Multithreaded reverse proxy shut down gracefully.");
    return 0;

}

