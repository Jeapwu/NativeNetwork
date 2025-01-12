#include "TcpStream.h"

#if defined(_WIN32)
#include "WindowsTcpStream.h"
#elif defined(__linux__)
#include "LinuxTcpStream.h"
#elif defined(__APPLE__)
#include "MacTcpStream.h"
#else
#error "Unsupported platform"
#endif

namespace net
{
    // TcpStream 类的构造和析构
    TcpStream::TcpStream() : impl_(new Impl()) {}

    TcpStream::TcpStream(SOCKET socket) : impl_(new Impl(socket)) {}

    TcpStream::~TcpStream()
    {
        delete impl_;
    }

    // 移动构造和赋值
    TcpStream::TcpStream(TcpStream&& other) noexcept : impl_(other.impl_)
    {
        other.impl_ = nullptr;
    }

    TcpStream& TcpStream::operator=(TcpStream&& other) noexcept
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
    std::optional<TcpStream> TcpStream::connect(const std::string& address, int port, std::error_code& ec)
    {
        TcpStream stream;
        if (stream.impl_->connect(address, port, ec))
        {
            return stream;
        }
        return std::nullopt;
    }

    // 写数据
    size_t TcpStream::write(const std::vector<uint8_t>& data, std::error_code& ec)
    {
        if (!impl_)
        {
            ec = std::make_error_code(std::errc::bad_file_descriptor);
            return 0;
        }
        return impl_->write(data, ec);
    }

    // 读数据
    size_t TcpStream::read(std::vector<uint8_t>& buffer, std::error_code& ec)
    {
        if (!impl_)
        {
            ec = std::make_error_code(std::errc::bad_file_descriptor);
            return 0;
        }
        return impl_->read(buffer, ec);
    }
}