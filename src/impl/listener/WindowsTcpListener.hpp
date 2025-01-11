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

#pragma comment(lib, "ws2_32.lib")

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
        Impl(HANDLE iocpHandle) : iocpHandle_(iocpHandle), listenSocket_(INVALID_SOCKET) {}

        ~Impl()
        {
            if (listenSocket_ != INVALID_SOCKET)
            {
                closesocket(listenSocket_);
            }
        }

        // 绑定地址和端口
        bool bind(const std::string &address, int port, std::error_code &ec)
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

            // 设置地址和端口
            sockaddr_in addr = {};
            addr.sin_family = AF_INET;
            addr.sin_port = htons(port);
            addr.sin_addr.s_addr = inet_addr(address.c_str());

            // 绑定到指定地址和端口
            if (::bind(listenSocket_, reinterpret_cast<SOCKADDR *>(&addr), sizeof(addr)) == SOCKET_ERROR)
            {
                ec = std::make_error_code(std::errc::address_in_use);
                closesocket(listenSocket_);
                listenSocket_ = INVALID_SOCKET;
                return false;
            }

            // 设置 IOCP
            if (CreateIoCompletionPort(reinterpret_cast<HANDLE>(listenSocket_), iocpHandle_, 0, 0) == nullptr)
            {
                ec = std::make_error_code(std::errc::io_error);
                closesocket(listenSocket_);
                listenSocket_ = INVALID_SOCKET;
                return false;
            }

            // 启动监听
            if (listen(listenSocket_, SOMAXCONN) == SOCKET_ERROR)
            {
                ec = std::make_error_code(std::errc::operation_not_supported);
                closesocket(listenSocket_);
                listenSocket_ = INVALID_SOCKET;
                return false;
            }

            return true;
        }

        // 接受连接
        std::optional<TcpStream> accept(std::error_code &ec)
        {
            if (listenSocket_ == INVALID_SOCKET)
            {
                ec = std::make_error_code(std::errc::bad_file_descriptor);
                return std::nullopt;
            }

            // 创建新的 Socket 用于客户端连接
            SOCKET clientSocket = INVALID_SOCKET;
            AcceptOverlapped *overlapped = new AcceptOverlapped();
            memset(overlapped, 0, sizeof(AcceptOverlapped));

            overlapped->clientSocket = INVALID_SOCKET;
            overlapped->clientAddrLen = sizeof(overlapped->clientAddr);

            // 异步等待客户端连接
            DWORD flags = 0;
            int result = AcceptEx(listenSocket_, overlapped->clientSocket,
                                  reinterpret_cast<LPVOID>(overlapped->clientAddr), 0, sizeof(overlapped->clientAddr), sizeof(overlapped->clientAddr), NULL, &overlapped->overlapped);
            if (result == SOCKET_ERROR && WSAGetLastError() != WSA_IO_PENDING)
            {
                ec = std::make_error_code(std::errc::io_error);
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
                delete overlapped;
                return std::nullopt;
            }

            // 获取客户端套接字
            clientSocket = overlapped->clientSocket;
            delete overlapped;

            // 如果接收到连接，返回 TcpStream
            return TcpStream(new TcpStream::Impl(clientSocket, iocpHandle_));
        }

    private:
        HANDLE iocpHandle_ = INVALID_HANDLE_VALUE;
        SOCKET listenSocket_ = INVALID_SOCKET;
    };

} // namespace net

#endif // WINDOWS_TCP_LISTENER_H
