#include <csignal>
#include <sstream>

#include "HTTPServer.hpp"
#include "HTTPParser.hpp"
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
        logf(threadStr, "Completion status: ", result, ", bytesTransferred: ", bytesTransferred);
        if (!result && overlapped == nullptr) {
            logf(threadStr, "Empty result..");
            break;
        }

        if (overlapped == nullptr) {
            logf(threadStr, "Shutdown signal received.");
            break;
        }

        IOContext* context = CONTAINING_RECORD(overlapped, IOContext, overlapped);

        switch (context->state) {
            case IOType::ACCEPT:
                handleAccept(static_cast<AcceptContext*>(context), threadStr);
                break;
            case IOType::RECV:
                handleRecv(context, bytesTransferred, threadStr);
                break;
            case IOType::SEND:
                handleSend(context, bytesTransferred, threadStr);
                break;
            default:
                logcerr(threadStr, "Unknown IOType");
                break;
        }
    }
}


void HTTPServer::run() {
    logf("[Main] Running HTTPServer");
    std::signal(SIGINT, signalHandler);

    running = true;
    logf("[Main] Initialize IOCP");
    initIOCP();

    logf("[Main] Create listening socket");
    listenSocket_ = createListenSocket();
    CreateIoCompletionPort((HANDLE)listenSocket_, iocpHandle_, 0, 0);

    logf("[Main] Init lpfnAcceptEx");
    initExtensions();

    logf("[Main] Posting initial accepts");
    const int numAccepts = 10;
    for (int i = 0; i < numAccepts; ++i) {
        if (!postAccept("[Main] ")) {
            logcerr("[Main] Failed to post initial AcceptEx");
        }
    }

    logf("[Main] Creating worker threads");
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
    
    logf("[Main] Shutdown HTTPServer");
    logf("[Main] Cleaning up resources..");
    running = false;
    
    logf("[Main] Signal worker threads to shutdown");
    for (size_t i = 0; i < workerThreads.size(); ++i) {
        PostQueuedCompletionStatus(iocpHandle_, 0, 0, nullptr);
    }
    
    logf("[Main] Joining worker threads");
    for (auto& t : workerThreads) {
        if (t.joinable()) { t.join(); }
    }

    if (listenSocket_ != INVALID_SOCKET) {
        logf("[Main] Closing listening socket");
        closesocket(listenSocket_);
        listenSocket_ = INVALID_SOCKET;
    }

    if (iocpHandle_ != nullptr) {
        logf("[Main] Closing IOCP handle");
        CloseHandle(iocpHandle_);
        iocpHandle_ = nullptr;
    }
    logf("[Main] HTTPServer shutdown gracefully.");
}


bool HTTPServer::postAccept(std::string threadStr) {
    AcceptContext* context = new AcceptContext();
    context->socket = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, nullptr, 0, WSA_FLAG_OVERLAPPED);
    if (context->socket == INVALID_SOCKET) {
        logcerr(threadStr, "WSASocket() failed: ", WSAGetLastError());
        delete context;
        return false;
    }

    ZeroMemory(&context->overlapped, sizeof(OVERLAPPED));

    DWORD bytesReceived;
    BOOL result = lpfnAcceptEx(
        listenSocket_,
        context->socket,
        context->acceptBuffer,
        0,
        sizeof(SOCKADDR_IN) + 16,
        sizeof(SOCKADDR_IN) + 16,
        &bytesReceived,
        &context->overlapped
    );

    if (!result && WSAGetLastError() != ERROR_IO_PENDING) {
        logcerr(threadStr, "AcceptEx() failed: ", WSAGetLastError());
        delete context;
        return false;
    }
    return true;
}


