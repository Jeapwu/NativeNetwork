#ifndef WINDOWS_UDP_SOCKET_H
#define WINDOWS_UDP_SOCKET_H

#include <winsock2.h>
#include <ws2tcpip.h>
#include <iostream>
#include <thread>
#include <atomic>
#include <vector>
#include <string>
#include <system_error>
#include <memory>
#include <mutex>
#include "UdpSocket.h"

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
        OVERLAPPED overlapped{};
        std::vector<uint8_t> buffer;
        sockaddr_storage remote_addr{};
        int remote_addr_len = 0;
    };

    class UdpSocket::Impl
    {
    public:
        Impl() : running_(true) {}

        ~Impl()
        {
            stop();
        }

        bool bind(const std::string& address, int port, std::error_code& ec)
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (socket_ != INVALID_SOCKET)
            {
                ec = std::make_error_code(std::errc::address_in_use);
                return false;
            }

            socket_ = ::socket(AF_INET, SOCK_DGRAM, 0);
            if (socket_ == INVALID_SOCKET)
            {
                ec = std::make_error_code(static_cast<std::errc>(WSAGetLastError()));
                return false;
            }

            sockaddr_in local_addr = {};
            local_addr.sin_family = AF_INET;
            local_addr.sin_port = htons(port);
            if (inet_pton(AF_INET, address.c_str(), &local_addr.sin_addr) <= 0)
            {
                ec = std::make_error_code(std::errc::invalid_argument);
                return false;
            }

            if (::bind(socket_, reinterpret_cast<sockaddr*>(&local_addr), sizeof(local_addr)) == SOCKET_ERROR)
            {
                ec = std::make_error_code(static_cast<std::errc>(WSAGetLastError()));
                return false;
            }

            iocp_ = CreateIoCompletionPort(reinterpret_cast<HANDLE>(socket_), nullptr, 0, 0);
            if (!iocp_)
            {
                ec = std::make_error_code(static_cast<std::errc>(GetLastError()));
                return false;
            }

            worker_thread_ = std::thread(&Impl::iocp_worker, this);
            return true;
        }

        size_t send_to(const std::vector<uint8_t>& data, const std::string& address, int port, std::error_code& ec)
        {
            ensure_initialized(ec);
            if (ec)
                return 0;

            sockaddr_in remote_addr = {};
            remote_addr.sin_family = AF_INET;
            remote_addr.sin_port = htons(port);
            if (inet_pton(AF_INET, address.c_str(), &remote_addr.sin_addr) <= 0)
            {
                ec = std::make_error_code(std::errc::invalid_argument);
                return 0;
            }

            auto* key = new IOCPKey{ IOCPKey::Type::Send, {}, data, {}, 0 };

            WSABUF wsabuf = { static_cast<ULONG>(data.size()), reinterpret_cast<char*>(key->buffer.data()) };
            DWORD bytes_sent = 0;
            int result = WSASendTo(socket_, &wsabuf, 1, &bytes_sent, 0,
                reinterpret_cast<sockaddr*>(&remote_addr), sizeof(remote_addr),
                &key->overlapped, nullptr);

            if (result == SOCKET_ERROR && WSAGetLastError() != WSA_IO_PENDING)
            {
                delete key;
                ec = std::make_error_code(static_cast<std::errc>(WSAGetLastError()));
                return 0;
            }

            return bytes_sent;
        }

        size_t recv_from(std::vector<uint8_t>& buffer, std::string& address, int& port, std::error_code& ec)
        {
            ensure_initialized(ec);
            if (ec)
                return 0;

            auto* key = new IOCPKey{ IOCPKey::Type::Receive, {}, std::vector<uint8_t>(buffer.size()), {}, sizeof(sockaddr_storage) };

            WSABUF wsabuf = { static_cast<ULONG>(key->buffer.size()), reinterpret_cast<char*>(key->buffer.data()) };
            DWORD flags = 0;

            int result = WSARecvFrom(socket_, &wsabuf, 1, nullptr, &flags,
                reinterpret_cast<sockaddr*>(&key->remote_addr),
                &key->remote_addr_len, &key->overlapped, nullptr);

            if (result == SOCKET_ERROR && WSAGetLastError() != WSA_IO_PENDING)
            {
                delete key;
                ec = std::make_error_code(static_cast<std::errc>(WSAGetLastError()));
                return 0;
            }

            DWORD bytes_transferred = 0;
            BOOL success = GetOverlappedResult(reinterpret_cast<HANDLE>(socket_), &key->overlapped, &bytes_transferred, TRUE);
            if (!success)
            {
                delete key;
                ec = std::make_error_code(static_cast<std::errc>(WSAGetLastError()));
                return 0;
            }

            buffer = std::move(key->buffer);

            sockaddr_in* remote_addr = reinterpret_cast<sockaddr_in*>(&key->remote_addr);
            char addr_buffer[INET_ADDRSTRLEN] = {};
            if (inet_ntop(AF_INET, &remote_addr->sin_addr, addr_buffer, sizeof(addr_buffer)) == nullptr)
            {
                delete key;
                ec = std::make_error_code(std::errc::address_not_available);
                return 0;
            }

            address = addr_buffer;
            port = ntohs(remote_addr->sin_port);

            delete key;
            return bytes_transferred;
        }

    private:
        SOCKET socket_ = INVALID_SOCKET;
        HANDLE iocp_ = nullptr;
        std::atomic<bool> running_;
        std::thread worker_thread_;
        std::mutex mutex_;

        void stop()
        {
            running_ = false;
            if (worker_thread_.joinable())
                worker_thread_.join();

            if (socket_ != INVALID_SOCKET)
            {
                closesocket(socket_);
                socket_ = INVALID_SOCKET;
            }

            if (iocp_ != nullptr)
            {
                CloseHandle(iocp_);
                iocp_ = nullptr;
            }
        }

        void iocp_worker()
        {
            while (running_)
            {
                DWORD bytes_transferred = 0;
                ULONG_PTR completion_key = 0;
                OVERLAPPED* overlapped = nullptr;

                BOOL result = GetQueuedCompletionStatus(iocp_, &bytes_transferred, &completion_key, &overlapped, INFINITE);

                if (!result)
                {
                    DWORD error_code = GetLastError();

                    // IOCP 被关闭时，返回 WAIT_TIMEOUT 或 ERROR_ABANDONED_WAIT_0，退出循环
                    if (error_code == WAIT_TIMEOUT || error_code == ERROR_ABANDONED_WAIT_0)
                    {
                        std::cerr << "IOCP closed or timed out. Stopping worker." << std::endl;
                        break;
                    }

                    // 如果 overlapped 为 nullptr，说明 IOCP 句柄被关闭或错误发生
                    if (overlapped == nullptr)
                    {
                        std::cerr << "IOCP critical error: " << error_code << std::endl;
                        continue;
                    }

                    // 其他错误，记录日志继续
                    std::cerr << "IOCP operation failed with error: " << error_code << std::endl;
                    continue;
                }

                // 确保 completion_key 和 overlapped 有效
                auto* key = reinterpret_cast<IOCPKey*>(completion_key);
                if (key == nullptr)
                {
                    //std::cerr << "Invalid completion key received." << std::endl;
                    continue;
                }

                if (key->type == IOCPKey::Type::Send)
                {
                    std::cout << "Sent " << bytes_transferred << " bytes." << std::endl;
                }
                else if (key->type == IOCPKey::Type::Receive)
                {
                    char address_buffer[INET6_ADDRSTRLEN];
                    auto* remote_addr = reinterpret_cast<sockaddr_in*>(&key->remote_addr);
                    inet_ntop(AF_INET, &remote_addr->sin_addr, address_buffer, sizeof(address_buffer));

                    std::cout << "Received " << bytes_transferred << " bytes from " << address_buffer << std::endl;
                }

                delete key;
            }
        }

        void ensure_initialized(std::error_code& ec)
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (socket_ != INVALID_SOCKET)
                return;

            socket_ = ::socket(AF_INET, SOCK_DGRAM, 0);
            if (socket_ == INVALID_SOCKET)
            {
                ec = std::make_error_code(static_cast<std::errc>(WSAGetLastError()));
                return;
            }

            sockaddr_in local_addr = {};
            local_addr.sin_family = AF_INET;
            local_addr.sin_port = htons(0);
            local_addr.sin_addr.s_addr = INADDR_ANY;

            if (::bind(socket_, reinterpret_cast<sockaddr*>(&local_addr), sizeof(local_addr)) == SOCKET_ERROR)
            {
                ec = std::make_error_code(static_cast<std::errc>(WSAGetLastError()));
            }
        } 
    };
} // namespace net

#endif // WINDOWS_UDP_SOCKET_H
