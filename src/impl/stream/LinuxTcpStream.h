#ifndef LINUX_TCP_STREAM_H
#define LINUX_TCP_STREAM_H

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
		Impl() : socket_fd_(-1), ring_(nullptr) {}
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

        bool connect(const std::string &address, int port, std::error_code &ec)
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

            sockaddr_in server_addr = {};
            server_addr.sin_family = AF_INET;
            server_addr.sin_port = htons(port);
            if (inet_pton(AF_INET, address.c_str(), &server_addr.sin_addr) <= 0)
            {
                ec = std::make_error_code(std::errc::invalid_argument);
                close(socket_fd);
                io_uring_queue_exit(ring);
                delete ring;
                return false;
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
                return false;
            }

            if (cqe->res < 0)
            {
                ec = std::make_error_code(std::errc::connection_refused);
                close(socket_fd);
                io_uring_cqe_seen(ring, cqe);
                io_uring_queue_exit(ring);
                delete ring;
                return false;
            }

            io_uring_cqe_seen(ring, cqe);

            // 连接成功
            socket_fd_ = socket_fd; // 保存 socket_fd
            ring_ = ring;           // 保存 io_uring 实例
            return true;
        }

        size_t write(const std::vector<uint8_t> &data, std::error_code &ec)
        {
            if (socket_fd_ < 0)
            {
                ec = std::make_error_code(std::errc::bad_file_descriptor);
                return 0;
            }

            // 使用 io_uring 提交异步写入请求
            io_uring_sqe *sqe = io_uring_get_sqe(ring_);
            if (!sqe)
            {
                ec = std::make_error_code(std::errc::resource_unavailable_try_again);
                return 0;
            }
            io_uring_prep_write(sqe, socket_fd_, data.data(), data.size(), 0);
            io_uring_submit(ring_);

            // 等待写入完成
            io_uring_cqe *cqe = nullptr;
            if (io_uring_wait_cqe(ring_, &cqe) < 0)
            {
                ec = std::make_error_code(std::errc::io_error);
                return 0;
            }

            if (cqe->res < 0)
            {
                ec = std::error_code(-cqe->res, std::generic_category());
                io_uring_cqe_seen(ring_, cqe);
                return 0;
            }

            size_t bytes_written = cqe->res;
            io_uring_cqe_seen(ring_, cqe);
            return bytes_written;
        }

        size_t read(std::vector<uint8_t> &buffer, std::error_code &ec)
        {
            if (socket_fd_ < 0)
            {
                ec = std::make_error_code(std::errc::bad_file_descriptor);
                return 0;
            }

            // 使用 io_uring 提交异步读取请求
            io_uring_sqe *sqe = io_uring_get_sqe(ring_);
            if (!sqe)
            {
                ec = std::make_error_code(std::errc::resource_unavailable_try_again);
                return 0;
            }
            io_uring_prep_read(sqe, socket_fd_, buffer.data(), buffer.size(), 0);
            io_uring_submit(ring_);

            // 等待读取完成
            io_uring_cqe *cqe = nullptr;
            if (io_uring_wait_cqe(ring_, &cqe) < 0)
            {
                ec = std::make_error_code(std::errc::io_error);
                return 0;
            }

            if (cqe->res < 0)
            {
                ec = std::error_code(-cqe->res, std::generic_category());
                io_uring_cqe_seen(ring_, cqe);
                return 0;
            }

            size_t bytes_read = cqe->res;
            io_uring_cqe_seen(ring_, cqe);
            return bytes_read;
        }

    private:
        int socket_fd_ = -1;
        io_uring *ring_ = nullptr;
    };

} // namespace net

#endif // LINUX_TCP_STREAM_H
