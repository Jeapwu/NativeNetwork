#include <iostream>
#include <vector>
#include <optional>
#include <system_error>
#include "TcpStream.h"

int main()
{
    WSADATA wsaData;
    int result = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (result != 0)
    {
        std::cerr << "WSAStartup failed with error: " << result << std::endl;
        return -1;
    }

    // 连接到服务端
    std::error_code ec;
    auto streamOpt = net::TcpStream::connect("127.0.0.1", 9090, ec);
    if (!streamOpt)
    {
        std::cerr << "Failed to connect to server: " << ec.message() << std::endl;
        WSACleanup();
        return -1;
    }

    // 发送数据
    net::TcpStream stream = std::move(*streamOpt);
    std::string message = "Hello from client!";
    std::vector<uint8_t> data(message.begin(), message.end());
    stream.write(data, ec);
    if (ec)
    {
        std::cerr << "Error writing to server: " << ec.message() << std::endl;
        WSACleanup();
        return -1;
    }

    // 接收响应
    std::vector<uint8_t> buffer(1024);
    size_t bytesRead = stream.read(buffer, ec);
    if (ec)
    {
        std::cerr << "Error reading from server: " << ec.message() << std::endl;
        WSACleanup();
        return -1;
    }

    std::cout << "Received: " << std::string(buffer.begin(), buffer.begin() + bytesRead) << std::endl;
    WSACleanup();
    return 0;
}
