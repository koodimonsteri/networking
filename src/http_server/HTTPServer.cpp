#include <csignal>
#include <sstream>

#include "HTTPServer.hpp"
#include "log.hpp"

HTTPServer* HTTPServer::instance_ = nullptr;

HTTPServer::HTTPServer(const std::string& serverAddress, const u_short serverPort)
    : address_(serverAddress), port_(serverPort) {
    logf("Creating HTTPServer on: ", serverAddress, ":", serverPort);
    instance_ = this;
}


HTTPServer::~HTTPServer() {
    if (running) {
        shutdown();
    }
}


void HTTPServer::signalHandler(int signal) {
    logf("\nCaught signal ", signal, ", exiting..");
    if (instance_) {
        instance_->shutdown();
    }
}


void HTTPServer::workerThread() {
    std::ostringstream oss;
    oss << "[Thread " << std::this_thread::get_id() << "] ";
    std::string threadStr = oss.str();
    logf(threadStr, "Started worker");

    while (running) {
        DWORD bytesTransferred;
        ULONG_PTR key;
        LPOVERLAPPED overlapped;

        BOOL result = GetQueuedCompletionStatus(iocpHandle_, &bytesTransferred, &key, &overlapped, INFINITE);

        if (!result && overlapped == nullptr) {
            break;
        }

        if (overlapped == nullptr) {
            logf(threadStr, "Shutdown signal received.");
            break;
        }

        IOContext* context = CONTAINING_RECORD(overlapped, IOContext, overlapped);
        
        switch (context->state) {
            case IOType::ACCEPT:
                handleAccept(context);
                break;
            case IOType::RECV:
                handleRecv(context);
                break;
            case IOType::SEND:
                handleSend(context);
                break;
            default:
                logcerr(threadStr, "Incorrect state..?");
        }
    }
}


void HTTPServer::run() {
    logf("Running HTTPServer");
    std::signal(SIGINT, signalHandler);

    running = true;
    logf("Initialize IOCP");
    initIOCP();

    logf("Create listening socket");
    listenSocket_ = createListenSocket();

    logf("Init lpfnAcceptEx");
    initExtensions();

    logf("Posting initial accepts");
    const int numAccepts = 10;
    for (int i = 0; i < numAccepts; ++i) {
        if (!postAccept()) {
            logcerr("Failed to post initial AcceptEx");
        }
    }

    logf("Creating worker threads");
    for (int i = 0; i < n_threads; i++) {
        workerThreads.emplace_back(&HTTPServer::workerThread, this);
    }

    while (running) {
        // Just sleeping here while workers do all the work :)
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    shutdown();

}


void HTTPServer::shutdown() {
    bool expected = false;
    if (!shutdownCalled.compare_exchange_strong(expected, true)) {
        return;
    }
    
    logf("Shutdown HTTPServer");
    logf("Cleaning up resources..");
    running = false;
    
    logf("Signal worker threads to shutdown");
    for (size_t i = 0; i < workerThreads.size(); ++i) {
        PostQueuedCompletionStatus(iocpHandle_, 0, 0, nullptr);
    }
    
    logf("Joining worker threads");
    for (auto& t : workerThreads) {
        if (t.joinable()) { t.join(); }
    }

    if (listenSocket_ != INVALID_SOCKET) {
        logf("Closing listening socket");
        closesocket(listenSocket_);
        listenSocket_ = INVALID_SOCKET;
    }

    if (iocpHandle_ != nullptr) {
        logf("Closing IOCP handle");
        CloseHandle(iocpHandle_);
        iocpHandle_ = nullptr;
    }
    logf("HTTPServer shutdown gracefully.");
}


bool HTTPServer::postAccept() {
    SOCKET clientSocket = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, nullptr, 0, WSA_FLAG_OVERLAPPED);
    if (clientSocket == INVALID_SOCKET) {
        logcerr("WSASocket() failed: ", WSAGetLastError());
        return false;
    }

    IOContext* context = new IOContext(IOType::ACCEPT);
    context->socket = clientSocket;
    ZeroMemory(&context->overlapped, sizeof(OVERLAPPED));

    DWORD bytesReceived;
    BOOL result = lpfnAcceptEx(
        listenSocket_,
        clientSocket,
        context->buffer,
        0,
        sizeof(SOCKADDR_IN) + 16,
        sizeof(SOCKADDR_IN) + 16,
        &bytesReceived,
        &context->overlapped
    );

    if (!result && WSAGetLastError() != ERROR_IO_PENDING) {
        logcerr("AcceptEx() failed: ", WSAGetLastError());
        closesocket(clientSocket);
        delete context;
        return false;
    }
    return true;
}


bool HTTPServer::postRecv(IOContext* context) {
    DWORD flags = 0;
    DWORD bytesReceived = 0;

    WSABUF buf;
    buf.buf = context->buffer;
    buf.len = sizeof(context->buffer);

    int result = WSARecv(
        context->socket,
        &buf,
        1,
        &bytesReceived,
        &flags,
        &context->overlapped,
        NULL
    );

    if (result == SOCKET_ERROR && WSAGetLastError() != WSA_IO_PENDING) {
        logcerr("WSARecv() failed: ", WSAGetLastError());
        return false;
    }

    return true;
}


void HTTPServer::handleAccept(IOContext* context) {
    if (CreateIoCompletionPort((HANDLE)context->socket, iocpHandle_, (ULONG_PTR)context->socket, 0) == NULL) {
        logcerr("Failed to associate client socket with IOCP: ", GetLastError());
        closesocket(context->socket);
        delete context;
        return;
    }

    logf("New connection accepted");

    postRecv(context);

    postAccept();
}


void HTTPServer::handleRecv(IOContext* context) {

}


void HTTPServer::handleSend(IOContext* context) {

}


void HTTPServer::initIOCP() {
    iocpHandle_ = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0);
    if (iocpHandle_ == NULL) {
        logcerr("CreateIoCompletionPort() failed: ", GetLastError());
        throw std::runtime_error("Failed to init IOCP");
    }
}


void HTTPServer::initExtensions() {
    GUID guidAcceptEx = WSAID_ACCEPTEX;
    DWORD bytes = 0;

    int result = WSAIoctl(
        listenSocket_,
        SIO_GET_EXTENSION_FUNCTION_POINTER,
        &guidAcceptEx,
        sizeof(guidAcceptEx),
        &lpfnAcceptEx,
        sizeof(lpfnAcceptEx),
        &bytes,
        nullptr,
        nullptr
    );

    if (result == SOCKET_ERROR) {
        logcerr("WSAIoctl() failed getting AcceptEx pointer: ", WSAGetLastError());
        throw std::runtime_error("AcceptEx pointer is null!");
    }
}


SOCKET HTTPServer::createListenSocket() {

    SOCKET listenSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listenSocket == INVALID_SOCKET) {
        logcerr("socket() failed: ", WSAGetLastError());
        return INVALID_SOCKET;
    }

    sockaddr_in serverAddr;
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(port_);
    serverAddr.sin_addr.s_addr = inet_addr(address_.c_str());
    if (bind(listenSocket, (sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
        logcerr("bind() failed: ", WSAGetLastError());
        closesocket(listenSocket);
        return INVALID_SOCKET;
    }

    if (listen(listenSocket, SOMAXCONN) == SOCKET_ERROR) {
        logcerr("listen() failed: ", WSAGetLastError());
        closesocket(listenSocket);
        return INVALID_SOCKET;
    }
    
    return listenSocket;
}

