#include <mutex>
#include <atomic>
#include <csignal>
#include <iostream>
#include <string>
#include <sstream>
#include <winsock2.h>
#include <thread>
#include <vector>

const char* const LISTEN_ADDR = "127.0.0.1";
constexpr int LISTEN_PORT = 9000;

const char* const BACKEND_ADDR = "127.0.0.1";
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


struct WinSockGuard {
    WinSockGuard() { WSAStartup(MAKEWORD(2, 2), &wsaData); }
    ~WinSockGuard() { WSACleanup(); }
    WSADATA wsaData;
};


enum class IOState {
    RECV,
    SEND
};

struct ProxyContext {
    SOCKET srcSocket;
    SOCKET dstSocket;
    OVERLAPPED overlapped;
    WSABUF wsaBuf;
    char buffer[BUFFER_SIZE];
    size_t bufferLen;
    IOState state = IOState::RECV;


    ProxyContext(SOCKET srcSock, SOCKET dstSock) : srcSocket(srcSock), dstSocket(dstSock) {
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


void startInitialRecv(SOCKET srcSocket, SOCKET dstSocket) {
    ProxyContext* ctx = new ProxyContext(srcSocket, dstSocket);
    ctx->state = IOState::RECV;
    ctx->wsaBuf.buf = ctx->buffer;
    ctx->wsaBuf.len = BUFFER_SIZE;

    DWORD flags = 0;
    DWORD bytesReceived = 0;
    ZeroMemory(&ctx->overlapped, sizeof(ctx->overlapped));

    int result = WSARecv(
        srcSocket,
        &ctx->wsaBuf,
        1,
        &bytesReceived,
        &flags,
        &ctx->overlapped,
        nullptr
    );

    if (result == SOCKET_ERROR && WSAGetLastError() != WSA_IO_PENDING) {
        logcerr("Initial WSARecv failed: ", WSAGetLastError());
        delete ctx;
    }
}


void cleanUpContext(ProxyContext* context) {
    closesocket(context->srcSocket);
    closesocket(context->dstSocket);
    delete context;
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
        
        auto* context = CONTAINING_RECORD(overlapped, ProxyContext, overlapped);

        if (!completionResult) {
            DWORD err = GetLastError();
            if (bytesTransferred == 0) {
                logf(threadStr, " Client disconnected (bytesTransferred == 0)");
            } else if (err == ERROR_NETNAME_DELETED || err == ERROR_CONNECTION_ABORTED) {
                logf(threadStr, " Client disconnected (connection closed)");
            } else {
                logcerr(threadStr, " GetQueuedCompletionStatus() failed with error: ", err);
            }
            cleanUpContext(context);
            continue;
        }

        if (bytesTransferred == 0) {
            logf(threadStr, " Client disconnected gracefully (bytesTransferred == 0)");
            cleanUpContext(context);
            continue;
        }

        if (context->state == IOState::RECV) {

            std::string msg(context->buffer, bytesTransferred);
            logf(threadStr, " Received ", bytesTransferred, " bytes: ", msg);
            
            ProxyContext* sendContext = new ProxyContext(context->dstSocket, context->srcSocket);
            sendContext->state = IOState::SEND;
            memcpy(sendContext->buffer, context->buffer, bytesTransferred);
            sendContext->wsaBuf.buf = context->buffer;
            sendContext->wsaBuf.len = bytesTransferred;

            ZeroMemory(&sendContext->overlapped, sizeof(sendContext->overlapped));

            DWORD bytesSent = 0;
            int sendResult = WSASend(
                sendContext->srcSocket,
                &sendContext->wsaBuf,
                1,
                &bytesSent,
                0,
                &sendContext->overlapped,
                nullptr
            );

            if (sendResult == SOCKET_ERROR && WSAGetLastError() != WSA_IO_PENDING) {
                logcerr(threadStr, " WSASend() failed with error: ", WSAGetLastError());
                cleanUpContext(sendContext);
            }
            startInitialRecv(context->srcSocket, context->dstSocket);

        } else if (context->state == IOState::SEND) {
            logf(threadStr, " Sent ", bytesTransferred, " bytes");
            delete context;
        }
    }
}


SOCKET createListenSocket() {
    SOCKET listenSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listenSocket == INVALID_SOCKET) {
        logcerr("[Main] Failed to create socket");
        exit(1);
    }
    
