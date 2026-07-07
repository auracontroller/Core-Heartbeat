#include <iostream>
#include <vector>
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
#define INVALID_SOCKET -1
#define SOCKET_ERROR -1
#define closesocket close
#endif

using namespace std;

enum SystemState { ENVIRONMENT_SCAN, NORMAL, SUSPENDED, RESUMING };
SystemState system_state = ENVIRONMENT_SCAN;

mutex data_mutex;
chrono::steady_clock::time_point last_fuse_reset;
int strikes = 0;
bool should_speak = false;

SOCKET connect_to_core(int& my_port_id) {
    SOCKET sock = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(5555);
    inet_pton(AF_INET, "127.0.0.1", &server_addr.sin_addr);

    if (connect(sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) == SOCKET_ERROR) {
        cerr << "Sister 2: Connection failed." << endl << flush;
        return INVALID_SOCKET;
    }

    char buffer;
    int bytes_read = recv(sock, buffer, sizeof(buffer) - 1, 0);
    if (bytes_read > 0) {
        buffer[bytes_read] = '\0';
        string welcome(buffer);
        size_t pos = welcome.find("DOCK:");
        if (pos != string::npos) {
            size_t end_pos = welcome.find('\n', pos);
            if (end_pos != string::npos) {
                my_port_id = stoi(welcome.substr(pos + 5, end_pos - (pos + 5)));
                cout << "Sister 2 connected. Assigned Dock ID: " << my_port_id << endl << flush;
            }
        }
    }
    return sock;
}

void receive_loop(SOCKET sock) {
    char buffer;
    string leftover = "";

    while (true) {
        int bytes_read = recv(sock, buffer, sizeof(buffer) - 1, 0);
        if (bytes_read <= 0) {
            cerr << "Sister 2: CoreRelay dropped. Violently freeing RAM." << endl << flush;
            exit(0);
        }

        buffer[bytes_read] = '\0';
        leftover += buffer;
        
        // Anti-Overflow Truncation Strategy
        if (leftover.size() > 8192) {
            leftover.erase(0, leftover.size() - 4096);
        }
        
        size_t pos;
        while ((pos = leftover.find('\n')) != string::npos) {
            string message = leftover.substr(0, pos);
            leftover = leftover.substr(pos + 1);

            size_t from_pos = message.find("FROM:");
            size_t payload_pos = message.find("|PAYLOAD:");

            if (from_pos != string::npos && payload_pos != string::npos) {
                string payload = message.substr(payload_pos + 9);

                lock_guard<mutex> lock(data_mutex);

                if (payload == "SUSPEND_ALL" && system_state != SUSPENDED) {
                    cout << "Sister 2: External SUSPEND caught. Locking lattice." << endl << flush;
                    system_state = SUSPENDED;
                }
                else if (payload == "RESUME") {
                    cout << "Sister 2: RESUME caught. Thawing lattice." << endl << flush;
                    system_state = NORMAL;
                    strikes = 0;
                    last_fuse_reset = chrono::steady_clock::now();
                    should_speak = true;
                }
                else if (payload == "PULSE" || payload == "ALIVE") {
                    if (system_state == ENVIRONMENT_SCAN) {
                        cout << "Sister 2: Ignition caught. Joining rhythm." << endl << flush;
                    }
                    if (system_state == ENVIRONMENT_SCAN || system_state == NORMAL) {
                        system_state = NORMAL;
                        strikes = 0;
                        last_fuse_reset = chrono::steady_clock::now();
                        should_speak = true; 
                    }
                }
            }
        }
    }
}

int main() {
#ifdef _WIN32
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);
#endif

    int my_port_id = -1;
    SOCKET sock = connect_to_core(my_port_id);
    if (sock == INVALID_SOCKET) return 1;

    thread recv_thread(receive_loop, sock);
    recv_thread.detach();

    cout << "Sister 2: Booting up. Standing in the cave, listening for ignition..." << endl << flush;

    while (true) {
        this_thread::sleep_for(chrono::milliseconds(50)); 

        lock_guard<mutex> lock(data_mutex);

        if (system_state == NORMAL) {
            auto now = chrono::steady_clock::now();
            auto elapsed = chrono::duration_cast<chrono::milliseconds>(now - last_fuse_reset).count();

            if (elapsed > 1000) { 
                strikes++;
                last_fuse_reset = now; 

                if (strikes == 1) {
                    cout << "Sister 2: Warning. Echo dropped. Strike 1. Firing secondary pulse." << endl << flush;
                    should_speak = true;
                }
                else if (strikes >= 2) {
                    cout << "Sister 2: Critical Silence. Strike 2 threshold breached. FIRING SUSPEND_ALL." << endl << flush;
                    system_state = SUSPENDED;
                    should_speak = false;
                }
            }

            if (should_speak) {
                data_mutex.unlock();
                this_thread::sleep_for(chrono::milliseconds(250));
                data_mutex.lock();

                if (system_state == NORMAL) {
                    string pulse_msg = "TARGET:ALL|PAYLOAD:PULSE\n";
                    #ifdef _WIN32
                    send(sock, pulse_msg.c_str(), pulse_msg.length(), 0);
                    #else
                    send(sock, pulse_msg.c_str(), pulse_msg.length(), MSG_NOSIGNAL);
                    #endif
                    
                    should_speak = false;
                    last_fuse_reset = chrono::steady_clock::now(); 
                }
            }
        }
        else if (system_state == SUSPENDED) {
            string suspend_msg = "TARGET:ALL|PAYLOAD:SUSPEND_ALL\n";
            #ifdef _WIN32
            send(sock, suspend_msg.c_str(), suspend_msg.length(), 0);
            #else
            send(sock, suspend_msg.c_str(), suspend_msg.length(), MSG_NOSIGNAL);
            #endif
            
            data_mutex.unlock();
            this_thread::sleep_for(chrono::milliseconds(1000));
            data_mutex.lock();
        }
    }

#ifdef _WIN32
    WSACleanup();
#endif
    return 0;
}