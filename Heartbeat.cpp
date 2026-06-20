#include <iostream>
#include <vector>
#include <string>
#include <thread>
#include <mutex>
#include <set>
#include <map>
#include <chrono>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <fcntl.h>
#define SOCKET int
#define INVALID_SOCKET -1
#define SOCKET_ERROR -1
#define closesocket close
#endif

using namespace std;

SOCKET connect_to_core(int& my_port_id) {
    SOCKET sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == INVALID_SOCKET) {
        cerr << "Heartbeat: Could not create socket." << endl << flush;
        return INVALID_SOCKET;
    }

    sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(5555);
    inet_pton(AF_INET, "127.0.0.1", &server_addr.sin_addr);

    if (connect(sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) == SOCKET_ERROR) {
        cerr << "Heartbeat: Connection failed." << endl << flush;
        return INVALID_SOCKET;
    }

    char buffer[1024];
    int bytes_read = recv(sock, buffer, sizeof(buffer) - 1, 0);
    if (bytes_read > 0) {
        buffer[bytes_read] = '\0';
        string welcome(buffer);
        size_t pos = welcome.find("PORT:");
        if (pos != string::npos) {
            size_t end_pos = welcome.find('\n', pos);
            if (end_pos != string::npos) {
                my_port_id = stoi(welcome.substr(pos + 5, end_pos - (pos + 5)));
                cout << "Heartbeat connected to CoreRelay. Assigned Port ID: " << my_port_id << endl << flush;
            }
        }
    }

    string identity = "I_AM_HEARTBEAT\n";
    send(sock, identity.c_str(), identity.length(), 0);

    return sock;
}

mutex data_mutex;
set<int> previous_pulse;
set<int> current_pulse;
map<int, int> miss_cache;

enum SystemState { NORMAL, SUSPENDED };
SystemState system_state = NORMAL;

void receive_loop(SOCKET sock) {
    char buffer[4096];
    string leftover = "";

    while (true) {
        int bytes_read = recv(sock, buffer, sizeof(buffer) - 1, 0);
        if (bytes_read <= 0) {
            cerr << "Heartbeat: CoreRelay disconnected. Exiting to violently free RAM." << endl << flush;
            exit(0);
        }

        buffer[bytes_read] = '\0';
        leftover += buffer;

        size_t pos;
        while ((pos = leftover.find('\n')) != string::npos) {
            string message = leftover.substr(0, pos);
            leftover = leftover.substr(pos + 1);

            size_t from_pos = message.find("FROM:");
            size_t payload_pos = message.find("|PAYLOAD:");

            if (from_pos != string::npos && payload_pos != string::npos) {
                int sender_id = stoi(message.substr(from_pos + 5, payload_pos - (from_pos + 5)));
                string payload = message.substr(payload_pos + 9);

                lock_guard<mutex> lock(data_mutex);
                if (system_state == NORMAL) {
                    if (payload == "ALIVE") {
                        current_pulse.insert(sender_id);
                    }
                } else if (system_state == SUSPENDED) {
                    if (payload == "ALREADY_SUSPENDED" || payload == "UNSUSPENDABLE") {
                        current_pulse.insert(sender_id);
                    }
                }

                if (payload == "RESUME") {
                    if (system_state == SUSPENDED) {
                        cout << "Heartbeat: Received RESUME from Port " << sender_id << ". Flipping system_state back to NORMAL." << endl << flush;
                        system_state = NORMAL;
                        previous_pulse.clear();
                        current_pulse.clear();
                        miss_cache.clear();
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
        cerr << "Heartbeat: WSAStartup failed." << endl << flush;
        return 1;
    }
#endif

    int my_port_id = -1;
    SOCKET sock = connect_to_core(my_port_id);
    if (sock == INVALID_SOCKET) return 1;

    thread recv_thread(receive_loop, sock);
    recv_thread.detach();

    while (true) {
        this_thread::sleep_for(chrono::milliseconds(1000));

        lock_guard<mutex> lock(data_mutex);

        if (system_state == NORMAL) {
            // First Miss Trigger
            for (int port_id : previous_pulse) {
                if (current_pulse.find(port_id) == current_pulse.end()) {
                    if (miss_cache.find(port_id) == miss_cache.end()) {
                        cout << "Heartbeat: Port " << port_id << " missed a pulse." << endl << flush;
                        miss_cache[port_id] = 1;
                    }
                }
            }

            // Subsequent Miss Trigger & Redemption
            bool suspend_triggered = false;
            for (auto it = miss_cache.begin(); it != miss_cache.end(); ) {
                int missing_port_id = it->first;

                if (current_pulse.find(missing_port_id) != current_pulse.end()) {
                    // Redemption
                    it = miss_cache.erase(it);
                } else {
                    // Still missing
                    if (it->second == 1 && previous_pulse.find(missing_port_id) == previous_pulse.end()) {
                        // It was missed in a PREVIOUS cycle and is still missing now. Increment to 2.
                        // Actually, if we just inserted it above, we don't want to immediately increment it to 2.
                        // So we only increment if it wasn't just inserted.
                        // A simple way to check if it was just inserted is if it was in previous_pulse.
                        // If it's in previous_pulse, it just missed its first.
                        // If it's not in previous_pulse, it missed an older one and is still missing.
                        it->second = 2;
                    }

                    if (it->second >= 2) {
                        cout << "Heartbeat: Port " << missing_port_id << " missed 2 consecutive pulses. Triggering SYSTEM FREEZE." << endl << flush;
                        suspend_triggered = true;
                        break;
                    }
                    ++it;
                }
            }

            if (suspend_triggered) {
                system_state = SUSPENDED;
                previous_pulse.clear();
                current_pulse.clear();
                miss_cache.clear();

                string suspend_msg = "TARGET:ALL|PAYLOAD:SUSPEND_ALL\n";
                send(sock, suspend_msg.c_str(), suspend_msg.length(), 0);
            } else {
                previous_pulse = current_pulse;
                current_pulse.clear();

                string pulse_msg = "TARGET:ALL|PAYLOAD:PULSE\n";
                send(sock, pulse_msg.c_str(), pulse_msg.length(), 0);
            }
        } else if (system_state == SUSPENDED) {
            previous_pulse = current_pulse;
            current_pulse.clear();

            string suspend_msg = "TARGET:ALL|PAYLOAD:SUSPEND_ALL\n";
            send(sock, suspend_msg.c_str(), suspend_msg.length(), 0);
        }
    }

#ifdef _WIN32
    WSACleanup();
#endif

    return 0;
}
