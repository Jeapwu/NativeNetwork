#include "UdpSocket.h"

#if defined(_WIN32)
#include "WindowsUdpSocket.h"
#elif defined(__linux__)
#include "LinuxUdpSocket.h"
#elif defined(__APPLE__)
#include "MacUdpSocket.h"
#else
#error "Unsupported platform"
#endif

namespace net
{
    // 默认构造和析构函数
    UdpSocket::UdpSocket() : impl_(new Impl()) {}

    UdpSocket::~UdpSocket()
    {
        delete impl_;
    }

    // 移动构造函数
    UdpSocket::UdpSocket(UdpSocket&& other) noexcept : impl_(other.impl_)
    {
        other.impl_ = nullptr;
    }

    // 移动赋值运算符
    UdpSocket& UdpSocket::operator=(UdpSocket&& other) noexcept
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
    std::optional<UdpSocket> UdpSocket::bind(const std::string& address, int port, std::error_code& ec)
    {
        UdpSocket socket;
        if (socket.impl_->bind(address, port, ec))
        {
            return socket;
        }
        return std::nullopt;
    }

    // 发送数据到目标地址
    size_t UdpSocket::send_to(const std::vector<uint8_t>& data, const std::string& address, int port, std::error_code& ec)
    {
        if (!impl_)
        {
            ec = std::make_error_code(std::errc::bad_file_descriptor);
            return 0;
        }
        return impl_->send_to(data, address, port, ec);
    }

    // 从远程地址接收数据
    size_t UdpSocket::recv_from(std::vector<uint8_t>& buffer, std::string& address, int& port, std::error_code& ec)
    {
        if (!impl_)
        {
            ec = std::make_error_code(std::errc::bad_file_descriptor);
            return 0;
        }
        return impl_->recv_from(buffer, address, port, ec);
    }
}