bool HTTPServer::postRecv(Connection* conn, std::string threadStr) {
    IOContext* context = conn->recvContext;
    ZeroMemory(&context->overlapped, sizeof(OVERLAPPED));

    WSABUF wsaBuf;
    wsaBuf.buf = conn->recvBuffer.data();
    wsaBuf.len = static_cast<ULONG>(conn->recvBuffer.size());

    DWORD flags = 0;
    DWORD bytesReceived = 0;

    int result = WSARecv(
        conn->socket,
        &wsaBuf,
        1,
        &bytesReceived,
        &flags,
        &context->overlapped,
        nullptr
    );
    logf(threadStr, "WSARecv posted for socket: ", conn->socket,
         ", buffer size: ", wsaBuf.len,
         ", result: ", result,
         ", error: ", (result == SOCKET_ERROR ? WSAGetLastError() : 0));
    
    if (result == SOCKET_ERROR && WSAGetLastError() != WSA_IO_PENDING) {
        logcerr(threadStr, "WSARecv() failed: ", WSAGetLastError());
        delete conn;
        return false;
    }
    return true;
}


bool HTTPServer::postSend(Connection* conn, std::string threadStr) {
    IOContext* context = conn->sendContext;
    ZeroMemory(&context->overlapped, sizeof(OVERLAPPED));

    WSABUF wsaBuf;
    wsaBuf.buf = reinterpret_cast<CHAR*>(conn->sendBuffer.data() + conn->sendOffset);
    wsaBuf.len = static_cast<ULONG>(conn->sendBuffer.size() - conn->sendOffset);

    DWORD bytesSent = 0;
    int result = WSASend(
        conn->socket,
        &wsaBuf,
        1,
        &bytesSent,
        0,
        &context->overlapped,
        nullptr
    );
    if (result == SOCKET_ERROR && WSAGetLastError() != WSA_IO_PENDING) {
        logcerr(threadStr, "WSASend() failed: ", WSAGetLastError());
        delete conn;
        return false;
    }
    return true;
}


void HTTPServer::handleAccept(AcceptContext* acceptContext, std::string threadStr) {
    SOCKET clientSocket = acceptContext->socket;
    auto conn = new Connection(clientSocket);
    acceptContext->socket = INVALID_SOCKET;
    
    if (CreateIoCompletionPort((HANDLE)clientSocket, iocpHandle_, (ULONG_PTR)conn, 0) == nullptr) {
        logcerr(threadStr, "Failed to associate client socket with IOCP: ", GetLastError());
        closesocket(clientSocket);
        delete acceptContext;
        delete conn;
        return;
    }

    if (setsockopt(clientSocket, SOL_SOCKET, SO_UPDATE_ACCEPT_CONTEXT,
                   (char*)&listenSocket_, sizeof(listenSocket_)) == SOCKET_ERROR) {
        logcerr(threadStr, "setsockopt(SO_UPDATE_ACCEPT_CONTEXT) failed: ", WSAGetLastError());
        closesocket(clientSocket);
        delete acceptContext;
        delete conn;
        return;
    }

    logf(threadStr, "New connection accepted");
    postRecv(conn, threadStr);

    delete acceptContext;
    postAccept(threadStr);
}


void HTTPServer::handleRecv(IOContext* context, DWORD bytesTransferred, std::string threadStr) {
    Connection* conn = context->connection;
    if (bytesTransferred == 0) {
        delete conn;
        return;
    }
    std::string data(conn->recvBuffer.data(), bytesTransferred);
    HTTPRequest req = HTTPParser::parse(data);

    std::unordered_map<std::string, std::string> responseHeaders;
    for (auto& header : req.headers) {
        responseHeaders[header.first] = header.second;
    }
    HTTPResponse resp = makeHttpResponse(200, "OK", responseHeaders, req.body);
    std::string response = serializeResponse(resp);

    conn->sendBuffer.assign(response.begin(), response.end());
    conn->sendOffset = 0;

    postSend(conn, threadStr);
}


void HTTPServer::handleSend(IOContext* context, DWORD bytesTransferred, std::string threadStr) {
    Connection* conn = context->connection;
    conn->sendOffset += bytesTransferred;

    if (conn->sendOffset < conn->sendBuffer.size()) {
        postSend(conn, threadStr);
    } else {
        postRecv(conn, threadStr);
    }
}


void HTTPServer::initIOCP() {
    iocpHandle_ = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0);
    if (iocpHandle_ == nullptr) {
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

