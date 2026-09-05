#include <iostream>
#include <vector>
#include <string>
#include <thread>
#include <mutex>
#include <map>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <signal.h>
#define SOCKET int
#define INVALID_SOCKET -1
#define SOCKET_ERROR -1
#define closesocket close
#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif
#endif

using namespace std;

mutex clients_mutex;
map<int, SOCKET> midju_vreji; // Core registry
int next_dock_id = 1;

map<int, bool> locked_docks;

void handle_client(int dock_id, SOCKET client_socket) {
    char buffer[1024];
    string leftover = "";

    while (true) {
        int bytes_read = recv(client_socket, buffer, sizeof(buffer) - 1, 0);
        if (bytes_read <= 0) break;

        buffer[bytes_read] = '\0';
        leftover += buffer;

        size_t pos;
        while ((pos = leftover.find('\n')) != string::npos) {
            string message = leftover.substr(0, pos);
            leftover = leftover.substr(pos + 1);

            // Forward everything appropriately. If Dock > 1 connects, we tell Heartbeat (Dock 1).
            size_t target_pos = message.find("TARGET:");
            size_t payload_pos = message.find("|PAYLOAD:");

            if (target_pos != string::npos && payload_pos != string::npos && payload_pos > target_pos + 7) {
                string target_str = message.substr(target_pos + 7, payload_pos - (target_pos + 7));
                string payload_str = message.substr(payload_pos + 9);

                // Mail Slot Logic
                if (target_str == "CORE") {
                    if (payload_str.find("LOCK_DOCK:") == 0) {
                        try {
                            int lock_id = stoi(payload_str.substr(10));
                            lock_guard<mutex> lock(clients_mutex);
                            locked_docks[lock_id] = true;
                            cout << "Core: Locked outbound mail slot for Dock " << lock_id << endl;
                        } catch (...) {}
                    }
                    continue;
                }

                // If this listening thread is locked, drop outbound into the void
                {
                    lock_guard<mutex> lock(clients_mutex);
                    if (locked_docks[dock_id]) {
                        continue;
                    }
                }

                string forwarded_message = "FROM:" + to_string(dock_id) + "|" + message.substr(target_pos) + "\n";
                lock_guard<mutex> lock(clients_mutex);

                if (target_str == "ALL") {
                    for (auto const& [id, sock] : midju_vreji) {
                        if (id != dock_id) {
                            send(sock, forwarded_message.c_str(), forwarded_message.length(), MSG_NOSIGNAL);
                        }
                    }
                } else {
                    try {
                        int target_id = stoi(target_str);
                        if (midju_vreji.find(target_id) != midju_vreji.end()) {
                            send(midju_vreji[target_id], forwarded_message.c_str(), forwarded_message.length(), MSG_NOSIGNAL);
                        }
                    } catch (...) {}
                }
            }
        }
    }

    // Core does not remove dead sockets from registry or memory! Mail slot stays open for inbound.
    // However, the reading loop breaks if recv fails.
}

int main() {
#ifdef _WIN32
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);
#else
    signal(SIGPIPE, SIG_IGN); // Ignored globally to suppress dead socket crashes
#endif

    SOCKET server_socket = socket(AF_INET, SOCK_STREAM, 0);
    int opt = 1;
    setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt));

    sockaddr_in server_addr = {};
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(5555);

    bind(server_socket, (struct sockaddr *)&server_addr, sizeof(server_addr));
    listen(server_socket, SOMAXCONN);

    cout << "Core routing lattice active." << endl;

    while (true) {
        sockaddr_in client_addr = {};
        socklen_t client_len = sizeof(client_addr);
        SOCKET client_socket = accept(server_socket, (struct sockaddr *)&client_addr, &client_len);

        int current_dock_id;
        {
            lock_guard<mutex> lock(clients_mutex);
            current_dock_id = next_dock_id++;
            midju_vreji[current_dock_id] = client_socket;
        }

        cout << "Core: Dock " << current_dock_id << " connected." << endl;
        string welcome_msg = "DOCK:" + to_string(current_dock_id) + "\n";
        send(client_socket, welcome_msg.c_str(), welcome_msg.length(), 0);

        // Notify Heartbeat (Dock 1) of new connections
        if (current_dock_id > 1) {
            string alert = "TARGET:1|PAYLOAD:NEW_DOCK:" + to_string(current_dock_id) + "\n";
            send(midju_vreji[1], alert.c_str(), alert.length(), 0);
        }

        thread t(handle_client, current_dock_id, client_socket);
        t.detach();
    }
    return 0;
}