#include <iostream>
#include <string>
#include <thread>
#include <mutex>
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
#define SOCKET int
#endif

using namespace std;

mutex data_mutex;
chrono::steady_clock::time_point last_heartbeat_time;
SOCKET core_sock;

void receive_loop() {
    char buffer[1024];
    string leftover = "";
    while (true) {
        int bytes = recv(core_sock, buffer, sizeof(buffer)-1, 0);
        if (bytes <= 0) exit(0);
        
        buffer[bytes] = '\0';
        leftover += buffer;

        size_t pos;
        while ((pos = leftover.find('\n')) != string::npos) {
            string msg = leftover.substr(0, pos);
            leftover = leftover.substr(pos + 1);

            if (msg.find("PAYLOAD:PULSE") != string::npos) {
                lock_guard<mutex> lock(data_mutex);
                last_heartbeat_time = chrono::steady_clock::now();
                
                // Immediately return the loop
                string reply = "TARGET:1|PAYLOAD:YES\n";
                send(core_sock, reply.c_str(), reply.length(), 0);
            }
        }
    }
}

int main() {
#ifdef _WIN32
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);
#endif

    core_sock = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in server_addr = {};
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(5555);
    inet_pton(AF_INET, "127.0.0.1", &server_addr.sin_addr);
    connect(core_sock, (struct sockaddr *)&server_addr, sizeof(server_addr));

    last_heartbeat_time = chrono::steady_clock::now();
    thread recv_thread(receive_loop);
    recv_thread.detach();

    cout << "Sister Watchdog active. Monitoring 1000ms rhythm..." << endl;

    while (true) {
        this_thread::sleep_for(chrono::milliseconds(50));

        lock_guard<mutex> lock(data_mutex);
        auto now = chrono::steady_clock::now();
        auto elapsed = chrono::duration_cast<chrono::milliseconds>(now - last_heartbeat_time).count();

        // 1000ms absolute limit
        if (elapsed > 1000) {
            cout << "Sister: Heartbeat silent! Executing SUSPEND_ALL." << endl;
            string suspend = "TARGET:ALL|PAYLOAD:SUSPEND_ALL\n";
            send(core_sock, suspend.c_str(), suspend.length(), 0);
            
            // Wait to prevent spamming
            this_thread::sleep_for(chrono::milliseconds(5000));
        }
    }
    return 0;
}