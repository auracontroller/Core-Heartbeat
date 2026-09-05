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

    const int num_clients = 10;
    SOCKET sockets[num_clients];

    for (int i = 0; i < num_clients; ++i) {
        sockets[i] = socket(AF_INET, SOCK_STREAM, 0);
        sockaddr_in server_addr = {};
        server_addr.sin_family = AF_INET;
        server_addr.sin_port = htons(5555);
        inet_pton(AF_INET, "127.0.0.1", &server_addr.sin_addr);

        if (connect(sockets[i], (struct sockaddr *)&server_addr, sizeof(server_addr)) == SOCKET_ERROR) {
            cerr << "TestPacts: Failed to connect client " << i << endl;
            return 1;
        }

        // Setup pact intervals ranging from 2000 to 11000 ms
        int interval = 2000 + (i * 1000);
        string pact = "TARGET:1|PAYLOAD:PACT:" + to_string(interval) + "\n";
        send(sockets[i], pact.c_str(), pact.length(), 0);
    }

    cout << "TestPacts established " << num_clients << " pacts." << endl;

    // Simulate staying alive at a pace that avoids the 2000ms spam throttle rule
    auto start = chrono::steady_clock::now();
    while (chrono::duration_cast<chrono::seconds>(chrono::steady_clock::now() - start).count() < 5) {
        this_thread::sleep_for(chrono::milliseconds(2100)); // Sleep just over 2000ms
        for (int i = 0; i < num_clients; ++i) {
            string alive = "TARGET:1|PAYLOAD:ALIVE\n";
            send(sockets[i], alive.c_str(), alive.length(), 0);
        }
    }

    for (int i = 0; i < num_clients; ++i) {
        closesocket(sockets[i]);
    }

    return 0;
}