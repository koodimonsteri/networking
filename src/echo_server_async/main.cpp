#include <mutex>
#include <atomic>
#include <csignal>
#include <iostream>
#include <string>
#include <sstream>
#include <winsock2.h>
#include <thread>
#include <vector>

#pragma comment(lib, "ws2_32.lib")

constexpr int LISTEN_PORT = 8080;
const char* const LISTEN_ADDR = "127.0.0.1";

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


struct WinSockGuard {
    WinSockGuard() { WSAStartup(MAKEWORD(2, 2), &wsaData); }
    ~WinSockGuard() { WSACleanup(); }
    WSADATA wsaData;
};


enum class IOState {
    RECV,
    SEND
};

struct ClientContext {
    SOCKET socket;
    OVERLAPPED overlapped;
    WSABUF wsaBuf;
    char buffer[BUFFER_SIZE];
    IOState state = IOState::RECV;

    ClientContext(SOCKET s) : socket(s) {
        ZeroMemory(&overlapped, sizeof(overlapped));
        wsaBuf.buf = buffer;
        wsaBuf.len = BUFFER_SIZE;
    }
};


std::atomic_bool running = true;

void signalHandler(int signal) {
    logf("\nCaught signal ", signal, ", exiting..");
    running = false;
}


void workerThread(HANDLE iocpHandle) {
    std::ostringstream oss;
    oss << "[Thread " << std::this_thread::get_id() << "] ";
    std::string threadStr = oss.str();
    logf(threadStr, " Started worker");

    while (true) {
        DWORD bytesTransferred = 0;
        ULONG_PTR completionKey = 0;
        LPOVERLAPPED overlapped = nullptr;

        BOOL completionResult = GetQueuedCompletionStatus(
            iocpHandle,
            &bytesTransferred,
            &completionKey,
            &overlapped,
            INFINITE
        );

        if (overlapped == nullptr) {
            logf(threadStr, " Shutdown signal received.");
            break;
        }
        
        SOCKET clientSocket = (SOCKET)completionKey;
        auto* context = CONTAINING_RECORD(overlapped, ClientContext, overlapped);

        if (!completionResult) {
            DWORD err = GetLastError();
            if (bytesTransferred == 0 || err == ERROR_NETNAME_DELETED || err == ERROR_CONNECTION_ABORTED) {
                logf(threadStr, " Client disconnected.");
            } else {
                logcerr(threadStr, " GetQueuedCompletionStatus() failed with error: ", GetLastError());
            }
            closesocket(clientSocket);
            delete CONTAINING_RECORD(overlapped, ClientContext, overlapped);  // or your offset logic
            continue;
        }

        if (context->state == IOState::RECV) {
            if (bytesTransferred == 0) {
                logf(threadStr, " Client disconnected during RECV.");
                closesocket(clientSocket);
                delete context;
                continue;
            }

            std::string msg(context->buffer, bytesTransferred);
            logf(threadStr, " Received ", bytesTransferred, " bytes: ", msg);
            
            context->state = IOState::SEND;

            context->wsaBuf.buf = context->buffer;
            context->wsaBuf.len = bytesTransferred;
            
            DWORD bytesSent = 0;
            int result = WSASend(
                clientSocket,
                &context->wsaBuf,
                1,
                &bytesSent,
                0,
                &context->overlapped,
                nullptr
            );
            
            if(result == SOCKET_ERROR && WSAGetLastError() != WSA_IO_PENDING) {
                logcerr(threadStr, " WSASend() failed with error: ", WSAGetLastError());
                closesocket(clientSocket);
                delete context;
            }

        } else if (context->state == IOState::SEND) {
            if (bytesTransferred == 0) {
                logf(threadStr, " Client disconnected during SEND.");
                closesocket(clientSocket);
                delete context;
                continue;
            }
            logf(threadStr, " Sent ", bytesTransferred, " bytes");

            context->state = IOState::RECV;
            ZeroMemory(&context->overlapped, sizeof(context->overlapped));
            DWORD flags = 0;
            DWORD recvBytes = 0;

            int result = WSARecv(
                clientSocket,
                &context->wsaBuf,
                1,
                &recvBytes,
                &flags,
                &context->overlapped,
                nullptr
            );
            if (result == SOCKET_ERROR && WSAGetLastError() != WSA_IO_PENDING) {
                logf(threadStr, " WSARecv() failed with error: ", WSAGetLastError());
                closesocket(clientSocket);
                delete context;
            }
        }
    }
}


