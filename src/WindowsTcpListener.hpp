#include <winsock2.h>
#include <mswsock.h>
#include <windows.h>
#include <iostream>
#include <vector>
#include <thread>
#include <atomic>
#include <system_error>
#include <optional>
#include "network_lib.h"

#pragma comment(lib, "ws2_32.lib")

namespace net
{

    class TcpListener::Impl
    {
    public:
        Impl(SOCKET socket, HANDLE iocp) : socket_(socket), iocp_(iocp), running_(true)
        {
            worker_thread_ = std::thread(&Impl::iocp_worker, this);
        }

        ~Impl()
        {
            running_ = false;
            if (worker_thread_.joinable())
                worker_thread_.join();
            if (socket_ != INVALID_SOCKET)
                closesocket(socket_);
            if (iocp_ != nullptr)
                CloseHandle(iocp_);
        }

        void bind(const std::string &address, int port)
        {
            sockaddr_in local_addr = {};
            local_addr.sin_family = AF_INET;
            local_addr.sin_port = htons(port);
            if (inet_pton(AF_INET, address.c_str(), &local_addr.sin_addr) <= 0)
            {
                throw std::runtime_error("Invalid address");
            }

            if (::bind(socket_, reinterpret_cast<sockaddr *>(&local_addr), sizeof(local_addr)) == SOCKET_ERROR)
            {
                throw std::system_error(WSAGetLastError(), std::system_category(), "Bind failed");
            }
        }

        void listen(int backlog = SOMAXCONN)
        {
            if (::listen(socket_, backlog) == SOCKET_ERROR)
            {
                throw std::system_error(WSAGetLastError(), std::system_category(), "Listen failed");
            }
        }

        std::optional<TcpStream> accept()
        {
            // Allocate buffers for AcceptEx
            char accept_buffer[(sizeof(sockaddr_in) + 16) * 2] = {};
            auto *overlapped = new OVERLAPPED{};

            DWORD bytes_received = 0;
            int result = AcceptEx(socket_, INVALID_SOCKET, accept_buffer, 0,
                                  sizeof(sockaddr_in) + 16, sizeof(sockaddr_in) + 16,
                                  &bytes_received, overlapped);

            if (result == SOCKET_ERROR && WSAGetLastError() != WSA_IO_PENDING)
            {
                delete overlapped;
                throw std::system_error(WSAGetLastError(), std::system_category(), "AcceptEx failed");
            }

            // 等待 IOCP 事件完成
            DWORD transferred_bytes = 0;
            ULONG_PTR completion_key = 0;
            OVERLAPPED *completed_overlapped = nullptr;

            BOOL success = GetQueuedCompletionStatus(iocp_, &transferred_bytes, &completion_key, &completed_overlapped, INFINITE);
            if (!success || completed_overlapped != overlapped)
            {
                delete overlapped;
                throw std::system_error(GetLastError(), std::system_category(), "AcceptEx failed to complete");
            }

            // Extract client socket from AcceptEx buffer
            SOCKET client_socket = reinterpret_cast<SOCKET>(completion_key);
            if (client_socket == INVALID_SOCKET)
            {
                delete overlapped;
                throw std::system_error(WSAGetLastError(), std::system_category(), "Invalid client socket");
            }

            // Return TcpStream wrapping the client socket
            return TcpStream(client_socket);
        }

    private:
        SOCKET socket_;
        HANDLE iocp_;
        std::atomic<bool> running_;
        std::thread worker_thread_;

        void iocp_worker()
        {
            while (running_)
            {
                DWORD bytes_transferred = 0;
                ULONG_PTR completion_key = 0;
                OVERLAPPED *overlapped = nullptr;

                BOOL result = GetQueuedCompletionStatus(iocp_, &bytes_transferred, &completion_key, &overlapped, INFINITE);
                if (!result)
                {
                    std::cerr << "IOCP error: " << GetLastError() << std::endl;
                    continue;
                }

                // Handle the IOCP completion event (e.g., AcceptEx, Send, Receive)
            }
        }
    };

    TcpListener::TcpListener() : impl_(nullptr) {}

    TcpListener::~TcpListener() { delete impl_; }

    TcpListener::TcpListener(TcpListener &&other) noexcept : impl_(other.impl_)
    {
        other.impl_ = nullptr;
    }

    TcpListener &TcpListener::operator=(TcpListener &&other) noexcept
    {
        if (this != &other)
        {
            delete impl_;
            impl_ = other.impl_;
            other.impl_ = nullptr;
        }
        return *this;
    }

    TcpListener TcpListener::bind(const std::string &address, int port)
    {
        WSADATA wsa_data;
        if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0)
        {
            throw std::system_error(WSAGetLastError(), std::system_category(), "WSAStartup failed");
        }

        SOCKET socket_fd = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, nullptr, 0, WSA_FLAG_OVERLAPPED);
        if (socket_fd == INVALID_SOCKET)
        {
            throw std::system_error(WSAGetLastError(), std::system_category(), "Socket creation failed");
        }

        HANDLE iocp = CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, 0);
        if (iocp == nullptr)
        {
            closesocket(socket_fd);
            throw std::system_error(GetLastError(), std::system_category(), "IOCP creation failed");
        }

        if (CreateIoCompletionPort(reinterpret_cast<HANDLE>(socket_fd), iocp, 0, 0) == nullptr)
        {
            CloseHandle(iocp);
            closesocket(socket_fd);
            throw std::system_error(GetLastError(), std::system_category(), "Socket association with IOCP failed");
        }

        auto listener = TcpListener();
        listener.impl_ = new Impl(socket_fd, iocp);
        listener.impl_->bind(address, port);
        listener.impl_->listen();

        return listener;
    }

    std::optional<TcpStream> TcpListener::accept()
    {
        return impl_->accept();
    }

} // namespace net
