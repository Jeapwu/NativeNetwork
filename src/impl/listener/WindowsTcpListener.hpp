#ifndef WINDOWS_TCP_LISTENER_H
#define WINDOWS_TCP_LISTENER_H

#include <winsock2.h>
#include <mswsock.h>
#include <windows.h>
#include <iostream>
#include <vector>
#include <thread>
#include <atomic>
#include <system_error>
#include <optional>
#include <ws2tcpip.h>
#include "TcpStream.h"

#pragma comment(lib, "Ws2_32.lib")  // 链接 WinSock 库
#pragma comment(lib, "Mswsock.lib") // 链接 Mswsock 库

namespace net
{
    // IOCP 相关的结构
    struct AcceptOverlapped
    {
        OVERLAPPED overlapped;
        SOCKET clientSocket;
        sockaddr_in clientAddr;
        int clientAddrLen;
    };

    class TcpListener::Impl
    {
    public:
        Impl() : iocpHandle_(INVALID_HANDLE_VALUE), listenSocket_(INVALID_SOCKET) {}

        ~Impl()
        {
            if (listenSocket_ != INVALID_SOCKET)
            {
                closesocket(listenSocket_);
            }
            if (iocpHandle_ != INVALID_HANDLE_VALUE)
            {
                CloseHandle(iocpHandle_);
            }
        }

        // 绑定地址和端口
        bool bind(const std::string& address, int port, std::error_code& ec)
        {
            iocpHandle_ = CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, 0);
            if (iocpHandle_ == nullptr)
            {
                ec = std::make_error_code(std::errc::io_error);
                return false;
            }

            listenSocket_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
            if (listenSocket_ == INVALID_SOCKET)
            {
                ec = std::make_error_code(std::errc::bad_file_descriptor);
                return false;
            }

            // 启用 SO_REUSEADDR
            int optval = 1;
            if (setsockopt(listenSocket_, SOL_SOCKET, SO_REUSEADDR, (char*)&optval, sizeof(optval)) == SOCKET_ERROR) {
                ec = std::make_error_code(std::errc::operation_not_permitted);
                closesocket(listenSocket_);
                return false;
            }

            // 设置地址和端口
            sockaddr_in addr = {};
            addr.sin_family = AF_INET;
            addr.sin_port = htons(port);

            int result = inet_pton(AF_INET, address.c_str(), &addr.sin_addr);
            if (result != 1) {
                ec = std::make_error_code(std::errc::invalid_argument);
                std::cerr << "Invalid address format: " << address << std::endl;
                closesocket(listenSocket_);
                return false;
            }

            // 绑定到指定地址和端口
            if (::bind(listenSocket_, reinterpret_cast<SOCKADDR*>(&addr), sizeof(addr)) == SOCKET_ERROR)
            {
                ec = std::make_error_code(std::errc::address_in_use);
                std::cerr << "Bind failed with error: " << WSAGetLastError() << std::endl;
                closesocket(listenSocket_);
                listenSocket_ = INVALID_SOCKET;
                return false;
            }

            // 设置 IOCP
            if (CreateIoCompletionPort(reinterpret_cast<HANDLE>(listenSocket_), iocpHandle_, 0, 0) == nullptr)
            {
                ec = std::make_error_code(std::errc::io_error);
                std::cerr << "CreateIoCompletionPort failed with error: " << GetLastError() << std::endl;
                closesocket(listenSocket_);
                listenSocket_ = INVALID_SOCKET;
                return false;
            }

            // 启动监听
            if (listen(listenSocket_, SOMAXCONN) == SOCKET_ERROR)
            {
                ec = std::make_error_code(std::errc::operation_not_supported);
                std::cerr << "Listen failed with error: " << WSAGetLastError() << std::endl;
                closesocket(listenSocket_);
                listenSocket_ = INVALID_SOCKET;
                return false;
            }

            return true;
        }

        // 接受连接
        std::optional<TcpStream> accept(std::error_code& ec)
        {
            if (listenSocket_ == INVALID_SOCKET)
            {
                ec = std::make_error_code(std::errc::bad_file_descriptor);
                return std::nullopt;
            }

            // 创建新的 Socket 用于客户端连接
            SOCKET clientSocket = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
            if (clientSocket == INVALID_SOCKET)
            {
                ec = std::make_error_code(std::errc::io_error);
                return std::nullopt;
            }

            AcceptOverlapped* overlapped = new AcceptOverlapped();
            memset(overlapped, 0, sizeof(AcceptOverlapped));

            overlapped->clientSocket = clientSocket;
            overlapped->clientAddrLen = sizeof(overlapped->clientAddr);

            // 异步等待客户端连接
            int addrLen = sizeof(sockaddr_in) + 16;
            std::vector<char> buffer(2 * addrLen);

            int result = AcceptEx(
                listenSocket_, clientSocket,
                buffer.data(), 0,
                addrLen, addrLen,
                NULL, &overlapped->overlapped
            );

            if (result == SOCKET_ERROR && WSAGetLastError() != WSA_IO_PENDING)
            {
                ec = std::make_error_code(std::errc::io_error);
                std::cerr << "AcceptEx failed with error: " << WSAGetLastError() << std::endl;
                closesocket(clientSocket);
                delete overlapped;
                return std::nullopt;
            }

            // 使用 IOCP 等待连接完成
            DWORD bytesTransferred;
            ULONG_PTR completionKey;
            LPOVERLAPPED pOverlapped;
            BOOL success = GetQueuedCompletionStatus(iocpHandle_, &bytesTransferred, &completionKey, &pOverlapped, INFINITE);

            if (!success)
            {
                ec = std::make_error_code(std::errc::io_error);
                std::cerr << "GetQueuedCompletionStatus failed with error: " << GetLastError() << std::endl;
                closesocket(clientSocket);
                delete overlapped;
                return std::nullopt;
            }

            // 获取客户端套接字
            clientSocket = overlapped->clientSocket;
            delete overlapped;

            // 如果接收到连接，返回 TcpStream
            return TcpStream(clientSocket);
        }

    private:
        HANDLE iocpHandle_ = INVALID_HANDLE_VALUE;
        SOCKET listenSocket_ = INVALID_SOCKET;
    };

} // namespace net

#endif // WINDOWS_TCP_LISTENER_H
