#ifndef TCP_STREAM_H
#define TCP_STREAM_H

#include <string>
#include <vector>
#include <optional>
#include <system_error>

class SOCKET;
class io_uring;

namespace net
{

    class TcpStream
    {
    public:
        // 构造函数和析构函数
        TcpStream();
        TcpStream(SOCKET socket);
        TcpStream(int socket_fd, io_uring *ring);
        ~TcpStream();

        // 禁用拷贝构造和赋值
        TcpStream(const TcpStream&) = delete;
        TcpStream& operator=(const TcpStream&) = delete;

        // 支持移动构造和赋值
        TcpStream(TcpStream&&) noexcept;
        TcpStream& operator=(TcpStream&&) noexcept;

        // 连接到远程地址
        static std::optional<TcpStream> connect(const std::string& address, int port, std::error_code& ec);

        // 写入数据
        size_t write(const std::vector<uint8_t>& data, std::error_code& ec);

        // 读取数据
        size_t read(std::vector<uint8_t>& buffer, std::error_code& ec);

    public:
        class Impl; // 平台特定实现
        Impl* impl_;
    };

} // namespace net

#endif // TCP_STREAM_H
