#include "TcpListener.h"

#if defined(_WIN32)
#include "WindowsTcpListener.h"
#elif defined(__linux__)
#include "LinuxTcpListener.h"
#elif defined(__APPLE__)
#include "MacTcpListener.h"
#else
#error "Unsupported platform"
#endif

namespace net
{
    // TcpListener 类的构造和析构
    TcpListener::TcpListener() : impl_(new Impl()) {}

    TcpListener::~TcpListener()
    {
        delete impl_;
    }

    // 移动构造和移动赋值
    TcpListener::TcpListener(TcpListener&& other) noexcept : impl_(other.impl_)
    {
        other.impl_ = nullptr;
    }

    TcpListener& TcpListener::operator=(TcpListener&& other) noexcept
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
    std::optional<TcpListener> TcpListener::bind(const std::string& address, int port, std::error_code& ec)
    {
        TcpListener listener;
        if (listener.impl_->bind(address, port, ec))
        {
            return listener;
        }
        return std::nullopt;
    }

    // 接受连接
    std::optional<TcpStream> TcpListener::accept(std::error_code& ec)
    {
        if (!impl_)
        {
            ec = std::make_error_code(std::errc::bad_file_descriptor);
            return std::nullopt;
        }
        return impl_->accept(ec);
    }
}