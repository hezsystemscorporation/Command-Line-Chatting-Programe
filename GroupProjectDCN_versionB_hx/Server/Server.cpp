#include <winsock2.h>
#include <ws2tcpip.h>
#include <iostream>
#include <thread>
#include <mutex>
#include <map>
#include <set>
#include <vector>
#include <sstream>
#include <string>
#include <ctime>
#pragma comment(lib, "ws2_32.lib")

using namespace std;

static const char XOR_KEY = 'K';        // Key for simple XOR encryption/decryption
static const int  MAX_CLIENTS = 10;     // Maximum number of simultaneous clients
static const int  PORT = 54000;         // Listening port for the server

// Global state
map<string, SOCKET>       users;         // username -> socket
map<string, set<string>>  groups;        // group name -> members
map<string, string>       groupOwners;   // group name -> owner username
map<string, set<string>>  paused;        // username -> paused groups
mutex                     mtx;

// Get current timestamp as YYYY-MM-DD HH:MM:SS
string timestamp() {
    time_t now = time(nullptr);
    tm local;
    localtime_s(&local, &now);
    char buf[20];
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &local);
    return buf;
}

// Simple XOR encrypt/decrypt
string xorCrypt(const string& s) {
    string out = s;
    for (char& c : out) c ^= XOR_KEY;
    return out;
}

// Send encrypted message with no extra formatting
void sendEncrypted(SOCKET s, const string& msg) {
    string enc = xorCrypt(msg);
    send(s, enc.c_str(), (int)enc.size(), 0);
}

// Split string by delimiter
vector<string> split(const string& s, char delim) {
    vector<string> parts;
    istringstream iss(s);
    string item;
    while (getline(iss, item, delim)) {
        if (!item.empty()) parts.push_back(item);
    }
    return parts;
}

