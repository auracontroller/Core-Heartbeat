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
#endif

using namespace std;

mutex clients_mutex;
map<int, SOCKET> clients;
int next_dock_id = 1;

void handle_client(int dock_id, SOCKET client_socket) {
    char buffer;
    string leftover = "";

    while (true) {
        int bytes_read = recv(client_socket, buffer, sizeof(buffer) - 1, 0);
        
        // If the OS physical socket breaks (existential crash), the thread stops reading.
        // The Dock ID remains perfectly intact within the clients map (Ghost Dock).
        if (bytes_read <= 0) {
            break; 
        }

        buffer[bytes_read] = '\0';
        leftover += buffer;

        size_t pos;
        while ((pos = leftover.find('\n')) != string::npos) {
            string message = leftover.substr(0, pos);
            leftover = leftover.substr(pos + 1);

            size_t target_pos = message.find("TARGET:");
            size_t payload_pos = message.find("|PAYLOAD:");

            if (target_pos != string::npos && payload_pos != string::npos) {
                string target_str = message.substr(target_pos + 7, payload_pos - (target_pos + 7));
                string payload_str = message.substr(payload_pos + 9);

                // Core Authority: Explicit Dock Management
                if (target_str == "CORE") {
                    if (payload_str.find("CLOSE_DOCK:") == 0) {
                        try {
                            int dock_to_close = stoi(payload_str.substr(11));
                            lock_guard<mutex> lock(clients_mutex);
                            if (clients.find(dock_to_close) != clients.end()) {
                                closesocket(clients[dock_to_close]);
                                clients.erase(dock_to_close);
                                cout << "CoreRelay: Explicit command received. Dock " << dock_to_close << " severed." << endl << flush;
                            }
                        } catch (...) {
                            // Invalid ID, drop trailing noise silently
                        }
                    } else if (payload_str == "GOODBYE") {
                        lock_guard<mutex> lock(clients_mutex);
                        if (clients.find(dock_id) != clients.end()) {
                            closesocket(clients[dock_id]);
                            clients.erase(dock_id);
                            cout << "CoreRelay: Departure logged. Dock " << dock_id << " severed." << endl << flush;
                        }
                        return; // Exit thread cleanly
                    }
                    continue; // Do not route CORE commands back into the lattice
                }

                // Standard Routing Lattice
                string forwarded_message = "FROM:" + to_string(dock_id) + "|PAYLOAD:" + payload_str + "\n";

                lock_guard<mutex> lock(clients_mutex);
                if (target_str == "ALL") {
                    for (auto const& [id, sock] : clients) {
                        if (id != dock_id) {
                            // Send silently ignores if the Ghost Dock is physically broken
                            #ifdef _WIN32
                            send(sock, forwarded_message.c_str(), forwarded_message.length(), 0);
                            #else
                            send(sock, forwarded_message.c_str(), forwarded_message.length(), MSG_NOSIGNAL);
                            #endif
                        }
                    }
                } else {
                    try {
                        int target_id = stoi(target_str);
                        if (clients.find(target_id) != clients.end()) {
                            #ifdef _WIN32
                            send(clients[target_id], forwarded_message.c_str(), forwarded_message.length(), 0);
                            #else
                            send(clients[target_id], forwarded_message.c_str(), forwarded_message.length(), MSG_NOSIGNAL);
                            #endif
                        }
                    } catch (...) {
                        // Invalid target, drop trailing noise silently
                    }
                }
            }
        }
    }
}

int main() {
#ifdef _WIN32
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) return 1;
#else
    // Prevent Linux from crashing if the Core tries to send to a dead Ghost Dock
    signal(SIGPIPE, SIG_IGN);
#endif

    SOCKET server_socket = socket(AF_INET, SOCK_STREAM, 0);
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

    bind(server_socket, (struct sockaddr *)&server_addr, sizeof(server_addr));
    listen(server_socket, SOMAXCONN);
    
    cout << "CoreRelay started on port 5555. Pure Routing Lattice Active." << endl << flush;

    while (true) {
        sockaddr_in client_addr;
#ifdef _WIN32
        int client_len = sizeof(client_addr);
#else
        socklen_t client_len = sizeof(client_addr);
#endif
        SOCKET client_socket = accept(server_socket, (struct sockaddr *)&client_addr, &client_len);

        int current_dock_id;
        {
            lock_guard<mutex> lock(clients_mutex);
            current_dock_id = next_dock_id++;
            clients[current_dock_id] = client_socket;
        }

        cout << "CoreRelay: Module connected to Dock " << current_dock_id << endl << flush;
        
        // Note: The welcome message now sends "DOCK:" instead of "PORT:". 
        // Sister nodes will need to be updated to scan for "DOCK:" in the new chat.
        string welcome_msg = "DOCK:" + to_string(current_dock_id) + "\n";
        
        #ifdef _WIN32
        send(client_socket, welcome_msg.c_str(), welcome_msg.length(), 0);
        #else
        send(client_socket, welcome_msg.c_str(), welcome_msg.length(), MSG_NOSIGNAL);
        #endif

        thread t(handle_client, current_dock_id, client_socket);
        t.detach();
    }

#ifdef _WIN32
    WSACleanup();
#endif
    return 0;
}