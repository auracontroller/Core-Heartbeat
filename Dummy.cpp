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

SOCKET connect_to_core(int& my_port_id, int mode) {
    SOCKET sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == INVALID_SOCKET) {
        cerr << "Dummy: Could not create socket." << endl << flush;
        return INVALID_SOCKET;
    }

    sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(5555);
    inet_pton(AF_INET, "127.0.0.1", &server_addr.sin_addr);

    if (connect(sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) == SOCKET_ERROR) {
        cerr << "Dummy: Connection failed." << endl << flush;
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
                cout << "Dummy connected to CoreRelay. Assigned Port ID: " << my_port_id << endl << flush;
            }
        }
    }

    string identity = "I_AM_MODULE_DUMMY_" + to_string(mode) + "\n";
    send(sock, identity.c_str(), identity.length(), 0);

    return sock;
}

int main(int argc, char* argv[]) {
    if (argc != 2) {
        cerr << "Usage: Dummy <mode (1, 2, or 3)>" << endl << flush;
        return 1;
    }

    int mode = stoi(argv[1]);
    if (mode < 1 || mode > 3) {
        cerr << "Mode must be 1, 2, or 3." << endl << flush;
        return 1;
    }

#ifdef _WIN32
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        cerr << "Dummy: WSAStartup failed." << endl << flush;
        return 1;
    }
#endif

    int my_port_id = -1;
    SOCKET sock = connect_to_core(my_port_id, mode);
    if (sock == INVALID_SOCKET) return 1;

    cout << "Dummy Node " << mode << " running." << endl << flush;

    bool is_suspended = false;
    int suspended_pulse_count = 0;
    bool has_crashed = false; // specifically for Dummy 1

    char buffer[4096];
    string leftover = "";

    while (true) {
        int bytes_read = recv(sock, buffer, sizeof(buffer) - 1, 0);
        if (bytes_read <= 0) {
            cerr << "Dummy " << mode << ": CoreRelay disconnected. Exiting to violently free RAM." << endl << flush;
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

                if (payload == "PULSE") {
                    if (is_suspended) {
                        cout << "Dummy " << mode << ": Received normal PULSE. Auto-resetting suspension state." << endl << flush;
                        is_suspended = false;
                        suspended_pulse_count = 0;
                        if (mode == 1) {
                            has_crashed = false; // Reboot logic for Dummy 1
                        }
                    }

                    if (mode == 1) {
                        if (!has_crashed) {
                            string msg = "TARGET:" + to_string(sender_id) + "|PAYLOAD:ALIVE\n";
                            send(sock, msg.c_str(), msg.length(), 0);
                            has_crashed = true;
                        }
                        // If has_crashed is true, return nothing
                    } else if (mode == 2 || mode == 3) {
                        string msg = "TARGET:" + to_string(sender_id) + "|PAYLOAD:ALIVE\n";
                        send(sock, msg.c_str(), msg.length(), 0);
                    }
                } else if (payload == "SUSPEND_ALL") {
                    if (!is_suspended) {
                        cout << "Dummy " << mode << ": Received SUSPEND_ALL. Transitioning to suspended state." << endl << flush;
                        is_suspended = true;
                        suspended_pulse_count = 0;
                    }

                    if (mode == 1) {
                        string msg = "TARGET:" + to_string(sender_id) + "|PAYLOAD:ALREADY_SUSPENDED\n";
                        send(sock, msg.c_str(), msg.length(), 0);
                    } else if (mode == 2) {
                        string msg = "TARGET:" + to_string(sender_id) + "|PAYLOAD:ALREADY_SUSPENDED\n";
                        send(sock, msg.c_str(), msg.length(), 0);
                    } else if (mode == 3) {
                        suspended_pulse_count++;
                        string msg1 = "TARGET:" + to_string(sender_id) + "|PAYLOAD:UNSUSPENDABLE\n";
                        send(sock, msg1.c_str(), msg1.length(), 0);

                        if (suspended_pulse_count == 2) {
                            string msg2 = "TARGET:" + to_string(sender_id) + "|PAYLOAD:RESUME\n";
                            send(sock, msg2.c_str(), msg2.length(), 0);
                            cout << "Dummy 3: Issued RESUME command." << endl << flush;
                        }
                    }
                }
            }
        }
    }

#ifdef _WIN32
    WSACleanup();
#endif

    return 0;
}