SOCKET createListenSocket() {
    SOCKET listenSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listenSocket == INVALID_SOCKET) {
        logcerr("[Main] Failed to create socket");
        //return 1;
        exit(1);
    }
    
    sockaddr_in serverAddr{};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(LISTEN_PORT);
    serverAddr.sin_addr.s_addr = inet_addr(LISTEN_ADDR);

    if (bind(listenSocket, (sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
        logcerr("[Main] bind() failed with error: ", WSAGetLastError());
        closesocket(listenSocket);
        //return 1;
        exit(1);
    }
    
    if (listen(listenSocket, SOMAXCONN) == SOCKET_ERROR) {
        logcerr("[Main] listen() failed with error: ", WSAGetLastError());
        closesocket(listenSocket);
        //return 1;
        exit(1);
    }

    return listenSocket;
}


int main() {
    /*
    Asynchronous multithreaded echo server using IOCP (I/O Completion Ports)
    Create single IOCP handle that is associated with each client socket
    Worker threads wait and handle completed I/O operations
    Communication happens via WSASend and WSARecv
    */
    logf("[Main] Running async multithreaded (IOCP) echo server!");
    
    std::signal(SIGINT, signalHandler);
    WinSockGuard wsGuard;

    SOCKET listenSocket = createListenSocket();

    logf("[Main] Echo server listening on ", LISTEN_ADDR, ":", LISTEN_PORT);

    HANDLE iocpHandle = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0);
    if (iocpHandle == NULL) {
        logcerr("[Main] CreateIoCompletionPort() failed with error: ", GetLastError());
        closesocket(listenSocket);
        return 1;
    }
    logf("[Main] iocpHandle created succesfully!");

    std::vector<std::thread> workerThreads;
    for (int i = 0; i < MAX_WORKER_THREADS; i++) {
        workerThreads.emplace_back(workerThread, iocpHandle);
    }

    while (running) {
        fd_set readSet;
        FD_ZERO(&readSet);
        FD_SET(listenSocket, &readSet);

        timeval timeout;
        timeout.tv_sec = 1;
        timeout.tv_usec = 0;
        
        int selectResult = select(0, &readSet, nullptr, nullptr, &timeout);
        if (selectResult == SOCKET_ERROR) {
            logcerr("[Main] select() failed with error: ", WSAGetLastError());
            break;
        }
        
        if (selectResult == 0) {
            continue;
        }
        
        sockaddr_in clientAddr{};
        int clientAddrLen = sizeof(clientAddr);

        SOCKET clientSocket = accept(listenSocket, (sockaddr*)&clientAddr, &clientAddrLen);
        if (clientSocket == INVALID_SOCKET) {
            logcerr("[Main] accept() failed with error: ", WSAGetLastError());
            continue;
        }
        
        char* clientIp = inet_ntoa(clientAddr.sin_addr);
        int clientPort = ntohs(clientAddr.sin_port);
        logf("[Main] New client connected from ", clientIp, ":", clientPort);

        HANDLE clientIOCPHandle = CreateIoCompletionPort((HANDLE)clientSocket, iocpHandle, (ULONG_PTR)clientSocket, 0);
        if (clientIOCPHandle == nullptr) {
            logcerr("[Main] CreateIoCompletionPort() failed with error: ", GetLastError());
            closesocket(clientSocket);
            continue;
        }
        
        auto* context = new ClientContext(clientSocket);
        logf("[Main] New client socket associated with IOCP");

        DWORD flags = 0;
        DWORD bytesRecv = 0;

        int result = WSARecv(
            clientSocket,
            &context->wsaBuf,
            1,
            &bytesRecv,
            &flags,
            &context->overlapped,
            nullptr
        );

        if (result == SOCKET_ERROR && WSAGetLastError() != WSA_IO_PENDING) {
            logcerr("[Main] WSARecv() failed with error: ", WSAGetLastError());
            closesocket(clientSocket);
            delete context;
        }
    }

    logf("[Main] Stop worker threads");
    for (int i = 0; i < MAX_WORKER_THREADS; ++i) {
        PostQueuedCompletionStatus(iocpHandle, 0, 0, nullptr);
    }

    logf("[Main] Waiting for worker threads to finish.");
    for (auto& t : workerThreads) {
        if (t.joinable()) t.join();
    }

    CloseHandle(iocpHandle);
    closesocket(listenSocket);
    logf("[Main] Async echo server shut down gracefully!");
    return 0;
}
