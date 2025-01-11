#include <iostream>
#include <thread>
#include <vector>
#include <string>
#include <optional>
#include <system_error>
#include "src/TcpListener.hpp" // 假设已实现
#include "src/TcpStream.hpp"   // 假设已实现

void handleClient(net::TcpStream stream)
{
    std::error_code ec;
    char buffer[1024];

    while (true)
    {
        // 读取客户端发送的数据
        auto bytesRead = stream.read(buffer, sizeof(buffer), ec);
        if (ec)
        {
            std::cerr << "Read error: " << ec.message() << std::endl;
            break;
        }

        if (bytesRead == 0)
        {
            std::cout << "Client disconnected." << std::endl;
            break;
        }

        std::string message(buffer, bytesRead);
        std::cout << "Received: " << message << std::endl;

        // 回显数据给客户端
        stream.write(message.data(), message.size(), ec);
        if (ec)
        {
            std::cerr << "Write error: " << ec.message() << std::endl;
            break;
        }
    }
}

int main()
{
    std::error_code ec;
    auto listenerOpt = net::TcpListener::bind("127.0.0.1", 8080, ec);
    if (!listenerOpt)
    {
        std::cerr << "Failed to bind listener: " << ec.message() << std::endl;
        return 1;
    }

    auto listener = std::move(*listenerOpt);
    std::cout << "Server is listening on 127.0.0.1:8080..." << std::endl;

    std::vector<std::thread> clientThreads;

    while (true)
    {
        auto streamOpt = listener.accept(ec);
        if (!streamOpt)
        {
            std::cerr << "Accept error: " << ec.message() << std::endl;
            continue;
        }

        std::cout << "Client connected." << std::endl;

        // 创建一个线程处理客户端
        clientThreads.emplace_back(handleClient, std::move(*streamOpt));
    }

    for (auto &thread : clientThreads)
    {
        if (thread.joinable())
        {
            thread.join();
        }
    }

    return 0;
}
