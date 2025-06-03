#include <winsock2.h>
#include <ws2tcpip.h>
#include <iostream>
#include <string>
#include "mingw.thread.h"
#include "mingw.mutex.h"
#include <map>
#include <sstream>

#pragma comment(lib, "ws2_32.lib")

struct User {
    std::string name;
    SOCKET socket;
    std::string group;
};

std::map<SOCKET, User> users;
std::mutex userMutex;

std::string encryptDecrypt(const std::string& text) {
    char key = 'K';
    std::string output = text;
    for (auto& c : output)
        c ^= key;
    return output;
}

void broadcast(const std::string& message, SOCKET sender, const std::string& group) {
    std::lock_guard<std::mutex> lock(userMutex);
    for (const auto& pair : users) {
        SOCKET client = pair.first;
        const User& user = pair.second;
        if (user.group == group && client != sender) {
            std::string encrypted = encryptDecrypt(message);
            send(client, encrypted.c_str(), encrypted.length(), 0);
        }
    }
}

void handleClient(SOCKET clientSocket) {
    char buffer[1024];
    int bytesReceived;

    bytesReceived = recv(clientSocket, buffer, sizeof(buffer), 0);
    if (bytesReceived <= 0) {
        closesocket(clientSocket);
        return;
    }

    std::string username(buffer, bytesReceived);
    {
        std::lock_guard<std::mutex> lock(userMutex);
        users[clientSocket] = { username, clientSocket, "default" };
        std::cout << username << " joined (default group).\n";
    }

    std::string welcome = encryptDecrypt("Welcome, " + username + "!");
    send(clientSocket, welcome.c_str(), welcome.length(), 0);

    while (true) {
        bytesReceived = recv(clientSocket, buffer, sizeof(buffer), 0);
        if (bytesReceived <= 0) {
            std::lock_guard<std::mutex> lock(userMutex);
            std::cout << users[clientSocket].name << " disconnected.\n";
            users.erase(clientSocket);
            closesocket(clientSocket);
            return;
        }

        std::string decrypted = encryptDecrypt(std::string(buffer, bytesReceived));
        std::string senderName = users[clientSocket].name;
        std::string& senderGroup = users[clientSocket].group;

        if (decrypted.substr(0, 6) == "/group") {
            std::string newGroup = decrypted.substr(7);
            {
                std::lock_guard<std::mutex> lock(userMutex);
                users[clientSocket].group = newGroup;
            }
            std::string msg = "Switched to group: " + newGroup;
            std::string encrypted = encryptDecrypt(msg);
            send(clientSocket, encrypted.c_str(), encrypted.length(), 0);
            continue;
        }

        if (decrypted == "/list") {
            std::string list = "Online users:\n";
            {
                std::lock_guard<std::mutex> lock(userMutex);
                for (const auto& p : users) {
                    list += p.second.name + " [" + p.second.group + "]\n";
                }
            }
            std::string encrypted = encryptDecrypt(list);
            send(clientSocket, encrypted.c_str(), encrypted.length(), 0);
            continue;
        }

        if (decrypted == "/quit") {
            std::string bye = "You left the chat.";
            std::string encrypted = encryptDecrypt(bye);
            send(clientSocket, encrypted.c_str(), encrypted.length(), 0);

            {
                std::lock_guard<std::mutex> lock(userMutex);
                std::cout << senderName << " quit the chat.\n";
                users.erase(clientSocket);
            }

            closesocket(clientSocket);
            return;
        }
        time_t now = time(0);
        tm* localtm = localtime(&now);
        char timeStr[20];
        strftime(timeStr, sizeof(timeStr), "%Y-%m-%d %H:%M:%S", localtm);
        std::string fullMessage = "[" + std::string(timeStr) + "] " + senderName + ": " + decrypted;
        std::cout << "[" << senderGroup << "] " << fullMessage << std::endl;
        broadcast(fullMessage, clientSocket, senderGroup);
    }
}
void adminCommandLoop() {
    std::string cmd;
    while (true) {
        std::getline(std::cin, cmd);

        if (cmd == "/list") {
            std::lock_guard<std::mutex> lock(userMutex);
            std::cout << "=== Online Users ===\n";
            for (const auto& [sock, user] : users) {
                std::cout << "- " << user.name << " [" << user.group << "]\n";
            }
            std::cout << "====================\n";
        }
        else if (cmd.rfind("/delete ", 0) == 0) {
            std::string username = cmd.substr(8); // after "/delete "
            std::lock_guard<std::mutex> lock(userMutex);
            bool found = false;
            for (auto it = users.begin(); it != users.end(); ++it) {
                if (it->second.name == username) {
                    std::cout << "Kicking out user: " << username << std::endl;
                    send(it->first, encryptDecrypt("You have been removed by admin.").c_str(), 50, 0);
                    closesocket(it->first);
                    users.erase(it);
                    found = true;
                    break;
                }
            }
            if (!found) {
                std::cout << "User not found: " << username << std::endl;
            }
        }
        else if (cmd == "/help") {
            std::cout << "Server commands:\n";
            std::cout << "  /list         - List all online users\n";
            std::cout << "  /delete NAME  - Kick a user by name\n";
            std::cout << "  /help         - Show this help message\n";
        }
        else {
            std::cout << "Unknown command. Type /help for options.\n";
        }
    }
}

int main() {
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);

    SOCKET serverSocket = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in serverAddr{};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(54000);
    serverAddr.sin_addr.s_addr = inet_addr("127.0.0.1");

    bind(serverSocket, (sockaddr*)&serverAddr, sizeof(serverAddr));
    listen(serverSocket, SOMAXCONN);

    std::cout << "Server is running on port 54000...\n";
    std::thread(adminCommandLoop).detach();

    while (true) {
        SOCKET clientSocket = accept(serverSocket, nullptr, nullptr);
        std::thread(handleClient, clientSocket).detach();
    }

    WSACleanup();
    return 0;
}
