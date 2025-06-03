#include <mutex>
#include <atomic>
#include <csignal>
#include <iostream>
#include <string>
#include <sstream>
#include <winsock2.h>
#include <thread>
#include <vector>
#include <algorithm>
#include "Framing.hpp"

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

struct IOContext {
    SOCKET socket;
    OVERLAPPED overlapped;
    WSABUF wsaBuf;
    char buffer[BUFFER_SIZE];
    std::vector<char> sendBuffer;
    size_t sendOffset = 0;
    IOState state = IOState::RECV;
    FrameDecoder decoder;
    FrameEncoder encoder;

    IOContext(SOCKET s) : socket(s) {
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


void postRecv(IOContext* ctx, std::string threadStr) {
    ctx->state = IOState::RECV;
    ZeroMemory(&ctx->overlapped, sizeof(ctx->overlapped));
    DWORD flags = 0, bytes = 0;
    int r = WSARecv(ctx->socket, &ctx->wsaBuf, 1, &bytes, &flags, &ctx->overlapped, nullptr);
    if (r == SOCKET_ERROR && WSAGetLastError() != WSA_IO_PENDING) {
        logcerr(threadStr, "WSARecv failed: ", WSAGetLastError());
        closesocket(ctx->socket);
        delete ctx;
    }
}

void postSend(IOContext* ctx, std::string threadStr) {
    ctx->state = IOState::SEND;
    ZeroMemory(&ctx->overlapped, sizeof(ctx->overlapped));

    size_t remaining = ctx->sendBuffer.size() - ctx->sendOffset;
    size_t toSend = std::min(remaining, static_cast<size_t>(BUFFER_SIZE));
    memcpy(ctx->buffer, ctx->sendBuffer.data() + ctx->sendOffset, toSend);
    ctx->wsaBuf.buf = ctx->buffer;
    ctx->wsaBuf.len = static_cast<ULONG>(toSend);

    DWORD bytes = 0;
    int r = WSASend(ctx->socket, &ctx->wsaBuf, 1, &bytes, 0, &ctx->overlapped, nullptr);
    if (r == SOCKET_ERROR && WSAGetLastError() != WSA_IO_PENDING) {
        logcerr(threadStr, "WSASend failed: ", WSAGetLastError());
        closesocket(ctx->socket);
        delete ctx;
    }
}


void workerThread(HANDLE iocpHandle) {
    std::ostringstream oss;
    oss << "[Thread " << std::this_thread::get_id() << "] ";
    std::string threadStr = oss.str();
    logf(threadStr, "Started worker");

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
            logf(threadStr, "Shutdown signal received.");
            break;
        }
        
        auto* context = CONTAINING_RECORD(overlapped, IOContext, overlapped);

        if (!completionResult || bytesTransferred == 0) {
            if (bytesTransferred == 0) {
                logf(threadStr, "Client socket ", context->socket, " disconnected gracefully.");
            } else {
                logcerr(threadStr, "Client socket ", context->socket, " disconnected with error: ", WSAGetLastError());
            }
            closesocket(context->socket);
            delete context;
            continue;
        }

        if (context->state == IOState::RECV) {
            context->decoder.feed(context->buffer, bytesTransferred);
            Frame frame;
            while (context->decoder.nextFrame(frame)) {
                logf(threadStr, "Received frame of length ", frame.length + Frame::HEADER_LEN);
                FrameEncoder encoder;
                encoder.feed(frame.data.get(), frame.length);
                auto encoded = encoder.next();

                logf(threadStr, "Sending frame of length ", encoded.size());
                IOContext* sendCtx = new IOContext(context->socket);
                sendCtx->sendBuffer = std::move(encoded);
                sendCtx->sendOffset = 0;
                postSend(sendCtx, threadStr);
            }
            postRecv(context, threadStr);
        } else if (context->state == IOState::SEND) {
            context->sendOffset += bytesTransferred;
            if (context->sendOffset < context->sendBuffer.size()) {
                postSend(context, threadStr);
            } else {
                delete context;
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


int main() {
    /*
    Length-prefix framed asynchronous multithreaded echo server using IOCP (I/O Completion Ports)

    Messages are received and echoed back in format.
    <MSG_LEN><MESSAGE>
     4 bytes  n bytes
    
    FrameDecoder reads first 4 bytes from input stream to message length,
    and message length amount to buffer.

    FrameEncoder encodes buffer into frames.
    When sending, we chunk encoder frames by buffer size.

    Worker threads wait and handle completed I/O operations.
    Worker threads echo back only after full frame has been received.
    IOContexts are short lived per each SEND,
    and should be deleted after full frame has been sent, error happened or client disconnected.
    Communication happens via WSASend and WSARecv.
    */
    logf("[Main] Running length-prefix framed async multithreaded (IOCP) echo server!");
    
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
        SOCKET clientSocket = acceptClient(listenSocket);
        if (clientSocket == INVALID_SOCKET) {
            continue;
        } 

        HANDLE clientIOCPHandle = CreateIoCompletionPort((HANDLE)clientSocket, iocpHandle, (ULONG_PTR)clientSocket, 0);
        if (clientIOCPHandle == nullptr) {
            logcerr("[Main] CreateIoCompletionPort() failed with error: ", GetLastError());
            closesocket(clientSocket);
            continue;
        }
        
        logf("[Main] New client socket associated with IOCP");

        auto* context = new IOContext(clientSocket);
        postRecv(context, "[Main]");
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
