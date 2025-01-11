#include <iostream>
#include <string>
#include <system_error>
#include "TcpStream.hpp" // 假设已实现

int main()
{
    std::error_code ec;

    // 连接到服务器
    auto streamOpt = net::TcpStream::connect("127.0.0.1", 8080, ec);
    if (!streamOpt)
    {
        std::cerr << "Failed to connect to server: " << ec.message() << std::endl;
        return 1;
    }

    auto stream = std::move(*streamOpt);
    std::cout << "Connected to server." << std::endl;

    std::string message;
    char buffer[1024];

    while (true)
    {
        std::cout << "Enter message: ";
        std::getline(std::cin, message);
        if (message == "exit")
        {
            break;
        }

        // 发送数据给服务器
        stream.write(message.data(), message.size(), ec);
        if (ec)
        {
            std::cerr << "Write error: " << ec.message() << std::endl;
            break;
        }

        // 接收服务器的回显数据
        auto bytesRead = stream.read(buffer, sizeof(buffer), ec);
        if (ec)
        {
            std::cerr << "Read error: " << ec.message() << std::endl;
            break;
        }

        std::cout << "Server echoed: " << std::string(buffer, bytesRead) << std::endl;
    }

    return 0;
}
