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

        // 绑定到本地地址和端口
        bool bind(const std::string &address, int port, std::error_code &ec)
        {
            sockaddr_in local_addr = {};
            local_addr.sin_family = AF_INET;
            local_addr.sin_port = htons(port);
            if (inet_pton(AF_INET, address.c_str(), &local_addr.sin_addr) <= 0)
            {
                ec = std::make_error_code(std::errc::invalid_argument);
                return false;
            }

            if (::bind(socket_, reinterpret_cast<sockaddr *>(&local_addr), sizeof(local_addr)) == SOCKET_ERROR)
            {
                ec = std::make_error_code(static_cast<std::errc>(WSAGetLastError()));
                return false;
            }

            return true;
        }

        // 发送数据到目标地址
        size_t send_to(const std::vector<uint8_t> &data, const std::string &address, int port, std::error_code &ec)
        {
            sockaddr_in remote_addr = {};
            remote_addr.sin_family = AF_INET;
            remote_addr.sin_port = htons(port);
            if (inet_pton(AF_INET, address.c_str(), &remote_addr.sin_addr) <= 0)
            {
                ec = std::make_error_code(std::errc::invalid_argument);
                return 0;
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
                ec = std::make_error_code(static_cast<std::errc>(WSAGetLastError()));
                return 0;
            }

            return bytes_sent;
        }

        // 从远程地址接收数据
        std::optional<std::pair<std::vector<uint8_t>, sockaddr_in>> recv_from(std::error_code &ec)
        {
            sockaddr_in remote_addr = {};
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
                ec = std::make_error_code(static_cast<std::errc>(WSAGetLastError()));
                return std::nullopt;
            }

            // 此处需要通过回调处理实际接收到的数据，这里仅模拟返回
            return std::nullopt;
        }

    private:
        SOCKET socket_;
        HANDLE iocp_;
        std::atomic<bool> running_;
        std::thread worker_thread_;

        // IOCP 工作线程，处理完成的异步操作
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

} // namespace net