#ifndef MAC_TCP_STREAM_H
#define MAC_TCP_STREAM_H

#include "TcpStream.hpp"
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>
#include <sys/socket.h>
#include <netinet/in.h>
#include <stdexcept>

namespace net
{

    class TcpStream::Impl
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

        // 连接到远程地址
        bool connect(const std::string &address, int port, std::error_code &ec)
        {
            // 创建套接字
            socket_fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
            if (socket_fd_ == -1)
            {
                ec.assign(errno, std::system_category());
                return false;
            }

            // 设置服务器地址
            sockaddr_in server_addr{};
            server_addr.sin_family = AF_INET;
            server_addr.sin_port = htons(port);
            if (::inet_pton(AF_INET, address.c_str(), &server_addr.sin_addr) <= 0)
            {
                ec.assign(errno, std::system_category());
                close(socket_fd_);
                socket_fd_ = -1;
                return false;
            }

            // 连接到服务器
            if (::connect(socket_fd_, reinterpret_cast<sockaddr *>(&server_addr), sizeof(server_addr)) == -1)
            {
                ec.assign(errno, std::system_category());
                close(socket_fd_);
                socket_fd_ = -1;
                return false;
            }

            return true;
        }

        // 写数据
        size_t write(const std::vector<uint8_t> &data, std::error_code &ec)
        {
            ssize_t bytes_sent = ::send(socket_fd_, data.data(), data.size(), 0);
            if (bytes_sent == -1)
            {
                ec.assign(errno, std::system_category());
                return 0;
            }
            return static_cast<size_t>(bytes_sent);
        }

        // 读数据
        size_t read(std::vector<uint8_t> &buffer, std::error_code &ec)
        {
            ssize_t bytes_received = ::recv(socket_fd_, buffer.data(), buffer.size(), 0);
            if (bytes_received == -1)
            {
                ec.assign(errno, std::system_category());
                return 0;
            }
            return static_cast<size_t>(bytes_received);
        }

    private:
        int socket_fd_;
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
        return impl_->write(data, ec);
    }

    // 读数据
    size_t TcpStream::read(std::vector<uint8_t> &buffer, std::error_code &ec)
    {
        return impl_->read(buffer, ec);
    }

} // namespace net

#endif // MAC_TCP_STREAM_H
