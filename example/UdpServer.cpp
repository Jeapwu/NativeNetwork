#include <iostream>
#include <thread>
#include <vector>
#include <optional>
#include <string>
#include "UdpSocket.h"
#include <WinSock2.h>

void udp_server(const std::string& bind_address, int bind_port)
{
    std::error_code ec;
    auto maybe_socket = net::UdpSocket::bind(bind_address, bind_port, ec);
    if (!maybe_socket)
    {
        std::cerr << "Failed to bind UDP server to " << bind_address << ":" << bind_port
            << " Error: " << ec.message() << std::endl;
        return;
    }

    auto server_socket = std::move(*maybe_socket);

    std::cout << "UDP server listening on " << bind_address << ":" << bind_port << std::endl;

    while (true)
    {
        std::vector<uint8_t> buffer(1024);
        std::string remote_address;
        int remote_port = 0;

        size_t received = server_socket.recv_from(buffer, remote_address, remote_port, ec);
        if (ec)
        {
            std::cerr << "Error receiving data: " << ec.message() << std::endl;
            break;
        }

        std::string received_message(buffer.begin(), buffer.begin() + received);
        std::cout << "Received " << received << " bytes from " << remote_address << ":" << remote_port
            << " -> " << received_message << std::endl;

        // Echo the message back to the sender
        size_t sent = server_socket.send_to(buffer, remote_address, remote_port, ec);
        if (ec)
        {
            std::cerr << "Error sending response: " << ec.message() << std::endl;
            break;
        }
    }
}


void udp_client(const std::string& server_address, int server_port)
{
    std::error_code ec;
    net::UdpSocket client_socket;

    std::string message = "Hello from UDP client!";
    std::vector<uint8_t> data(message.begin(), message.end());

    size_t sent = client_socket.send_to(data, server_address, server_port, ec);
    if (ec)
    {
        std::cerr << "Error sending data: " << ec.message() << std::endl;
        return;
    }

    std::cout << "Sent " << sent << " bytes to " << server_address << ":" << server_port << std::endl;

    std::vector<uint8_t> buffer(1024);
    std::string remote_address;
    int remote_port = 0;

    size_t received = client_socket.recv_from(buffer, remote_address, remote_port, ec);
    if (ec)
    {
        std::cerr << "Error receiving response: " << ec.message() << std::endl;
        return;
    }

    std::string response(buffer.begin(), buffer.begin() + received);
    std::cout << "Received response from server: " << response << std::endl;
}


int main()
{
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
    {
        std::cerr << "WSAStartup failed!" << std::endl;
        return -1;
    }

    // 服务器线程
    std::thread server_thread([]() {
        udp_server("127.0.0.1", 12345);
        });

    // 等待服务器启动
    std::this_thread::sleep_for(std::chrono::seconds(1));

    // 客户端
    udp_client("127.0.0.1", 12345);

    server_thread.join();
    WSACleanup();

    return 0;
}
