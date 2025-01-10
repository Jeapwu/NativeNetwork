#ifndef WINDOWS_TCP_STREAM_H
#define WINDOWS_TCP_STREAM_H

#include <winsock2.h>
#include <ws2tcpip.h>
#include <stdexcept>
#include <system_error>

#pragma comment(lib, "Ws2_32.lib")

namespace net
{

    // TcpStream::Impl 实现
    class TcpStream::Impl
    {
    public:
        Impl(SOCKET socket) : socket_(socket) {}

        ~Impl()
        {
            if (socket_ != INVALID_SOCKET)
            {
                closesocket(socket_);
            }
        }

        // 连接到远程地址
        bool connect(const std::string &address, int port, std::error_code &ec)
        {
            WSADATA wsaData;
            if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
            {
                ec = std::make_error_code(std::errc::not_enough_memory);
                return false;
            }

            SOCKET socket = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
            if (socket == INVALID_SOCKET)
            {
                ec = std::make_error_code(std::errc::address_family_not_supported);
                return false;
            }

            sockaddr_in serverAddr = {};
            serverAddr.sin_family = AF_INET;
            serverAddr.sin_port = htons(port);
            inet_pton(AF_INET, address.c_str(), &serverAddr.sin_addr);

            if (::connect(socket, (sockaddr *)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR)
            {
                ec = std::make_error_code(std::errc::connection_refused);
                closesocket(socket);
                return false;
            }

            socket_ = socket; // 保存已连接的套接字
            return true;
        }

        size_t write(const std::vector<uint8_t> &data, std::error_code &ec)
        {
            int result = ::send(socket_, reinterpret_cast<const char *>(data.data()), static_cast<int>(data.size()), 0);
            if (result == SOCKET_ERROR)
            {
                ec = std::make_error_code(std::errc::io_error);
                return 0;
            }
            return static_cast<size_t>(result);
        }

        size_t read(std::vector<uint8_t> &buffer, std::error_code &ec)
        {
            int result = ::recv(socket_, reinterpret_cast<char *>(buffer.data()), static_cast<int>(buffer.size()), 0);
            if (result == SOCKET_ERROR)
            {
                ec = std::make_error_code(std::errc::io_error);
                return 0;
            }
            return static_cast<size_t>(result);
        }

    private:
        SOCKET socket_ = INVALID_SOCKET; // 初始为无效套接字
    };

} // namespace net

#endif // WINDOWS_TCP_STREAM_H
