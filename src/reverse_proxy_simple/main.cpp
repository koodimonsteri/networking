#include <iostream>
#include <winsock2.h>
#include <csignal>
#include <atomic>
#include <string>

constexpr const char* PROXY_ADDR = "127.0.0.1";
constexpr int PROXY_PORT = 9000;

constexpr const char* BACKEND_ADDR = "127.0.0.1";
constexpr int BACKEND_PORT = 8080;

constexpr int BUFFER_SIZE = 4096;

std::atomic_bool running = true;

void signalHandler(int signal) {
    std::cout << "\nCaught signal " << signal << ", exiting..\n";
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
        std::cerr << "recv() from " << directionLabel << " failed with error: " << WSAGetLastError() << "\n";
        return false;
    }

    if (bytesReceived == 0) {
        std::cout << directionLabel << " disconnected.\n";
        return false;
    }

    std::string receivedData(buffer, bytesReceived);
    std::cout << "Received " << bytesReceived << " bytes from " << directionLabel << ": \"" << receivedData << "\"\n";

    int bytesSent = send(to, buffer, bytesReceived, 0);
    if (bytesSent < 0) {
        std::cerr << "send() to " << directionLabel << " failed with error: " << WSAGetLastError() << "\n";
        return false;
    }

    return true;
}


int main() {
    // Most of this is same as echo server, but, instead of just receiving and sending back,
    // we relay the data to the backend and back to client
    
    std::cout << "Running reverse proxy\n";

    std::signal(SIGINT, signalHandler);
    WinSockGuard winSockGuard;

    SOCKET listenSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listenSocket == INVALID_SOCKET) {
        std::cerr << "Failed to create socket\n";
        return 1;
    }
    
    sockaddr_in proxyAddr{};
    proxyAddr.sin_family = AF_INET;
    proxyAddr.sin_port = htons(PROXY_PORT);
    proxyAddr.sin_addr.s_addr = inet_addr(PROXY_ADDR);

    if (bind(listenSocket, (sockaddr*)&proxyAddr, sizeof(proxyAddr)) == SOCKET_ERROR) {
        std::cerr << "bind() failed with error: " << WSAGetLastError() << "\n";
        closesocket(listenSocket);
        return 1;
    }
    
    if (listen(listenSocket, SOMAXCONN) == SOCKET_ERROR) {
        std::cerr << "listen() failed with error: " << WSAGetLastError() << "\n";
        closesocket(listenSocket);
        return 1;
    }

    std::cout << "Reverse proxy listening on " << PROXY_ADDR << ":" << PROXY_PORT
              << ", forwarding to " << BACKEND_ADDR << ":" << BACKEND_PORT << "\n";


    while (running) {

        sockaddr_in clientAddr;
        int clientAddrLen = sizeof(clientAddr);

        SOCKET clientSocket = accept(listenSocket, (sockaddr*)&clientAddr, &clientAddrLen);
        if (clientSocket == INVALID_SOCKET) {
            std::cerr << "accept() failed with error: " << WSAGetLastError() << "\n";
            continue;
        }
        
        char* clientIp = inet_ntoa(clientAddr.sin_addr);
        int clientPort = ntohs(clientAddr.sin_port);
        std::cout << "New client connected from " << clientIp << ":" << clientPort << "\n";
        
        // This is different, first we connect to backend
        SOCKET backendSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        sockaddr_in backendAddr{};
        backendAddr.sin_family = AF_INET;
        backendAddr.sin_port = htons(BACKEND_PORT);
        backendAddr.sin_addr.s_addr = inet_addr(BACKEND_ADDR);

        if (connect(backendSocket, (sockaddr*)&backendAddr, sizeof(backendAddr)) == SOCKET_ERROR) {
            std::cerr << "connect() to backend server failed with error: " << WSAGetLastError() << "\n";
            closesocket(clientSocket);
            continue;
        }

        std::cout << "Connected to backend\n";
        
        // Then we relay the data
        while (running) {
            if(!relayData(clientSocket, backendSocket, "client")) break;
            if(!relayData(backendSocket, clientSocket, "backend")) break;
        }

        std::cout << "Closing connections.\n";
        closesocket(clientSocket);
        closesocket(backendSocket);
    }

    closesocket(listenSocket);
    std::cout << "Reverse proxy shutdown complete.\n";
    return 0;

}

