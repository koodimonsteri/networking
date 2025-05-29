#include <atomic>
#include <csignal>
#include <iostream>
#include <string>
#include <winsock2.h>

#pragma comment(lib, "ws2_32.lib")

constexpr int LISTEN_PORT = 8080;
const char* const LISTEN_ADDR = "127.0.0.1";
constexpr int BUFFER_SIZE = 1024;

struct WinSockGuard {
    WinSockGuard() { WSAStartup(MAKEWORD(2, 2), &wsaData); }
    ~WinSockGuard() { WSACleanup(); }
    WSADATA wsaData;
};

std::atomic_bool running = true;

void signalHandler(int signal) {
    std::cout << "\nCaught signal " << signal <<  ", exiting..\n";
    running = false;
}


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
    std::cout << "Received " << bytesReceived << " bytes from " << directionLabel
              << ": \"" << receivedData << "\"\n";

    int bytesSent = send(to, buffer, bytesReceived, 0);
    if (bytesSent < 0) {
        std::cerr << "send() to " << directionLabel << " failed with error: "
                  << WSAGetLastError() << "\n";
        return false;
    }

    return true;
}


int main() {
    std::cout << "Running echo server!\n";

    std::signal(SIGINT, signalHandler);

    WinSockGuard winSockGuard;

    SOCKET listenSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listenSocket == INVALID_SOCKET) {
        std::cerr << "Failed to create socket\n";
        return 1;
    }
    
    sockaddr_in serverAddr{};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(LISTEN_PORT);
    serverAddr.sin_addr.s_addr = inet_addr(LISTEN_ADDR);

    if (bind(listenSocket, (sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
        std::cerr << "bind() failed with error: " << WSAGetLastError() << "\n";
        closesocket(listenSocket);
        return 1;
    }
    
    if (listen(listenSocket, SOMAXCONN) == SOCKET_ERROR) {
        std::cerr << "listen() failed with error: " << WSAGetLastError() << "\n";
        closesocket(listenSocket);
        return 1;
    }

    std::cout << "Server listening on " << LISTEN_ADDR << ":" << LISTEN_PORT << "\n";

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

        char buffer[BUFFER_SIZE];
        int bytesReceived;

        while (true) {
            bytesReceived = recv(clientSocket, buffer, BUFFER_SIZE, 0);
            
            if (bytesReceived < 0) {
                std::cerr << "recv() failed with error: " << WSAGetLastError() << "\n";
                break;
            }

            if (bytesReceived == 0) {
                std::cout << "Client disconnected.\n";
                break;
            }
            std::string receivedData(buffer, bytesReceived);
            std::cout << "Received " << bytesReceived << " bytes: \"" << receivedData << "\"\n";
            
            int bytesSent = send(clientSocket, buffer, bytesReceived, 0);
            if (bytesSent < SOCKET_ERROR) {
                std::cerr << "send() failed with error: " << WSAGetLastError() << "\n";
                break;
            }
        }

        closesocket(clientSocket);
    }

    closesocket(listenSocket);
    std::cout << "Closing echo server.." << std::endl;
}
