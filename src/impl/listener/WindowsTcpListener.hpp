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
    class TcpListener::Impl
    {
    public:
        TcpListener() : iocp_(nullptr), running_(false) {}

        ~Impl()
        {
            if (running_)
            {
                running_ = false;
                if (worker_thread_.joinable())
                    worker_thread_.join();
            }

            if (iocp_)
            {
                CloseHandle(iocp_);
                iocp_ = nullptr;
            }

            if (listener_ != INVALID_SOCKET)
            {
                closesocket(listener_);
                listener_ = INVALID_SOCKET;
            }
        }

        bool bind(const std::string &address, int port, std::error_code &ec)
        {
            // 创建监听 socket
            listener_ = ::socket(AF_INET, SOCK_STREAM, 0);
            if (listener_ == INVALID_SOCKET)
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
                closesocket(listener_);
                listener_ = INVALID_SOCKET;
                return false;
            }

            if (::bind(listener_, reinterpret_cast<sockaddr *>(&local_addr), sizeof(local_addr)) == SOCKET_ERROR)
            {
                ec = std::make_error_code(static_cast<std::errc>(WSAGetLastError()));
                closesocket(listener_);
                listener_ = INVALID_SOCKET;
                return false;
            }

            // 创建 IOCP
            iocp_ = CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, 0);
            if (!iocp_)
            {
                ec = std::make_error_code(static_cast<std::errc>(GetLastError()));
                closesocket(listener_);
                listener_ = INVALID_SOCKET;
                return false;
            }

            // 关联 listener socket 到 IOCP
            if (!CreateIoCompletionPort(reinterpret_cast<HANDLE>(listener_), iocp_, 0, 0))
            {
                ec = std::make_error_code(static_cast<std::errc>(GetLastError()));
                closesocket(listener_);
                listener_ = INVALID_SOCKET;
                CloseHandle(iocp_);
                iocp_ = nullptr;
                return false;
            }

            // 启动 IOCP 工作线程
            running_ = true;
            worker_thread_ = std::thread(&TcpListener::iocp_worker, this);

            return true;
        }

        void TcpListener::listen(int backlog)
        {
            if (::listen(listener_, backlog) == SOCKET_ERROR)
            {
                throw std::system_error(WSAGetLastError(), std::system_category(), "Listen failed");
            }
        }

        std::optional<TcpStream> TcpListener::accept(std::error_code &ec)
        {
            char accept_buffer[(sizeof(sockaddr_in) + 16) * 2] = {};
            auto *overlapped = new OVERLAPPED{};

            SOCKET client_socket = ::socket(AF_INET, SOCK_STREAM, 0);
            if (client_socket == INVALID_SOCKET)
            {
                ec = std::make_error_code(static_cast<std::errc>(WSAGetLastError()));
                delete overlapped;
                return std::nullopt;
            }

            DWORD bytes_received = 0;
            int result = AcceptEx(listener_, client_socket, accept_buffer, 0,
                                  sizeof(sockaddr_in) + 16, sizeof(sockaddr_in) + 16,
                                  &bytes_received, overlapped);

            if (result == SOCKET_ERROR && WSAGetLastError() != WSA_IO_PENDING)
            {
                delete overlapped;
                closesocket(client_socket);
                ec = std::make_error_code(static_cast<std::errc>(WSAGetLastError()));
                return std::nullopt;
            }

            // 等待 IOCP 事件完成
            DWORD transferred_bytes = 0;
            ULONG_PTR completion_key = 0;
            OVERLAPPED *completed_overlapped = nullptr;

            BOOL success = GetQueuedCompletionStatus(iocp_, &transferred_bytes, &completion_key, &completed_overlapped, INFINITE);
            if (!success || completed_overlapped != overlapped)
            {
                delete overlapped;
                closesocket(client_socket);
                ec = std::make_error_code(static_cast<std::errc>(GetLastError()));
                return std::nullopt;
            }

            delete overlapped;
            return TcpStream(client_socket);
        }

    private:
        SOCKET listener_ = INVALID_SOCKET;
        HANDLE iocp_ = nullptr;
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
                if (overlapped)
                {
                    // 如果是 AcceptEx 完成
                    if (completion_key == 1)
                    {
                        // 这里我们已经在 accept() 中处理了 AcceptEx 的完成
                    }
                    // 如果是 Send 操作完成
                    else if (completion_key == 2)
                    {
                        std::cout << "Send operation completed, bytes transferred: " << bytes_transferred << std::endl;
                    }
                    // 如果是 Receive 操作完成
                    else if (completion_key == 3)
                    {
                        std::cout << "Receive operation completed, bytes received: " << bytes_transferred << std::endl;
                    }
                }
            }
        }
    };

} // namespace net
