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
    IOState state = IOState::RECV;
    ProxyContext* peer = nullptr;
    std::mutex proxyMutex;
    std::atomic<bool> cleanedUp = false;

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


void cleanupContextPair(ProxyContext* context, std::string initiator) {
    if (context->cleanedUp.exchange(true)) {
        return;
    }

    logf(initiator, " [cleanup] Cleaning up context");

    if (context->peer && !context->peer->cleanedUp.exchange(true)) {
        std::lock_guard<std::mutex> lock(context->peer->proxyMutex);
        closesocket(context->peer->srcSocket);
        delete context->peer;
    }

    closesocket(context->srcSocket);
    delete context;
    logf(initiator, " [cleanup] Clean up complete");
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

        if (context->cleanedUp.load()) {
            continue;
        }

        if (!completionResult) {
            DWORD err = GetLastError();
            if (err != ERROR_OPERATION_ABORTED) {
                logf(threadStr, " Completion failed with error: ", err);
            }
            cleanupContextPair(context, threadStr);
            continue;
        }

        if (context->state == IOState::RECV) {
            if (bytesTransferred == 0) {
                logf(threadStr, " Client disconnected during RECV.");
                cleanupContextPair(context, threadStr);
                continue;
            }

            std::string msg(context->buffer, bytesTransferred);
            logf(threadStr, " Received ", bytesTransferred, " bytes: ", msg);
            {
                std::lock_guard<std::mutex> lock(context->peer->proxyMutex);
                context->peer->state = IOState::SEND;
                memcpy(context->peer->buffer, context->buffer, bytesTransferred);
                context->peer->wsaBuf.buf = context->peer->buffer;
                context->peer->wsaBuf.len = bytesTransferred;

                ZeroMemory(&context->peer->overlapped, sizeof(OVERLAPPED));

                DWORD bytesSent = 0;
                int result = WSASend(
                    context->dstSocket,
                    &context->peer->wsaBuf,
                    1,
                    &bytesSent,
                    0,
                    &context->peer->overlapped,
                    nullptr
                );

                if (result == 0) {
                    logf(threadStr, " WSASend() completed immediately with ", bytesSent, " bytes");

                    context->peer->state = IOState::RECV;
                    ZeroMemory(&context->peer->overlapped, sizeof(context->peer->overlapped));
                    DWORD flags = 0;
                    DWORD recvBytes = 0;
                    int recvResult = WSARecv(
                        context->peer->srcSocket,
                        &context->peer->wsaBuf,
                        1,
                        &recvBytes,
                        &flags,
                        &context->peer->overlapped,
                        nullptr
                    );
                    if (recvResult == SOCKET_ERROR && WSAGetLastError() != WSA_IO_PENDING) {
                        logcerr(threadStr, " WSARecv() after immediate WSASend failed: ", WSAGetLastError());
                        cleanupContextPair(context->peer, threadStr);
                    }
                } else if(result == SOCKET_ERROR && WSAGetLastError() != WSA_IO_PENDING) {
                    logcerr(threadStr, " WSASend() failed with error: ", WSAGetLastError());
                    cleanupContextPair(context, threadStr);
                }
            }

        } else if (context->state == IOState::SEND) {
            if (bytesTransferred == 0) {
                logf(threadStr, " Client disconnected during SEND.");
                cleanupContextPair(context, threadStr);
                continue;
            }
            logf(threadStr, " Sent ", bytesTransferred, " bytes");

            context->state = IOState::RECV;

            ZeroMemory(&context->overlapped, sizeof(context->overlapped));

            DWORD flags = 0;
            DWORD recvBytes = 0;
            int result = WSARecv(
                context->srcSocket,
                &context->wsaBuf,
                1,
                &recvBytes,
                &flags,
                &context->overlapped,
                nullptr
            );

            if (result == 0) {
                logf(threadStr, " WSARecv() completed immediately with ", recvBytes, " bytes");

            } else if (result == SOCKET_ERROR && WSAGetLastError() != WSA_IO_PENDING) {
                logf(threadStr, " WSARecv() failed with error: ", WSAGetLastError());
                cleanupContextPair(context, threadStr);
            }
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


int main() {
    /*
    Asynchronous multithreaded reverse proxy using IOCP (I/O Completion Ports)
    Works but thread syncing and clean up seems kinda funky :)

    Create single IOCP handle
    For each client associate client and backend sockets with iocp
    Worker threads wait and handle completed I/O operations
    Communication happens via WSASend and WSARecv
    */
    logf("[Main] Running async multithreaded (IOCP) reverse proxy!");
    
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

        // Client socket + iocp handle
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
        logf("[Main] Created client IOCP");

        // Backend socket + iocp handle
        SOCKET backendSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        sockaddr_in backendAddr{};
        backendAddr.sin_family = AF_INET;
        backendAddr.sin_port = htons(BACKEND_PORT);
        backendAddr.sin_addr.s_addr = inet_addr(BACKEND_ADDR);

        if (connect(backendSocket, (sockaddr*)&backendAddr, sizeof(backendAddr)) == SOCKET_ERROR) {
            logcerr("[Main] connect() to backend server failed with error: ", WSAGetLastError());
            closesocket(clientSocket);
            closesocket(backendSocket);
            continue;
        }
        logf("[Main] Connected to backend");

        HANDLE backendIOCPHandle = CreateIoCompletionPort((HANDLE)backendSocket, iocpHandle, (ULONG_PTR)backendSocket, 0);
        if (backendIOCPHandle == nullptr) {
            logcerr("[Main] CreateIoCompletionPort() failed with error: ", GetLastError());
            closesocket(clientSocket);
            closesocket(backendSocket);
            continue;
        }
        logf("[Main] Created backend IOCP");
        
        // Create contexts
        auto* clientContext = new ProxyContext(clientSocket, backendSocket);
        auto* backendContext = new ProxyContext(backendSocket, clientSocket);

        clientContext->peer = backendContext;
        backendContext->peer = clientContext;
        
        // Start IO
        DWORD cbFlags = 0;
        DWORD cbBytesRecv = 0;

        int ccResult = WSARecv(
            clientSocket,
            &clientContext->wsaBuf,
            1,
            &cbBytesRecv,
            &cbFlags,
            &clientContext->overlapped,
            nullptr
        );

        if (ccResult == SOCKET_ERROR && WSAGetLastError() != WSA_IO_PENDING) {
            logcerr("[Main] Client WSARecv() failed with error: ", WSAGetLastError());
            cleanupContextPair(clientContext, "[Main]");
        }

        DWORD bcFlags = 0;
        DWORD bcBytesRecv = 0;

        int bcResult = WSARecv(
            backendSocket,
            &backendContext->wsaBuf,
            1,
            &bcBytesRecv,
            &bcFlags,
            &backendContext->overlapped,
            nullptr
        );

        if (bcResult == SOCKET_ERROR && WSAGetLastError() != WSA_IO_PENDING) {
            logcerr("[Main] Backend WSARecv() failed with error: ", WSAGetLastError());
            cleanupContextPair(clientContext, "[Main]");
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
    logf("[Main] Async reverse proxy shut down gracefully!");
    return 0;
}
