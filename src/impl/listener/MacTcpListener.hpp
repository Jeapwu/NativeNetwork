#include "TcpListener.h"
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

    class TcpListener::Impl
    {
    public:
        Impl() : listener_fd_(-1) {}

        ~Impl()
        {
            if (listener_fd_ != -1)
            {
                close(listener_fd_);
            }
        }

        // 绑定地址和端口
        bool bind(const std::string &address, int port, std::error_code &ec)
        {
            listener_fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
            if (listener_fd_ == -1)
            {
                ec.assign(errno, std::system_category());
                return false;
            }

            sockaddr_in server_addr{};
            server_addr.sin_family = AF_INET;
            server_addr.sin_port = htons(port);

            // 处理地址
            if (address == "0.0.0.0" || address == "*")
            {
                server_addr.sin_addr.s_addr = INADDR_ANY; // 任意地址
            }
            else
            {
                if (::inet_pton(AF_INET, address.c_str(), &server_addr.sin_addr) <= 0)
                {
                    ec.assign(errno, std::system_category());
                    close(listener_fd_);
                    listener_fd_ = -1;
                    return false;
                }
            }

            // 绑定到指定的地址和端口
            if (::bind(listener_fd_, reinterpret_cast<sockaddr *>(&server_addr), sizeof(server_addr)) == -1)
            {
                ec.assign(errno, std::system_category());
                close(listener_fd_);
                listener_fd_ = -1;
                return false;
            }

            // 开始监听
            if (::listen(listener_fd_, 5) == -1)
            {
                ec.assign(errno, std::system_category());
                close(listener_fd_);
                listener_fd_ = -1;
                return false;
            }

            return true;
        }

        // 接受一个新的连接
        std::optional<TcpStream> accept(std::error_code &ec)
        {
            sockaddr_in client_addr{};
            socklen_t client_len = sizeof(client_addr);

            int client_fd = ::accept(listener_fd_, reinterpret_cast<sockaddr *>(&client_addr), &client_len);
            if (client_fd == -1)
            {
                ec.assign(errno, std::system_category());
                return std::nullopt;
            }

            return TcpStream(client_fd); // 假设 TcpStream 可以直接通过文件描述符创建
        }

    private:
        int listener_fd_;
    };

    // TcpListener 类的构造和析构
    TcpListener::TcpListener() : impl_(new Impl()) {}

    TcpListener::~TcpListener()
    {
        delete impl_;
    }

    // 移动构造和移动赋值
    TcpListener::TcpListener(TcpListener &&other) noexcept : impl_(other.impl_)
    {
        other.impl_ = nullptr;
    }

    TcpListener &TcpListener::operator=(TcpListener &&other) noexcept
    {
        if (this != &other)
        {
            delete impl_;
            impl_ = other.impl_;
            other.impl_ = nullptr;
        }
        return *this;
    }

    // 绑定地址和端口
    std::optional<TcpListener> TcpListener::bind(const std::string &address, int port)
    {
        std::error_code ec;
        TcpListener listener;
        if (listener.impl_->bind(address, port, ec))
        {
            return listener;
        }
        return std::nullopt;
    }

    // 接受连接
    std::optional<TcpStream> TcpListener::accept()
    {
        std::error_code ec;
        return impl_->accept(ec);
    }

} // namespace net