    sockaddr_in serverAddr{};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(LISTEN_PORT);
    serverAddr.sin_addr.s_addr = inet_addr(LISTEN_ADDR);

    if (bind(listenSocket, (sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
        logcerr("[Main] bind() failed with error: ", WSAGetLastError());
        closesocket(listenSocket);
        exit(1);
    }
    
    if (listen(listenSocket, SOMAXCONN) == SOCKET_ERROR) {
        logcerr("[Main] listen() failed with error: ", WSAGetLastError());
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
            logcerr("[Main] select() failed with error: ", WSAGetLastError());
            return INVALID_SOCKET;
        }
        
        if (selectResult == 0) {
            return INVALID_SOCKET;
        }
        
        sockaddr_in clientAddr{};
        int clientAddrLen = sizeof(clientAddr);

        SOCKET clientSocket = accept(listenSocket, (sockaddr*)&clientAddr, &clientAddrLen);
        if (clientSocket == INVALID_SOCKET) {
            logcerr("[Main] accept() failed with error: ", WSAGetLastError());
            return INVALID_SOCKET;
        }
        
        char* clientIp = inet_ntoa(clientAddr.sin_addr);
        int clientPort = ntohs(clientAddr.sin_port);
        logf("[Main] New client connected from ", clientIp, ":", clientPort);
        return clientSocket;
}


SOCKET connectToBackend() {
    SOCKET backendSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    sockaddr_in backendAddr{};
    backendAddr.sin_family = AF_INET;
    backendAddr.sin_port = htons(BACKEND_PORT);
    backendAddr.sin_addr.s_addr = inet_addr(BACKEND_ADDR);

    if (connect(backendSocket, (sockaddr*)&backendAddr, sizeof(backendAddr)) == SOCKET_ERROR) {
        logcerr("[Main] connect() to backend server failed with error: ", WSAGetLastError());
        closesocket(backendSocket);
        return INVALID_SOCKET;
    }
    logf("[Main] Connected to backend");
    return backendSocket;
}


bool associateIOCP(SOCKET socket, HANDLE iocpHandle, std::string label) {
    HANDLE result = CreateIoCompletionPort((HANDLE)socket, iocpHandle, (ULONG_PTR)socket, 0);
    if (!result) {
        logcerr("[Main] CreateIoCompletionPort() failed for ", label, " socket (", socket, ") with error: ", GetLastError());
        return false;
    }
    logf("[Main] Associated ", label, " socket (", socket, ") with IOCP");
    return true;
}


int main() {
    /*
    V2 Asynchronous multithreaded reverse proxy using IOCP (I/O Completion Ports)
    
    V1 tried to handle communication with persisting ProxyContexts by modifying the state,
    that quickly became unpleasant and caused funky behaviour due to thread syncing.

    V2 handles communication per IO by creating and deleting ProxyContexts,
    so, we don't need any thread syncing.

    Create single IOCP handle
    For each client, associate client and backend sockets with iocp
    Worker threads wait and handle completed I/O operations
    Communication happens via WSASend and WSARecv
    */
    logf("[Main] Running V2 async multithreaded (IOCP) reverse proxy!");
    
    std::signal(SIGINT, signalHandler);
    WinSockGuard wsGuard;

    SOCKET listenSocket = createListenSocket();

    logf("Reverse proxy listening on ", LISTEN_ADDR, ":", LISTEN_PORT, ", forwarding to ", BACKEND_ADDR, ":", BACKEND_PORT);

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
        SOCKET clientSocket = acceptClient(listenSocket);
        if (clientSocket == INVALID_SOCKET) {
            continue;
        } 

        SOCKET backendSocket = connectToBackend();
        if (backendSocket == INVALID_SOCKET) {
            closesocket(clientSocket);
            continue;
        } 

        if (!associateIOCP(clientSocket, iocpHandle, "client") ||
            !associateIOCP(backendSocket, iocpHandle, "backend")) {
            closesocket(clientSocket);
            closesocket(backendSocket);
            continue;
        }
        
        startInitialRecv(clientSocket, backendSocket);
        startInitialRecv(backendSocket, clientSocket);
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
    logf("[Main] Async reverse proxy shut down gracefully!");
    return 0;
}
