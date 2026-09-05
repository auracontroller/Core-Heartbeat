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

    SOCKET sock = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in server_addr = {};
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(5555);
    inet_pton(AF_INET, "127.0.0.1", &server_addr.sin_addr);

    if (connect(sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) == SOCKET_ERROR) {
        cerr << "TestParsing: Failed to connect to CoreRelay." << endl;
        return 1;
    }

    cout << "TestParsing connected." << endl;

    // Send malformed packets
    string bad_msgs[] = {
        "TARGET:1\n", // Missing PAYLOAD
        "PAYLOAD:HELLO\n", // Missing TARGET
        "TARGET:1|PAYLOAD:\n", // Empty payload
        "TARGET:1|PAYLOAD:HELLO", // Missing newline
        "TARGET:|PAYLOAD:HELLO\n", // Empty target
        "TARGET:ABC|PAYLOAD:HELLO\n" // Non-integer target
    };

    for (const string& msg : bad_msgs) {
        send(sock, msg.c_str(), msg.length(), 0);
        this_thread::sleep_for(chrono::milliseconds(100));
    }

    // Send one more bad message that missing newline
    string no_nl = "TARGET:1|PAYLOAD:NO_NEWLINE";
    send(sock, no_nl.c_str(), no_nl.length(), 0);

    cout << "TestParsing sent malformed packets." << endl;

    this_thread::sleep_for(chrono::seconds(1));

    closesocket(sock);
    return 0;
}