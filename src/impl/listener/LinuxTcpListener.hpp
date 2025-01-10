#ifndef LINUX_TCP_LISTENER_H
#define LINUX_TCP_LISTENER_H

#include <liburing.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <cstring>
#include <stdexcept>
#include <system_error>
#include "TcpListener.hpp"

namespace net
{
    // TcpListener::Impl for Linux with io_uring
    class TcpListener::Impl
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
            int socket_fd = ::socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
            if (socket_fd < 0)
            {
                ec = std::make_error_code(std::errc::address_family_not_supported);
                io_uring_queue_exit(ring);
                delete ring;
                return false;
            }

            int opt = 1;
            setsockopt(socket_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

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

            if (listen(socket_fd, SOMAXCONN) < 0)
            {
                ec = std::make_error_code(std::errc::io_error);
                close(socket_fd);
                io_uring_queue_exit(ring);
                delete ring;
                return false;
            }

            // 成功时，保存ring，并返回true
            socket_fd_ = socket_fd;
            ring_ = ring;
            return true;
        }

        std::optional<TcpStream> accept(std::error_code &ec)
        {
            if (socket_fd_ < 0)
            {
                ec = std::make_error_code(std::errc::bad_file_descriptor);
                return std::nullopt;
            }

            sockaddr_in client_addr = {};
            socklen_t addr_len = sizeof(client_addr);

            // 使用 io_uring 提交 accept 请求
            io_uring_sqe *sqe = io_uring_get_sqe(ring_);
            io_uring_prep_accept(sqe, socket_fd_, reinterpret_cast<sockaddr *>(&client_addr), &addr_len, 0);
            io_uring_submit(ring_);

            // 等待 accept 完成
            io_uring_cqe *cqe;
            if (io_uring_wait_cqe(ring_, &cqe) < 0)
            {
                ec = std::make_error_code(std::errc::io_error);
                return std::nullopt;
            }

            if (cqe->res < 0)
            {
                ec = std::make_error_code(std::errc::io_error);
                io_uring_cqe_seen(ring_, cqe);
                return std::nullopt;
            }

            int client_socket_fd = cqe->res;
            io_uring_cqe_seen(ring_, cqe);

            // 创建新的 io_uring 实例用于 TcpStream
            auto *stream_ring = new io_uring();
            if (io_uring_queue_init(32, stream_ring, 0) < 0)
            {
                ec = std::make_error_code(std::errc::io_error);
                close(client_socket_fd);
                return std::nullopt;
            }

            return TcpStream(new TcpStream::Impl(client_socket_fd, stream_ring));
        }

    private:
        int socket_fd_ = -1;
        io_uring *ring_ = nullptr;
    };

} // namespace net

#endif // LINUX_TCP_LISTENER_H
