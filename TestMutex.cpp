#include <iostream>
#include <string>
#include <thread>
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

    if (connect(sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) == SOCKET_ERROR) {
        cerr << "TestMutex: Failed to connect to CoreRelay." << endl;
        return 1;
    }

    cout << "TestMutex connected." << endl;

    // Set up a pact
    string pact = "TARGET:1|PAYLOAD:PACT:5000\n";
    send(sock, pact.c_str(), pact.length(), 0);

    // Spam ALIVE messages to trigger the < 2000ms Throttle rule
    auto start = chrono::steady_clock::now();
    int count = 0;
    bool is_muted = false;
    char buffer[1024];

    // Make socket non-blocking for the reading loop
    #ifdef _WIN32
    u_long mode = 1;
    ioctlsocket(sock, FIONBIO, &mode);
    #else
    int flags = fcntl(sock, F_GETFL, 0);
    fcntl(sock, F_SETFL, flags | O_NONBLOCK);
    #endif

    while (chrono::duration_cast<chrono::seconds>(chrono::steady_clock::now() - start).count() < 12) { // Allow time for Suspended Ledger cascade
        int bytes = recv(sock, buffer, sizeof(buffer)-1, 0);
        if (bytes > 0) {
            buffer[bytes] = '\0';
            string msg(buffer);
            if (msg.find("SUSPEND_OUTPUT") != string::npos) {
                cout << "TestMutex: Received SUSPEND_OUTPUT command from Core/Heartbeat. Swallowing outbound messages!" << endl;
                is_muted = true;
            }
        }

        if (!is_muted && count < 100) {
            string alive = "TARGET:1|PAYLOAD:ALIVE\n";
            send(sock, alive.c_str(), alive.length(), 0);
            count++;
            this_thread::sleep_for(chrono::milliseconds(100)); // Spam frequently
        } else {
            this_thread::sleep_for(chrono::milliseconds(500)); // Just wait for cascade
        }
    }

    cout << "TestMutex sent " << count << " ALIVE messages and awaited consequence." << endl;

    closesocket(sock);
    return 0;
}