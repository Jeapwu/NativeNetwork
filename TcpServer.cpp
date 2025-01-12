#include <iostream>
#include <vector>
#include <thread>
#include <optional>
#include <system_error>
#include "TcpListener.h"
#include "TcpStream.h"

void handle_client(net::TcpStream client)
{
    try
    {
        std::error_code ec;
        std::vector<uint8_t> buffer(1024);

        // 读取数据
        size_t bytesRead = client.read(buffer, ec);
        if (ec)
        {
            std::cerr << "Error reading from client: " << ec.message() << std::endl;
            return;
        }

        std::cout << "Received: " << std::string(buffer.begin(), buffer.begin() + bytesRead) << std::endl;

        // 回写数据
        std::string response = "Hello from server!";
        std::vector<uint8_t> responseData(response.begin(), response.end());
        client.write(responseData, ec);
        if (ec)
        {
            std::cerr << "Error writing to client: " << ec.message() << std::endl;
        }
    }
    catch (const std::exception& e)
    {
        std::cerr << "Exception in client handler: " << e.what() << std::endl;
    }
}

int main()
{
    WSADATA wsaData;
    int result = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (result != 0)
    {
        std::cerr << "WSAStartup failed with error: " << result << std::endl;
        return -1;
    }

    std::error_code ec;
    // 启动监听
    auto listenerOpt = net::TcpListener::bind("127.0.0.1", 9090, ec);
    if (!listenerOpt)
    {
        std::cerr << "Failed to bind: " << ec.message() << std::endl;
        WSACleanup();
        return -1;
    }

    net::TcpListener listener = std::move(*listenerOpt);
    std::cout << "Server listening on 127.0.0.1:9090" << std::endl;

    while (true)
    {
        auto clientOpt = listener.accept(ec);
        if (!clientOpt)
        {
            std::cerr << "Failed to accept connection: " << ec.message() << std::endl;
            continue;
        }

        std::thread(handle_client, std::move(*clientOpt)).detach();
    }

    WSACleanup(); // 在程序结束时清理 Winsock
    return 0;
}