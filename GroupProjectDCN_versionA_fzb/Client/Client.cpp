#include <iostream>
#include <winsock2.h>
#include "mingw.thread.h"
#include "mingw.mutex.h"


#pragma comment(lib, "ws2_32.lib")

std::string encryptDecrypt(const std::string& text) {
    char key = 'K';
    std::string result = text;
    for (char& c : result) c ^= key;
    return result;
}

void receiveMessages(SOCKET sock) {
    char buffer[1024];
    while (true) {
        int bytesReceived = recv(sock, buffer, sizeof(buffer), 0);
        if (bytesReceived <= 0) break;

        buffer[bytesReceived] = '\0';
        std::string decrypted = encryptDecrypt(std::string(buffer, bytesReceived));
        time_t now = time(0);
        tm* localtm = localtime(&now);
        char timeStr[20];
        strftime(timeStr, sizeof(timeStr), "%Y-%m-%d %H:%M:%S", localtm);
        std::cout << "[" << timeStr << "] " << decrypted << std::endl;
    }
}

int main() {
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);

    SOCKET clientSocket = socket(AF_INET, SOCK_STREAM, 0);

    sockaddr_in serverAddr{};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(54000);
    serverAddr.sin_addr.s_addr = inet_addr("127.0.0.1"); // 改为服务器 IP

    connect(clientSocket, (sockaddr*)&serverAddr, sizeof(serverAddr));

    std::string name;
    std::cout << "Enter your username: ";
    std::getline(std::cin, name);
    send(clientSocket, name.c_str(), name.length(), 0);

    std::thread(receiveMessages, clientSocket).detach();

    std::string message;
    while (true) {
        std::getline(std::cin, message);

        if (message == "/quit") break;

        std::string encrypted = encryptDecrypt(message);
        send(clientSocket, encrypted.c_str(), encrypted.length(), 0);
    }

    closesocket(clientSocket);
    WSACleanup();
    return 0;
}
