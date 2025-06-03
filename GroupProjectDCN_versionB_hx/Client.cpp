#include <winsock2.h>
#include <ws2tcpip.h>
#include <iostream>
#include <thread>
#include <string>
#pragma comment(lib, "ws2_32.lib")

using namespace std;

static const char XOR_KEY = 'K';
static const int  PORT = 54000;

// Simple XOR encrypt/decrypt
string xorCrypt(const string& s) {
    string out = s;
    for (char& c : out) c ^= XOR_KEY;
    return out;
}

// Thread: receive, decrypt, print
void receiver(SOCKET sock) {
    char buf[2048];
    int  len;
    while ((len = recv(sock, buf, sizeof(buf), 0)) > 0) {
        cout << xorCrypt(string(buf, len));
    }
}

int main() {
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);

    SOCKET sock = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in srv{};
    srv.sin_family = AF_INET;
    srv.sin_port = htons(PORT);
    inet_pton(AF_INET, "127.0.0.1", &srv.sin_addr);

    connect(sock, (sockaddr*)&srv, sizeof(srv));

    // Register
    cout << "Enter username: ";
    string name;
    getline(cin, name);
    send(sock, name.c_str(), (int)name.size(), 0);

    // Start receiver
    thread(receiver, sock).detach();

    // Input loop, get react
    string line;
    while (getline(cin, line)) {
        if (line == "/help") {
            cout << "Available commands:\n"
                << "  /send <u1,u2,...> <msg>\n"
                << "  /groupchat <u1,u2,...> <grp> <msg>\n"
                << "  /sendgp <grp> <msg>\n"
                << "  /pause <grp>\n"
                << "  /resume <grp>\n"
                << "  /dismiss <grp>\n"
                << "  mygplist\n"
                << "  /quit [<grp>]\n"
                << "  /help\n";
            continue;
        }
        string enc = xorCrypt(line);
        send(sock, enc.c_str(), (int)enc.size(), 0);
    }

    closesocket(sock);
    WSACleanup();
    return 0;
}
