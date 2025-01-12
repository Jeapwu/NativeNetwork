#ifndef TCP_LISTENER_H
#define TCP_LISTENER_H

#include <string>
#include <optional>
#include <memory>
#include "TcpStream.h" // TcpStream 的定义包含通信逻辑

namespace net
{
    class TcpListener
    {
    public:
        TcpListener();
        ~TcpListener();

        /// 禁用拷贝构造和拷贝赋值
        TcpListener(const TcpListener&) = delete;
        TcpListener& operator=(const TcpListener&) = delete;

        /// 移动构造和移动赋值
        TcpListener(TcpListener&& other) noexcept;
        TcpListener& operator=(TcpListener&& other) noexcept;

        /// 绑定一个地址和端口并返回一个 TcpListener 实例
        static std::optional<TcpListener> bind(const std::string& address, int port, std::error_code& ec);

        /// 接受一个新的连接
        std::optional<TcpStream> accept(std::error_code& ec);

    private:
        // 内部实现类，隐藏平台特定逻辑

        class Impl;
        Impl* impl_;
    };

} // namespace net

#endif // TCP_LISTENER_H
