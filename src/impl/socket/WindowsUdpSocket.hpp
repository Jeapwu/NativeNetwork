#include <winsock2.h>
#include <ws2tcpip.h>
#include <mswsock.h>
#include <windows.h>
#include <iostream>
#include <vector>
#include <thread>
#include <atomic>
#include <stdexcept>
#include <optional>
#include <system_error>
#include "network_lib.h"

#pragma comment(lib, "ws2_32.lib")

namespace net
{

    // IOCP Completion Key
    struct IOCPKey
    {
        enum class Type
        {
            Send,
            Receive
        } type;
        OVERLAPPED overlapped;
        std::vector<uint8_t> buffer;
        sockaddr_storage remote_addr;
        int remote_addr_len;
    };

    class UdpSocket::Impl
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

        void send_to(const std::vector<uint8_t> &data, const std::string &address, int port)
        {
            sockaddr_in remote_addr = {};
            remote_addr.sin_family = AF_INET;
            remote_addr.sin_port = htons(port);
            if (inet_pton(AF_INET, address.c_str(), &remote_addr.sin_addr) <= 0)
            {
                throw std::runtime_error("Invalid remote address");
            }

            auto *key = new IOCPKey{IOCPKey::Type::Send, {}, data, {}, 0};

            WSABUF wsabuf = {static_cast<ULONG>(data.size()), reinterpret_cast<char *>(key->buffer.data())};
            DWORD bytes_sent = 0;
            int result = WSASendTo(socket_, &wsabuf, 1, &bytes_sent, 0,
                                   reinterpret_cast<sockaddr *>(&remote_addr), sizeof(remote_addr),
                                   &key->overlapped, nullptr);

            if (result == SOCKET_ERROR && WSAGetLastError() != WSA_IO_PENDING)
            {
                delete key;
                throw std::system_error(WSAGetLastError(), std::system_category(), "WSASendTo failed");
            }
        }

        std::optional<std::pair<std::vector<uint8_t>, sockaddr_in>> recv_from()
        {
            sockaddr_storage remote_addr = {};
            int remote_addr_len = sizeof(remote_addr);

            auto *key = new IOCPKey{IOCPKey::Type::Receive, {}, std::vector<uint8_t>(1024), {}, 0};

            WSABUF wsabuf = {static_cast<ULONG>(key->buffer.size()), reinterpret_cast<char *>(key->buffer.data())};
            DWORD flags = 0;
            int result = WSARecvFrom(socket_, &wsabuf, 1, nullptr, &flags,
                                     reinterpret_cast<sockaddr *>(&key->remote_addr), &remote_addr_len,
                                     &key->overlapped, nullptr);

            if (result == SOCKET_ERROR && WSAGetLastError() != WSA_IO_PENDING)
            {
                delete key;
                throw std::system_error(WSAGetLastError(), std::system_category(), "WSARecvFrom failed");
            }

            return std::nullopt; // 实际上需要通过回调处理收到的数据
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

                auto *key = reinterpret_cast<IOCPKey *>(completion_key);
                if (key->type == IOCPKey::Type::Send)
                {
                    std::cout << "Sent " << bytes_transferred << " bytes." << std::endl;
                }
                else if (key->type == IOCPKey::Type::Receive)
                {
                    std::cout << "Received " << bytes_transferred << " bytes from "
                              << inet_ntoa(reinterpret_cast<sockaddr_in *>(&key->remote_addr)->sin_addr) << std::endl;
                }

                delete key;
            }
        }
    };

    UdpSocket::UdpSocket() : impl_(nullptr) {}

    UdpSocket::~UdpSocket() { delete impl_; }

    UdpSocket::UdpSocket(UdpSocket &&other) noexcept : impl_(other.impl_)
    {
        other.impl_ = nullptr;
    }

    UdpSocket &UdpSocket::operator=(UdpSocket &&other) noexcept
    {
        if (this != &other)
        {
            delete impl_;
            impl_ = other.impl_;
            other.impl_ = nullptr;
        }
        return *this;
    }

    UdpSocket UdpSocket::bind(const std::string &address, int port)
    {
        WSADATA wsa_data;
        if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0)
        {
            throw std::system_error(WSAGetLastError(), std::system_category(), "WSAStartup failed");
        }

        SOCKET socket_fd = WSASocket(AF_INET, SOCK_DGRAM, IPPROTO_UDP, nullptr, 0, WSA_FLAG_OVERLAPPED);
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

        auto udp_socket = UdpSocket();
        udp_socket.impl_ = new Impl(socket_fd, iocp);
        udp_socket.impl_->bind(address, port);

        return udp_socket;
    }

    void UdpSocket::send_to(const std::vector<uint8_t> &data, const std::string &address, int port)
    {
        impl_->send_to(data, address, port);
    }

    std::optional<std::pair<std::vector<uint8_t>, sockaddr_in>> UdpSocket::recv_from()
    {
        return impl_->recv_from();
    }

} // namespace net