// Handle one connected client
void handleClient(SOCKET sock) {
    char buffer[2048];
    int  len;

    // 1) Receive username
    if ((len = recv(sock, buffer, sizeof(buffer), 0)) <= 0) {
        closesocket(sock);
        return;
    }
    string username(buffer, len);
    {
        lock_guard<mutex> lock(mtx);
        users[username] = sock;
    }
    cout << "[" << timestamp() << "] [Server] " << username << " connected.\n";
    sendEncrypted(sock, "Welcome, " + username + "! Type /help\n");

    // 2) Main loop
    while ((len = recv(sock, buffer, sizeof(buffer), 0)) > 0) {
        string cmd = xorCrypt(string(buffer, len));
        istringstream iss(cmd);
        string token; iss >> token;

        if (token == "/help") {
            ostringstream h;
            h << "Commands:\n"
                << "  /send <u1,u2,...> <msg>            One-off private message\n"
                << "  /groupchat <u1,u2,...> <grp> <msg> Create group + send first message\n"
                << "  /sendgp <grp> <msg>                Send to an existing group\n"
                << "  /pause <grp>                       Pause receiving group\n"
                << "  /resume <grp>                      Resume receiving\n"
                << "  /dismiss <grp>                     Dismiss group (owner)\n"
                << "  mygplist                           List your groups\n"
                << "  /quit [<grp>]                      Leave or disconnect\n"
                << "  /help                              Show this help\n";
            sendEncrypted(sock, h.str());
            cout << "[" << timestamp() << "] [Server] Sent /help to " << username << "\n";
        }
        else if (token == "/send") {
            string targets; iss >> targets;
            string msg; getline(iss, msg);
            if (!msg.empty() && msg.front() == ' ') msg.erase(0, 1);

            string full = "[" + timestamp() + "] " + username + ": " + msg + "\n";
            {
                lock_guard<mutex> lock(mtx);
                for (auto& u : split(targets, ',')) {
                    auto it = users.find(u);
                    if (it != users.end()) {
                        sendEncrypted(it->second, full);
                    }
                }
                // senders see it too
                sendEncrypted(sock, full);
            }
            cout << "[" << timestamp() << "] [Server] /send from " << username
                << " to {" << targets << "}: " << msg << "\n";
        }
        else if (token == "/groupchat") {
            string targets, grp; iss >> targets >> grp;
            string msg; getline(iss, msg);
            if (!msg.empty() && msg.front() == ' ') msg.erase(0, 1);

            vector<string> members = split(targets, ',');
            members.push_back(username);
            {
                lock_guard<mutex> lock(mtx);
                groupOwners[grp] = username;
                for (auto& u : members) groups[grp].insert(u);
            }

            string full = "[" + timestamp() + "] " + username + ": " + msg + "\n";
            lock_guard<mutex> lock(mtx);
            for (auto& u : members) {
                auto it = users.find(u);
                if (it != users.end()) {
                    sendEncrypted(it->second, "[" + grp + "] " + full);
                }
            }
            cout << "[" << timestamp() << "] [Server] Created group '" << grp
                << "' by " << username << " with members {" << targets << "}\n";
            cout << "[" << timestamp() << "] [Server] Sent initial msg to " << grp
                << ": " << msg << "\n";
        }
        else if (token == "/sendgp") {
            string grp; iss >> grp;
            string msg; getline(iss, msg);
            if (!msg.empty() && msg.front() == ' ') msg.erase(0, 1);

            string full = "[" + timestamp() + "] " + username + ": " + msg + "\n";
            lock_guard<mutex> lock(mtx);
            for (auto& u : groups[grp]) {
                if (u != username && !paused[u].count(grp)) {
                    auto it = users.find(u);
                    if (it != users.end()) {
                        sendEncrypted(it->second, "[" + grp + "] " + full);
                    }
                }
            }
            cout << "[" << timestamp() << "] [Server] /sendgp to '" << grp
                << "' from " << username << ": " << msg << "\n";
        }
        else if (token == "/pause") {
            string grp; iss >> grp;
            lock_guard<mutex> lock(mtx);
            paused[username].insert(grp);
            sendEncrypted(sock, "Paused " + grp + "\n");
            cout << "[" << timestamp() << "] [Server] " << username
                << " paused " << grp << "\n";
        }
        else if (token == "/resume") {
            string grp; iss >> grp;
            lock_guard<mutex> lock(mtx);
            paused[username].erase(grp);
            sendEncrypted(sock, "Resumed " + grp + "\n");
            cout << "[" << timestamp() << "] [Server] " << username
                << " resumed " << grp << "\n";
        }
        else if (token == "/dismiss") {
            string grp; iss >> grp;
            lock_guard<mutex> lock(mtx);
            if (groupOwners[grp] == username) {
                for (auto& u : groups[grp]) {
                    auto it = users.find(u);
                    if (it != users.end()) {
                        sendEncrypted(it->second, "Group " + grp + " dismissed.\n");
                    }
                }
                groups.erase(grp);
                groupOwners.erase(grp);
                cout << "[" << timestamp() << "] [Server] Group " << grp
                    << " dismissed by " << username << "\n";
            }
        }
        else if (token == "mygplist") {
            ostringstream oss;
            lock_guard<mutex> lock(mtx);
            for (auto& kv : groups) {
                if (kv.second.count(username)) {
                    oss << " - " << kv.first << "\n";
                }
            }
            sendEncrypted(sock, oss.str());
            cout << "[" << timestamp() << "] [Server] Sent group list to "
                << username << "\n";
        }
        else if (token == "/quit") {
            string grp; iss >> grp;
            if (grp.empty()) {
                cout << "[" << timestamp() << "] [Server] " << username
                    << " disconnected.\n";
                break;
            }
            else {
                lock_guard<mutex> lock(mtx);
                groups[grp].erase(username);
                cout << "[" << timestamp() << "] [Server] " << username
                    << " left " << grp << "\n";
            }
        }
        else {
            sendEncrypted(sock, "Unknown command. Type /help\n");
        }
    }
    {
        lock_guard<mutex> lock(mtx);
        users.erase(username);
        for (auto& kv : groups) kv.second.erase(username);
    }
    closesocket(sock);
}

int main() {
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);

    SOCKET listener = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(PORT);

    bind(listener, (sockaddr*)&addr, sizeof(addr));
    listen(listener, MAX_CLIENTS);
    cout << "[" << timestamp() << "] [Server] Listening on port " << PORT << "\n";

    // Admin console
    thread([]() {
        string cmd;
        while (getline(cin, cmd)) {
            if (cmd == "/help") {
                cout << "Admin commands:\n"
                    << "  ckuser           List online users\n"
                    << "  ckgroup          List all groups\n"
                    << "  /help            Show this help\n";
            }
            else if (cmd == "ckuser") {
                lock_guard<mutex> lock(mtx);
                for (auto& kv : users) cout << kv.first << "\n";
            }
            else if (cmd == "ckgroup") {
                lock_guard<mutex> lock(mtx);
                for (auto& kv : groups) {
                    cout << kv.first << " (owner=" << groupOwners[kv.first] << ")\n";
                }
            }
        }
        }).detach();

    // Accept
    while (true) {
        SOCKET client = accept(listener, nullptr, nullptr);
        thread(handleClient, client).detach();
    }

    WSACleanup();
    return 0;
}
