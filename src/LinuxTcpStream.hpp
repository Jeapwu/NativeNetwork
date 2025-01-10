#ifndef LINUX_TCP_STREAM_H
#define LINUX_TCP_STREAM_H

#include "TcpStream.hpp"
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

    // TcpStream::Impl for Linux with io_uring
    class TcpStream::Impl
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

        std::optional<TcpStream> connect(const std::string &address, int port, std::error_code &ec)
        {
            // 创建 io_uring 实例
            auto *ring = new io_uring();
            if (io_uring_queue_init(32, ring, 0) < 0)
            {
                ec = std::make_error_code(std::errc::io_error);
                delete ring;
                return std::nullopt;
            }

            // 创建 socket
            int socket_fd = ::socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
            if (socket_fd < 0)
            {
                ec = std::make_error_code(std::errc::address_family_not_supported);
                io_uring_queue_exit(ring);
                delete ring;
                return std::nullopt;
            }

            sockaddr_in server_addr = {};
            server_addr.sin_family = AF_INET;
            server_addr.sin_port = htons(port);
            if (inet_pton(AF_INET, address.c_str(), &server_addr.sin_addr) <= 0)
            {
                ec = std::make_error_code(std::errc::invalid_argument);
                close(socket_fd);
                io_uring_queue_exit(ring);
                delete ring;
                return std::nullopt;
            }

            // 使用 io_uring 提交异步连接请求
            io_uring_sqe *sqe = io_uring_get_sqe(ring);
            io_uring_prep_connect(sqe, socket_fd, reinterpret_cast<sockaddr *>(&server_addr), sizeof(server_addr));
            io_uring_submit(ring);

            // 等待连接完成
            io_uring_cqe *cqe;
            if (io_uring_wait_cqe(ring, &cqe) < 0)
            {
                ec = std::make_error_code(std::errc::io_error);
                close(socket_fd);
                io_uring_queue_exit(ring);
                delete ring;
                return std::nullopt;
            }

            if (cqe->res < 0)
            {
                ec = std::make_error_code(std::errc::connection_refused);
                close(socket_fd);
                io_uring_cqe_seen(ring, cqe);
                io_uring_queue_exit(ring);
                delete ring;
                return std::nullopt;
            }

            io_uring_cqe_seen(ring, cqe);
            return TcpStream(new Impl(socket_fd, ring));
        }

        size_t write(const std::vector<uint8_t> &data, std::error_code &ec)
        {
            if (!impl_)
            {
                ec = std::make_error_code(std::errc::bad_file_descriptor);
                return 0;
            }

            // 使用 io_uring 提交异步写入请求
            io_uring_sqe *sqe = io_uring_get_sqe(impl_->ring_);
            io_uring_prep_write(sqe, impl_->socket_fd_, data.data(), data.size(), 0);
            io_uring_submit(impl_->ring_);

            // 等待写入完成
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

            size_t bytes_written = cqe->res;
            io_uring_cqe_seen(impl_->ring_, cqe);
            return bytes_written;
        }

        size_t read(std::vector<uint8_t> &buffer, std::error_code &ec)
        {
            if (!impl_)
            {
                ec = std::make_error_code(std::errc::bad_file_descriptor);
                return 0;
            }

            // 使用 io_uring 提交异步读取请求
            io_uring_sqe *sqe = io_uring_get_sqe(impl_->ring_);
            io_uring_prep_read(sqe, impl_->socket_fd_, buffer.data(), buffer.size(), 0);
            io_uring_submit(impl_->ring_);

            // 等待读取完成
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

            size_t bytes_read = cqe->res;
            io_uring_cqe_seen(impl_->ring_, cqe);
            return bytes_read;
        }

    private:
        int socket_fd_;
        io_uring *ring_;
    };

    // TcpStream 类的构造和析构
    TcpStream::TcpStream() : impl_(nullptr) {}

    TcpStream::~TcpStream()
    {
        delete impl_;
    }

    // 移动构造和赋值
    TcpStream::TcpStream(TcpStream &&other) noexcept : impl_(other.impl_)
    {
        other.impl_ = nullptr;
    }

    TcpStream &TcpStream::operator=(TcpStream &&other) noexcept
    {
        if (this != &other)
        {
            delete impl_;
            impl_ = other.impl_;
            other.impl_ = nullptr;
        }
        return *this;
    }

    // 连接到远程主机
    std::optional<TcpStream> TcpStream::connect(const std::string &address, int port, std::error_code &ec)
    {
        TcpStream stream;
        if (stream.impl_->connect(address, port, ec))
        {
            return stream;
        }
        return std::nullopt;
    }

    // 写数据
    size_t TcpStream::write(const std::vector<uint8_t> &data, std::error_code &ec)
    {
        if (!impl_)
        {
            ec = std::make_error_code(std::errc::bad_file_descriptor);
            return 0;
        }
        return impl_->write(data, ec);
    }

    // 读数据
    size_t TcpStream::read(std::vector<uint8_t> &buffer, std::error_code &ec)
    {
        if (!impl_)
        {
            ec = std::make_error_code(std::errc::bad_file_descriptor);
            return 0;
        }
        return impl_->read(buffer, ec);
    }

} // namespace net

#endif // LINUX_TCP_STREAM_H
