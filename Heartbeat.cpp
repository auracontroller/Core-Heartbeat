#include <iostream>
#include <string>
#include <thread>
#include <chrono>
#include <map>
#include <mutex>
#include <vector>

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

map<int, int> active_ledger;
map<int, int> suspended_ledger;
map<int, chrono::steady_clock::time_point> last_seen;
mutex data_mutex;

void listen_to_core(SOCKET sock) {
    char buffer[1024];
    string leftover = "";
    while (true) {
        int bytes = recv(sock, buffer, sizeof(buffer)-1, 0);
        if (bytes <= 0) break;
        buffer[bytes] = '\0';
        leftover += buffer;

        size_t pos;
        while ((pos = leftover.find('\n')) != string::npos) {
            string msg = leftover.substr(0, pos);
            leftover = leftover.substr(pos + 1);

            size_t from_pos = msg.find("FROM:");
            size_t payload_pos = msg.find("|PAYLOAD:");
            if (from_pos != string::npos && payload_pos != string::npos) {
                try {
                    int from_id = stoi(msg.substr(from_pos + 5, payload_pos - (from_pos + 5)));
                    string payload = msg.substr(payload_pos + 9);

                    lock_guard<mutex> lock(data_mutex);

                    if (payload == "INTENTIONAL_SHUTDOWN") {
                        active_ledger.erase(from_id);
                        suspended_ledger.erase(from_id);
                        last_seen.erase(from_id);
                        cout << "Heartbeat: Total Amnesia enacted for Dock " << from_id << endl;
                        continue;
                    }

                    auto now = chrono::steady_clock::now();
                    if (last_seen.find(from_id) != last_seen.end()) {
                        auto delta = chrono::duration_cast<chrono::milliseconds>(now - last_seen[from_id]).count();
                        if (delta < 2000 && payload.find("PACT:") != 0) {
                            string lock_cmd = "TARGET:CORE|PAYLOAD:LOCK_DOCK:" + to_string(from_id) + "\n";
                            send(sock, lock_cmd.c_str(), lock_cmd.length(), 0);
                            cout << "Heartbeat: Spam detected from Dock " << from_id << " (" << delta << "ms). Locking dock." << endl;
                        }
                    }
                    last_seen[from_id] = now;

                    if (payload.find("PACT:") == 0) {
                        int interval = stoi(payload.substr(5));
                        active_ledger[from_id] = interval;
                        cout << "Heartbeat: Pact established with Dock " << from_id << " at " << interval << "ms." << endl;
                    }
                } catch (...) {}
            }
        }
    }
}

int main() {
#ifdef _WIN32
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);
#endif

    SOCKET sock = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in server_addr = {};
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(5555);
    inet_pton(AF_INET, "127.0.0.1", &server_addr.sin_addr);
    connect(sock, (struct sockaddr *)&server_addr, sizeof(server_addr));

    thread listener(listen_to_core, sock);
    listener.detach();

    cout << "Heartbeat active. Anchoring rhythm..." << endl;

    while (true) {
        // Send pulse to Sister (Dock 2) every 1000ms
        string pulse_msg = "TARGET:2|PAYLOAD:PULSE\n";
        send(sock, pulse_msg.c_str(), pulse_msg.length(), 0);

        auto pulse_start = chrono::steady_clock::now();

        {
            lock_guard<mutex> lock(data_mutex);
            auto now = chrono::steady_clock::now();

            // Check dynamic pacts in Active Ledger
            vector<int> to_suspend;
            for (auto const& [dock_id, interval] : active_ledger) {
                if (last_seen.find(dock_id) != last_seen.end()) {
                    auto elapsed = chrono::duration_cast<chrono::milliseconds>(now - last_seen[dock_id]).count();
                    if (elapsed > interval) {
                        cout << "Heartbeat: Dock " << dock_id << " missed pact window! Moving to Suspended Ledger." << endl;
                        to_suspend.push_back(dock_id);
                    }
                }
            }

            for (int dock_id : to_suspend) {
                suspended_ledger[dock_id] = active_ledger[dock_id];
                active_ledger.erase(dock_id);
            }

            // Check Suspended Ledger for Global Freeze
            bool trigger_global_freeze = false;
            for (auto const& [dock_id, interval] : suspended_ledger) {
                if (last_seen.find(dock_id) != last_seen.end()) {
                    auto elapsed = chrono::duration_cast<chrono::milliseconds>(now - last_seen[dock_id]).count();
                    // If it misses its interval a second time (meaning elapsed > 2 * interval)
                    if (elapsed > (2 * interval)) {
                        cout << "Heartbeat: Dock " << dock_id << " missed pact window twice! Broadcasting SUSPEND_ALL." << endl;
                        trigger_global_freeze = true;
                        break;
                    }
                }
            }

            if (trigger_global_freeze) {
                string suspend_all = "TARGET:ALL|PAYLOAD:SUSPEND_ALL\n";
                send(sock, suspend_all.c_str(), suspend_all.length(), 0);
                // In a real scenario, this might halt Heartbeat entirely. For test, we just broadcast.
            }
        }

        auto pulse_end = chrono::steady_clock::now();
        auto processing_time = chrono::duration_cast<chrono::milliseconds>(pulse_end - pulse_start).count();
        int sleep_time = 1000 - processing_time;
        if (sleep_time > 0) {
            this_thread::sleep_for(chrono::milliseconds(sleep_time));
        }
    }
    return 0;
}