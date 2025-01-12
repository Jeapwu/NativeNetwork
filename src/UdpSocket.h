#ifndef UDP_SOCKET_H
#define UDP_SOCKET_H

#include <string>
#include <vector>
#include <optional>
#include <system_error>

namespace net
{

    class UdpSocket
    {
    public:
        UdpSocket();
        ~UdpSocket();

        UdpSocket(const UdpSocket&) = delete;
        UdpSocket& operator=(const UdpSocket&) = delete;

        UdpSocket(UdpSocket&&) noexcept;
        UdpSocket& operator=(UdpSocket&&) noexcept;

        // 绑定到本地地址和端口
        static std::optional<UdpSocket> bind(const std::string& address, int port, std::error_code& ec);

        // 发送数据到目标地址
        size_t send_to(const std::vector<uint8_t>& data, const std::string& address, int port, std::error_code& ec);

        // 从远程地址接收数据
        size_t recv_from(std::vector<uint8_t>& buffer, std::string& address, int& port, std::error_code& ec);

    private:
        class Impl; // 平台特定实现
        Impl* impl_;
    };

} // namespace net

#endif // UDP_SOCKET_H
