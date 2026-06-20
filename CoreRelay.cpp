#include <iostream>
#include <vector>
#include <string>
#include <thread>
#include <mutex>
#include <map>
#include <algorithm>
#include <sstream>
#include <cstring>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <arpa/inet.h>
#define SOCKET int
#define INVALID_SOCKET -1
#define SOCKET_ERROR -1
#define closesocket close
#endif

using namespace std;

mutex clients_mutex;
map<int, SOCKET> clients;
int heartbeat_port_id = -1;
int next_port_id = 1;

void purge_all_clients_except_heartbeat_mutex_held() {
    cout << "CoreRelay: Heartbeat disconnected. Executing TOTAL PURGE." << endl << flush;
    for (auto it = clients.begin(); it != clients.end(); ) {
        if (it->first != heartbeat_port_id) {
            closesocket(it->second);
            it = clients.erase(it);
        } else {
            ++it;
        }
    }
    heartbeat_port_id = -1;
}

void handle_client(int port_id, SOCKET client_socket) {
    char buffer[4096];
    string leftover = "";
    bool identified = false;

    while (true) {
        int bytes_read = recv(client_socket, buffer, sizeof(buffer) - 1, 0);
        if (bytes_read <= 0) {
            // Client disconnected
            lock_guard<mutex> lock(clients_mutex);
            if (clients.find(port_id) != clients.end()) {
                clients.erase(port_id);
                closesocket(client_socket);
                cout << "CoreRelay: Client " << port_id << " disconnected." << endl << flush;

                if (port_id == heartbeat_port_id) {
                    purge_all_clients_except_heartbeat_mutex_held();
                }
            }
            break;
        }

        buffer[bytes_read] = '\0';
        leftover += buffer;

        size_t pos;
        while ((pos = leftover.find('\n')) != string::npos) {
            string message = leftover.substr(0, pos);
            leftover = leftover.substr(pos + 1);

            if (!identified) {
                // First message must be identity
                if (message == "I_AM_HEARTBEAT") {
                    lock_guard<mutex> lock(clients_mutex);
                    heartbeat_port_id = port_id;
                    cout << "CoreRelay: Port " << port_id << " identified as HEARTBEAT." << endl << flush;
                } else {
                    cout << "CoreRelay: Port " << port_id << " identified as MODULE (" << message << ")." << endl << flush;
                }
                identified = true;
                continue;
            }

            // Expected format: TARGET:<target>|PAYLOAD:<payload>
            string target_str = "";
            string payload_str = "";

            size_t target_pos = message.find("TARGET:");
            size_t payload_pos = message.find("|PAYLOAD:");

            if (target_pos != string::npos && payload_pos != string::npos) {
                target_str = message.substr(target_pos + 7, payload_pos - (target_pos + 7));
                payload_str = message.substr(payload_pos + 9);

                string forwarded_message = "FROM:" + to_string(port_id) + "|PAYLOAD:" + payload_str + "\n";

                lock_guard<mutex> lock(clients_mutex);
                if (target_str == "ALL") {
                    for (auto const& [id, sock] : clients) {
                        if (id != port_id) {
                            send(sock, forwarded_message.c_str(), forwarded_message.length(), 0);
                        }
                    }
                } else {
                    try {
                        int target_id = stoi(target_str);
                        if (clients.find(target_id) != clients.end()) {
                            send(clients[target_id], forwarded_message.c_str(), forwarded_message.length(), 0);
                        }
                    } catch (...) {
                        // Invalid target
                    }
                }
            }
        }
    }
}

int main() {
#ifdef _WIN32
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        cerr << "WSAStartup failed." << endl << flush;
        return 1;
    }
#endif

    SOCKET server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket == INVALID_SOCKET) {
        cerr << "Could not create socket." << endl << flush;
        return 1;
    }

    int opt = 1;
#ifdef _WIN32
    setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt));
#else
    setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
#endif

    sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(5555);

    if (bind(server_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) == SOCKET_ERROR) {
        cerr << "Bind failed." << endl << flush;
        return 1;
    }

    if (listen(server_socket, SOMAXCONN) == SOCKET_ERROR) {
        cerr << "Listen failed." << endl << flush;
        return 1;
    }

    cout << "CoreRelay started on port 5555." << endl << flush;

    while (true) {
        sockaddr_in client_addr;
#ifdef _WIN32
        int client_len = sizeof(client_addr);
#else
        socklen_t client_len = sizeof(client_addr);
#endif
        SOCKET client_socket = accept(server_socket, (struct sockaddr *)&client_addr, &client_len);

        if (client_socket == INVALID_SOCKET) {
            cerr << "Accept failed." << endl << flush;
            continue;
        }

        int current_port_id;
        {
            lock_guard<mutex> lock(clients_mutex);
            current_port_id = next_port_id++;
            clients[current_port_id] = client_socket;
        }

        cout << "CoreRelay: Client connected, assigned Port ID: " << current_port_id << endl << flush;

        string welcome_msg = "PORT:" + to_string(current_port_id) + "\n";
        send(client_socket, welcome_msg.c_str(), welcome_msg.length(), 0);

        thread t(handle_client, current_port_id, client_socket);
        t.detach();
    }

#ifdef _WIN32
    WSACleanup();
#endif

    return 0;
}
