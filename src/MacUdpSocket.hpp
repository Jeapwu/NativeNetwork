#include "UdpSocket.h"
#include <arpa/inet.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <cstring>
#include <stdexcept>
#include <system_error>
#include <iostream>

namespace net
{

    class UdpSocket::Impl
    {
    public:
        Impl() : socket_fd_(-1) {}

        ~Impl()
        {
            if (socket_fd_ != -1)
            {
                close(socket_fd_);
            }
        }

        // 绑定到指定地址和端口
        bool bind(const std::string &address, int port, std::error_code &ec)
        {
            socket_fd_ = ::socket(AF_INET, SOCK_DGRAM, 0);
            if (socket_fd_ == -1)
            {
                ec.assign(errno, std::system_category());
                return false;
            }

            sockaddr_in addr{};
            addr.sin_family = AF_INET;
            addr.sin_port = htons(port);

            if (address == "0.0.0.0" || address == "*")
            {
                addr.sin_addr.s_addr = INADDR_ANY; // 绑定所有网络接口
            }
            else
            {
                if (::inet_pton(AF_INET, address.c_str(), &addr.sin_addr) <= 0)
                {
                    ec.assign(errno, std::system_category());
                    close(socket_fd_);
                    socket_fd_ = -1;
                    return false;
                }
            }

            if (::bind(socket_fd_, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) == -1)
            {
                ec.assign(errno, std::system_category());
                close(socket_fd_);
                socket_fd_ = -1;
                return false;
            }

            return true;
        }

        // 发送数据到指定目标
        size_t send_to(const std::vector<uint8_t> &data, const std::string &address, int port, std::error_code &ec)
        {
            sockaddr_in dest_addr{};
            dest_addr.sin_family = AF_INET;
            dest_addr.sin_port = htons(port);

            if (::inet_pton(AF_INET, address.c_str(), &dest_addr.sin_addr) <= 0)
            {
                ec.assign(errno, std::system_category());
                return 0;
            }

            ssize_t bytes_sent = ::sendto(socket_fd_, data.data(), data.size(), 0,
                                          reinterpret_cast<sockaddr *>(&dest_addr), sizeof(dest_addr));
            if (bytes_sent == -1)
            {
                ec.assign(errno, std::system_category());
                return 0;
            }

            return static_cast<size_t>(bytes_sent);
        }

        // 从远程地址接收数据
        size_t recv_from(std::vector<uint8_t> &buffer, std::string &address, int &port, std::error_code &ec)
        {
            sockaddr_in src_addr{};
            socklen_t addr_len = sizeof(src_addr);

            ssize_t bytes_received = ::recvfrom(socket_fd_, buffer.data(), buffer.size(), 0,
                                                reinterpret_cast<sockaddr *>(&src_addr), &addr_len);
            if (bytes_received == -1)
            {
                ec.assign(errno, std::system_category());
                return 0;
            }

            // 将接收到的源地址转换为字符串
            char ip_str[INET_ADDRSTRLEN];
            if (::inet_ntop(AF_INET, &src_addr.sin_addr, ip_str, sizeof(ip_str)) != nullptr)
            {
                address = ip_str;
                port = ntohs(src_addr.sin_port);
            }

            return static_cast<size_t>(bytes_received);
        }

    private:
        int socket_fd_;
    };

    // 默认构造函数
    UdpSocket::UdpSocket() : impl_(new Impl()) {}

    // 析构函数
    UdpSocket::~UdpSocket()
    {
        delete impl_;
    }

    // 移动构造函数
    UdpSocket::UdpSocket(UdpSocket &&other) noexcept : impl_(other.impl_)
    {
        other.impl_ = nullptr;
    }

    // 移动赋值运算符
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

    // 绑定到本地地址和端口
    std::optional<UdpSocket> UdpSocket::bind(const std::string &address, int port, std::error_code &ec)
    {
        UdpSocket socket;
        if (socket.impl_->bind(address, port, ec))
        {
            return socket;
        }
        return std::nullopt;
    }

    // 发送数据到目标地址
    size_t UdpSocket::send_to(const std::vector<uint8_t> &data, const std::string &address, int port, std::error_code &ec)
    {
        return impl_->send_to(data, address, port, ec);
    }

    // 从远程地址接收数据
    size_t UdpSocket::recv_from(std::vector<uint8_t> &buffer, std::string &address, int &port, std::error_code &ec)
    {
        return impl_->recv_from(buffer, address, port, ec);
    }

} // namespace net
