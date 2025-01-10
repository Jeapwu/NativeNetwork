#ifndef MAC_TCP_STREAM_H
#define MAC_TCP_STREAM_H

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

} // namespace net

#endif // MAC_TCP_STREAM_H
