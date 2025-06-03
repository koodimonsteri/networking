#include <winsock2.h>
#include <string>
#include <sstream>
#include <iostream>
#include <csignal>
#include <atomic>
#include "log.hpp"
#include "HTTPParser.hpp"


constexpr const char* BACKEND_ADDR = "127.0.0.1";
constexpr int BACKEND_PORT = 8080;

constexpr int BUFFER_SIZE = 4096;

std::atomic<bool> running = true;


struct WinSockGuard {
    WinSockGuard() { WSAStartup(MAKEWORD(2, 2), &wsaData); }
    ~WinSockGuard() { WSACleanup(); }
    WSAData wsaData;
};


void signalHandler(int signal) {
    logf("\nCaught signal ", signal, ", exiting..");
    running = false;
}


SOCKET createListenSocket() {
    SOCKET listenSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listenSocket == INVALID_SOCKET) {
        logcerr("[Main] socket() failed: ", WSAGetLastError());
        exit(1);
    }

    sockaddr_in serverAddr;
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(BACKEND_PORT);
    serverAddr.sin_addr.s_addr = inet_addr(BACKEND_ADDR);

    if (bind(listenSocket, (sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
        logcerr("[Main] bind() failed: ", WSAGetLastError());
        closesocket(listenSocket);
        exit(1);
    }

    if (listen(listenSocket, SOMAXCONN) == SOCKET_ERROR) {
        logcerr("[Main] listen() failed: ", WSAGetLastError());
        closesocket(listenSocket);
        exit(1);
    }

    return listenSocket;
}


SOCKET acceptClient(SOCKET listenSocket) {
    fd_set readSet;
    FD_ZERO(&readSet);
    FD_SET(listenSocket, &readSet);

    timeval timeout;
    timeout.tv_sec = 1;
    timeout.tv_usec = 0;

    int selectResult = select(0, &readSet, nullptr, nullptr, &timeout);
    if (selectResult == SOCKET_ERROR) {
        logcerr("[Main] select() failed: ", WSAGetLastError());
        return INVALID_SOCKET;
    }
    
    if (selectResult == 0) {
        return INVALID_SOCKET;
    }

    sockaddr_in clientAddr;
    int clientAddrLen = sizeof(clientAddr);
    SOCKET clientSocket = accept(listenSocket, (sockaddr*)&clientAddr, &clientAddrLen);
    if (clientSocket == INVALID_SOCKET) {
        logcerr("[Main] accept() failed: ", WSAGetLastError());
        return INVALID_SOCKET;
    }

    char* clientIp = inet_ntoa(clientAddr.sin_addr);
    int clientPort = ntohs(clientAddr.sin_port);
    logf("[Main] New client connected from ", clientIp, ":", clientPort);

    return clientSocket;
}


int main() {
    /*
    Minimal HTTP/1.1 server
    */
    
    logf("[Main] Running simple http server");
    WinSockGuard winSockGuard;
    std::signal(SIGINT, signalHandler);

    SOCKET listenSocket = createListenSocket();

    while (running) {

        SOCKET clientSocket = acceptClient(listenSocket);
        if (clientSocket == INVALID_SOCKET) continue;
        
        char buffer[BUFFER_SIZE];
        int bytesReceived;
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
        HTTPRequest request = HTTPParser::parse(receivedData);

        std::ostringstream responseBody;
        responseBody << "Hellooo from server! Request body: "
                     << request.body;
        std::string bodyStr = responseBody.str();

        std::ostringstream response;
        response << "HTTP/1.1 200 OK\r\n"
                 << "Content-Length: " << bodyStr.size() << "\r\n"
                 << "Content-Type: text/plain\r\n"
                 << "Connection: close\r\n"
                 << "\r\n"
                 << bodyStr;
        std::string responseStr = response.str();

        int sentBytes = send(clientSocket, responseStr.c_str(), static_cast<int>(responseStr.size()), 0);
        if (sentBytes < SOCKET_ERROR) {
            logcerr("[Main] send() failed: ", WSAGetLastError());
            closesocket(clientSocket);
        }
    }

    closesocket(listenSocket);
    logf("Simple http server shut down gracefully.");
    return 0;
}