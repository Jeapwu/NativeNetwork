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
#include "UdpSocket.h"

namespace net
{
    // UdpSocket::Impl for Linux with io_uring
    class UdpSocket::Impl
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

        bool bind(const std::string &address, int port, std::error_code &ec)
        {
            // 创建 io_uring 实例
            ring_ = new io_uring();
            if (io_uring_queue_init(32, ring_, 0) < 0)
            {
                ec = std::make_error_code(std::errc::io_error);
                delete ring_;
                ring_ = nullptr;
                return false;
            }

            // 创建 socket
            socket_fd_ = ::socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK, 0);
            if (socket_fd_ < 0)
            {
                ec = std::make_error_code(std::errc::address_family_not_supported);
                release();
                return false;
            }

            sockaddr_in local_addr = {};
            local_addr.sin_family = AF_INET;
            local_addr.sin_port = htons(port);
            if (inet_pton(AF_INET, address.c_str(), &local_addr.sin_addr) <= 0)
            {
                ec = std::make_error_code(std::errc::invalid_argument);
                release();
                return false;
            }

            if (::bind(socket_fd_, reinterpret_cast<sockaddr *>(&local_addr), sizeof(local_addr)) < 0)
            {
                ec = std::make_error_code(std::errc::address_in_use);
                release();
                return false;
            }

            return true;
        }

        size_t send_to(const std::vector<uint8_t> &data, const std::string &address, int port, std::error_code &ec)
        {
            if (socket_fd_ < 0 || !ring_)
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

            io_uring_sqe *sqe = io_uring_get_sqe(ring_);
            if (!sqe)
            {
                ec = std::make_error_code(std::errc::resource_unavailable_try_again);
                return 0;
            }

            io_uring_prep_sendto(sqe, socket_fd_, data.data(), data.size(), 0,
                                 reinterpret_cast<sockaddr *>(&remote_addr), sizeof(remote_addr));
            io_uring_submit(ring_);

            io_uring_cqe *cqe;
            if (io_uring_wait_cqe(ring_, &cqe) < 0)
            {
                ec = std::make_error_code(std::errc::io_error);
                return 0;
            }

            if (cqe->res < 0)
            {
                ec = std::make_error_code(std::errc::io_error);
                io_uring_cqe_seen(ring_, cqe);
                return 0;
            }

            size_t bytes_sent = cqe->res;
            io_uring_cqe_seen(ring_, cqe);
            return bytes_sent;
        }

        size_t recv_from(std::vector<uint8_t> &buffer, std::string &address, int &port, std::error_code &ec)
        {
            if (socket_fd_ < 0 || !ring_)
            {
                ec = std::make_error_code(std::errc::bad_file_descriptor);
                return 0;
            }

            // 准备 sender 地址存储
            sockaddr_in sender_addr = {};
            socklen_t addr_len = sizeof(sender_addr);

            // 准备 msghdr 用于 recvmsg
            msghdr msg = {};
            iovec iov = {};
            iov.iov_base = buffer.data();
            iov.iov_len = buffer.size();

            msg.msg_name = &sender_addr; // 发送方地址
            msg.msg_namelen = addr_len;  // 地址长度
            msg.msg_iov = &iov;          // 数据缓冲区
            msg.msg_iovlen = 1;          // iovec 数量

            // 从 io_uring 获取一个提交队列条目 (SQE)
            io_uring_sqe *sqe = io_uring_get_sqe(ring_);
            if (!sqe)
            {
                ec = std::make_error_code(std::errc::resource_unavailable_try_again);
                return 0;
            }

            // 准备 recvmsg 操作
            io_uring_prep_recvmsg(sqe, socket_fd_, &msg, 0);

            // 提交队列
            io_uring_submit(ring_);

            // 等待完成队列条目 (CQE)
            io_uring_cqe *cqe;
            if (io_uring_wait_cqe(ring_, &cqe) < 0)
            {
                ec = std::make_error_code(std::errc::io_error);
                return 0;
            }

            if (cqe->res < 0)
            {
                ec = std::make_error_code(std::errc::io_error);
                io_uring_cqe_seen(ring_, cqe);
                return 0;
            }

            // 获取接收到的字节数
            size_t bytes_received = cqe->res;
            io_uring_cqe_seen(ring_, cqe);

            // 提取发送方地址和端口
            char addr_str[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &sender_addr.sin_addr, addr_str, sizeof(addr_str));
            address = addr_str;
            port = ntohs(sender_addr.sin_port);

            return bytes_received;
        }

    private:
        void release()
        {
            if (socket_fd_ >= 0)
            {
                close(socket_fd_);
                socket_fd_ = -1;
            }
            if (ring_)
            {
                io_uring_queue_exit(ring_);
                delete ring_;
                ring_ = nullptr;
            }
        }

        int socket_fd_;
        io_uring *ring_;
    };

} // namespace net

#endif /// LINUX_UDP_SOCKET_H
