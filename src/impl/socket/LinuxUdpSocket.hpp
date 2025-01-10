#ifndef LINUX_UDP_SOCKET_H
#define LINUX_UDP_SOCKET_H

#include <liburing.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>
#include <stdexcept>
#include <system_error>

namespace net
{
    // UdpSocket::Impl for Linux with io_uring
    class UdpSocket::Impl
    {
    public:
        Impl(int socket_fd, io_uring *ring) : socket_fd_(socket_fd), ring_(ring) {}

        ~Impl()
        {
            if (socket_fd_ >= 0)
                close(socket_fd_);
            if (ring_)
            {
                io_uring_queue_exit(ring_);
                delete ring_;
            }
        }

        bool bind(const std::string &address, int port, std::error_code &ec)
        {
            // 创建 io_uring 实例
            auto *ring = new io_uring();
            if (io_uring_queue_init(32, ring, 0) < 0)
            {
                ec = std::make_error_code(std::errc::io_error);
                delete ring;
                return false;
            }

            // 创建 socket
            int socket_fd = ::socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK, 0);
            if (socket_fd < 0)
            {
                ec = std::make_error_code(std::errc::address_family_not_supported);
                io_uring_queue_exit(ring);
                delete ring;
                return false;
            }

            sockaddr_in local_addr = {};
            local_addr.sin_family = AF_INET;
            local_addr.sin_port = htons(port);
            if (inet_pton(AF_INET, address.c_str(), &local_addr.sin_addr) <= 0)
            {
                ec = std::make_error_code(std::errc::invalid_argument);
                close(socket_fd);
                io_uring_queue_exit(ring);
                delete ring;
                return false;
            }

            if (bind(socket_fd, reinterpret_cast<sockaddr *>(&local_addr), sizeof(local_addr)) < 0)
            {
                ec = std::make_error_code(std::errc::address_in_use);
                close(socket_fd);
                io_uring_queue_exit(ring);
                delete ring;
                return false;
            }

            // 返回 true 表示绑定成功
            impl_ = new Impl(socket_fd, ring);
            return true;
        }

        size_t UdpSocket::send_to(const std::vector<uint8_t> &data, const std::string &address, int port, std::error_code &ec)
        {
            if (!impl_)
            {
                ec = std::make_error_code(std::errc::bad_file_descriptor);
                return 0;
            }

            sockaddr_in remote_addr = {};
            remote_addr.sin_family = AF_INET;
            remote_addr.sin_port = htons(port);
            if (inet_pton(AF_INET, address.c_str(), &remote_addr.sin_addr) <= 0)
            {
                ec = std::make_error_code(std::errc::invalid_argument);
                return 0;
            }

            // 使用 io_uring 提交异步发送请求
            io_uring_sqe *sqe = io_uring_get_sqe(impl_->ring_);
            io_uring_prep_sendto(sqe, impl_->socket_fd_, data.data(), data.size(), 0,
                                 reinterpret_cast<sockaddr *>(&remote_addr), sizeof(remote_addr));
            io_uring_submit(impl_->ring_);

            // 等待发送完成
            io_uring_cqe *cqe;
            if (io_uring_wait_cqe(impl_->ring_, &cqe) < 0)
            {
                ec = std::make_error_code(std::errc::io_error);
                return 0;
            }

            if (cqe->res < 0)
            {
                ec = std::make_error_code(std::errc::io_error);
                io_uring_cqe_seen(impl_->ring_, cqe);
                return 0;
            }

            size_t bytes_sent = cqe->res;
            io_uring_cqe_seen(impl_->ring_, cqe);
            return bytes_sent;
        }

        size_t UdpSocket::receive_from(std::vector<uint8_t> &buffer, std::string &sender_address, int &sender_port, std::error_code &ec)
        {
            if (!impl_)
            {
                ec = std::make_error_code(std::errc::bad_file_descriptor);
                return 0;
            }

            sockaddr_in sender_addr = {};
            socklen_t addr_len = sizeof(sender_addr);

            // 使用 io_uring 提交异步接收请求
            io_uring_sqe *sqe = io_uring_get_sqe(impl_->ring_);
            io_uring_prep_recvfrom(sqe, impl_->socket_fd_, buffer.data(), buffer.size(), 0,
                                   reinterpret_cast<sockaddr *>(&sender_addr), &addr_len);
            io_uring_submit(impl_->ring_);

            // 等待接收完成
            io_uring_cqe *cqe;
            if (io_uring_wait_cqe(impl_->ring_, &cqe) < 0)
            {
                ec = std::make_error_code(std::errc::io_error);
                return 0;
            }

            if (cqe->res < 0)
            {
                ec = std::make_error_code(std::errc::io_error);
                io_uring_cqe_seen(impl_->ring_, cqe);
                return 0;
            }

            size_t bytes_received = cqe->res;
            io_uring_cqe_seen(impl_->ring_, cqe);

            // 提取发送者地址和端口
            char addr_str[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &sender_addr.sin_addr, addr_str, sizeof(addr_str));
            sender_address = addr_str;
            sender_port = ntohs(sender_addr.sin_port);

            return bytes_received;
        }

    private:
        int socket_fd_;
        io_uring *ring_;
    };

    // 默认构造和析构函数
    UdpSocket::UdpSocket() : impl_(nullptr) {}

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
        if (!impl_)
        {
            ec = std::make_error_code(std::errc::bad_file_descriptor);
            return 0;
        }
        return impl_->send_to(data, address, port, ec);
    }

    // 从远程地址接收数据
    size_t UdpSocket::recv_from(std::vector<uint8_t> &buffer, std::string &address, int &port, std::error_code &ec)
    {
        if (!impl_)
        {
            ec = std::make_error_code(std::errc::bad_file_descriptor);
            return 0;
        }
        return impl_->recv_from(buffer, address, port, ec);
    }

} // namespace net

#endif /// LINUX_UDP_SOCKET_H
